#pragma once
#include "resourcepacks/TaskLineage.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace praxis::companion::packs {
enum class ReloadTraceEvent : unsigned {
    Session, TitleObserved, FlagGateInferred, FlushEnter, FlushReturned,
    ChangedStart, StartReturned, FinishEnter, FinishReturned,
    TaskQueued, TaskTerminal, TerrainEnter, TerrainReturned, ItemEnter, ItemReturned,
    CallbackUnknown, ForeignFlush, IdentityRejected, End
};
inline constexpr char const* reloadTraceLabel(ReloadTraceEvent e) noexcept {
    constexpr char const* labels[]{"session", "title_observed", "flag_gate_inferred", "flush_enter", "flush_returned",
        "changed_start", "start_returned", "finish_enter", "finish_returned", "task_queued", "task_body_terminal",
        "terrain_enter", "terrain_returned", "item_enter", "item_returned", "callback_unknown", "foreign_flush",
        "identity_rejected", "end"};
    auto index = static_cast<unsigned>(e); return index < std::size(labels) ? labels[index] : "invalid";
}
enum class ReloadTraceEnd : unsigned { None, Deadline, RecordLimit, CycleLimit, OwnerChanged, ObservationError, Stopped };
enum class ReloadTraceGuard : unsigned { Off, ImageUnknown, SymbolUnknown, FingerprintUnknown, Ready };
inline constexpr ReloadTraceGuard reloadTraceGuard(bool requested, bool image, bool symbol, bool fingerprint) noexcept {
    return !requested ? ReloadTraceGuard::Off : !image ? ReloadTraceGuard::ImageUnknown
        : !symbol ? ReloadTraceGuard::SymbolUnknown : !fingerprint ? ReloadTraceGuard::FingerprintUnknown : ReloadTraceGuard::Ready;
}
struct ReloadTraceOwner {
    std::uintptr_t game{}, packs{}, loads{};
    std::uint64_t world{};
    bool preGameKnown{};
    bool valid() const noexcept { return game && packs && loads && !world && preGameKnown; }
    bool operator==(ReloadTraceOwner const&) const noexcept = default;
};
struct ReloadTraceRecord {
    ReloadTraceEvent event{};
    std::uint64_t sequence{}, session{}, cycle{}, boundary{}, update{}, task{}, group{};
    unsigned value{};
    bool titleSeen{};
};
struct ReloadFlushTicket {
    std::uint64_t session{}, boundary{}, update{}, invocation{}, group{};
    explicit operator bool() const noexcept { return session && invocation; }
};
struct ReloadTraceSummary {
    std::uint64_t session{}, records{}, emitted{}, cycles{}, flushes{}, terminals{}, unknownBodies{}, errors{}, unknownCallbacks{};
    ReloadTraceEnd end{};
    bool titleSeen{}, lost{};
};
inline bool passiveReloadCallbackMatches(TaskTicket const& entry, TaskTicket const* current,
    std::uintptr_t game, std::uint64_t world, std::uintptr_t executingTask, bool healthy) noexcept {
    return healthy && entry.queue && current && current->queue==entry.queue && current->group==entry.group
        && current->task==entry.task && executingTask==entry.task && entry.stamp.game==game
        && entry.stamp.world==world && !world && entry.stamp.preGameSession
        && sameTaskCycle(entry.stamp,current->stamp);
}
inline NativeTaskStamp passiveReloadParentStamp(std::uintptr_t executingTask, TaskTicket const* parent,
                                               NativeTaskStamp const& enclosing) noexcept {
    // An unrelated executing task cannot borrow a synchronous Start scope.
    if (executingTask && (!parent || parent->task!=executingTask)) return {};
    if (!executingTask && parent) return {};
    auto stamp=parent?parent->stamp:NativeTaskStamp{};
    if (enclosing.valid()) {
        if (stamp.valid() && !sameTaskCycle(stamp,enclosing)) return {};
        stamp=enclosing;
    }
    return stamp;
}

// Small diagnostic journal, not a native fence or a replacement task graph.
// Internally copied pointer identities are compared only; output contains IDs,
// fixed labels and bounded counts. No native object/capture is retained here.
class PassiveReloadTrace {
public:
    using TerminalNotice=void(*)(void*,ReloadTraceEnd) noexcept;
private:
    TerminalNotice const terminalNotice;
    void* const terminalContext;
    std::atomic_bool accepting{};
    std::mutex mutex;
    static constexpr std::size_t MaxRecords = 4096, MaxCycles = 4, MaxGroups = 64;
    std::array<ReloadTraceRecord, MaxRecords> journal{};
    std::array<NativeTaskStamp, MaxCycles> cycles{};
    std::array<std::uintptr_t, MaxGroups> groups{};
    ReloadTraceOwner owner;
    ReloadTraceSummary summary;
    std::uint64_t deadline{}, updates{}, boundary{}, boundaryUpdate{}, invocations{}, activeFlush{}, lastCycle{};
    std::uintptr_t titleGame{};
    std::size_t groupCount{};
    bool boundaryLost{}, boundaryConsumed{};
    void append(ReloadTraceEvent event, std::uint64_t cycle=0, std::uint64_t attempt=0, std::uint64_t update=0,
                std::uint64_t task=0, std::uint64_t group=0, unsigned value=0) noexcept {
        if (summary.records >= MaxRecords) return;
        auto sequence=++summary.records;
        journal[sequence-1]={event,sequence,summary.session,cycle,attempt,update,task,group,value,summary.titleSeen};
    }
    void close(ReloadTraceEnd end) noexcept {
        if (summary.end != ReloadTraceEnd::None) return;
        if(end==ReloadTraceEnd::None||static_cast<unsigned>(end)>static_cast<unsigned>(ReloadTraceEnd::Stopped))
            end=ReloadTraceEnd::ObservationError;
        summary.end=end; summary.lost=end!=ReloadTraceEnd::Stopped; accepting=false;
        // Must be leaf/nonblocking: close holds the journal mutex. Notify at
        // the state transition, before any native origin or later emit returns.
        if(terminalNotice)terminalNotice(terminalContext,end);
        append(ReloadTraceEvent::End,lastCycle,boundary,boundaryUpdate,0,0,static_cast<unsigned>(end));
    }
    bool admit(std::uint64_t now) noexcept {
        if (!accepting.load()) return false;
        if (now >= deadline) { close(ReloadTraceEnd::Deadline); return false; }
        // Reserve one record for an explicit loss/end marker.
        if (summary.records >= MaxRecords-4) { close(ReloadTraceEnd::RecordLimit); return false; }
        return true;
    }
    bool identify(ReloadTraceOwner const& value) noexcept {
        if (!value.valid()) { append(ReloadTraceEvent::IdentityRejected); return false; }
        if (titleGame && titleGame!=value.game) { close(ReloadTraceEnd::OwnerChanged); return false; }
        if (!summary.session) {
            owner=value; summary.session=1; append(ReloadTraceEvent::Session); return true;
        }
        if (owner != value) { close(ReloadTraceEnd::OwnerChanged); return false; }
        return true;
    }
    bool known(NativeTaskStamp const& stamp) const noexcept {
        if (!stamp.valid() || stamp.world || stamp.preGameSession!=summary.session
            || stamp.game!=owner.game || stamp.manager!=owner.packs) return false;
        for (std::size_t i=0;i<summary.cycles;++i) if (sameTaskCycle(cycles[i],stamp)) return true;
        return false;
    }
public:
    explicit PassiveReloadTrace(TerminalNotice notice=nullptr,void* context=nullptr) noexcept
        :terminalNotice(notice),terminalContext(context){}
#ifdef PRAXIS_PASSIVE_RELOAD_STOP_NOTICE_TEST
    void fixtureCounters(std::uint64_t update,std::uint64_t batch,std::uint64_t invocation) noexcept {
        std::lock_guard lock(mutex);updates=update;boundary=batch;invocations=invocation;
    }
#endif
    // Once per process. A failed optional hook installation closes this journal;
    // it is never re-opened over old in-flight observations.
    void enable(bool supported, std::uint64_t now) noexcept {
        std::lock_guard lock(mutex);
        if (supported && !accepting && !deadline && summary.end==ReloadTraceEnd::None) {
            deadline=now+600000; if (deadline<now) return; accepting=true;
        }
    }
    bool enabled() const noexcept { return accepting.load(); }
    std::uint64_t update(std::uint64_t now, std::uint64_t world, bool ready, bool inGame) noexcept {
        if (!enabled()) return 0;
        std::lock_guard lock(mutex); if (!admit(now)) return 0;
        if (world || inGame || (summary.session && !ready)) { close(ReloadTraceEnd::OwnerChanged); return 0; }
        if (!ready) return 0;
        if (++updates==0) { close(ReloadTraceEnd::ObservationError); return 0; }
        return updates;
    }
    void title(std::uintptr_t game, std::uint64_t now) noexcept {
        if (!enabled()) return; std::lock_guard lock(mutex);
        if (!admit(now) || !game) return;
        if ((summary.session && owner.game!=game) || (titleGame && titleGame!=game)) {
            close(ReloadTraceEnd::OwnerChanged); return;
        }
        if (!summary.titleSeen) {
            titleGame=game;
            summary.titleSeen=true; append(ReloadTraceEvent::TitleObserved,lastCycle);
        }
    }
    ReloadFlushTicket flushBegin(ReloadTraceOwner const& value, std::uint64_t updateId,
                                bool callerMatches, bool normalUpdate, bool executingTask, std::uint64_t now,
                                std::uintptr_t group) noexcept {
        if (!enabled()) return {}; std::lock_guard lock(mutex); if (!admit(now)) return {};
        if (!callerMatches || !normalUpdate || executingTask || !updateId || !group || activeFlush) {
            if (boundaryUpdate==updateId) boundaryLost=true;
            append(ReloadTraceEvent::ForeignFlush,0,0,updateId); return {};
        }
        if (!summary.titleSeen) { append(ReloadTraceEvent::IdentityRejected); return {}; }
        if (!identify(value)) return {};
        if (boundaryUpdate!=updateId || boundaryConsumed) {
            boundaryUpdate=updateId; ++boundary; boundaryLost=false; boundaryConsumed=false;
            if (!boundary) { close(ReloadTraceEnd::ObservationError); return {}; }
            // Static call-path inference only: no field or isComplete call.
            append(ReloadTraceEvent::FlagGateInferred,lastCycle,boundary,updateId);
        }
        std::size_t index{};
        while (index<groupCount && groups[index]!=group) ++index;
        if (index==groupCount) {
            if (groupCount==MaxGroups) { close(ReloadTraceEnd::ObservationError); return {}; }
            groups[groupCount++]=group;
        }
        activeFlush=++invocations;
        if (!activeFlush) { close(ReloadTraceEnd::ObservationError); return {}; }
        ++summary.flushes;
        append(ReloadTraceEvent::FlushEnter,lastCycle,boundary,updateId,0,index+1);
        return {summary.session,boundary,updateId,activeFlush,index+1};
    }
    void flushEnd(ReloadFlushTicket ticket, bool returned, std::uint64_t now) noexcept {
        if (!ticket || !enabled()) return; std::lock_guard lock(mutex); if (!admit(now)) return;
        if (ticket.session!=summary.session || ticket.boundary!=boundary || ticket.update!=boundaryUpdate
            || ticket.invocation!=activeFlush) { close(ReloadTraceEnd::ObservationError); return; }
        activeFlush=0; boundaryLost=boundaryLost || !returned;
        append(ReloadTraceEvent::FlushReturned,lastCycle,ticket.boundary,ticket.update,0,ticket.group,returned?1:0);
    }
    NativeTaskStamp start(ReloadTraceOwner const& value, std::uint64_t serial, std::uint64_t updateId,
                          bool normalUpdate, bool fullVanilla, std::uint64_t now) noexcept {
        if (!enabled()) return {}; std::lock_guard lock(mutex); if (!admit(now)) return {};
        if (!summary.titleSeen || !normalUpdate || !updateId || !serial || !identify(value)) {
            append(ReloadTraceEvent::IdentityRejected); return {};
        }
        if (summary.cycles==MaxCycles) { close(ReloadTraceEnd::CycleLimit); return {}; }
        for (std::size_t i=0;i<summary.cycles;++i) if (cycles[i].serial==serial) {
            close(ReloadTraceEnd::ObservationError); return {};
        }
        NativeTaskStamp stamp{value.game,value.packs,0,serial,{},summary.session};
        cycles[summary.cycles++]=stamp; lastCycle=serial;
        bool linked=boundaryUpdate==updateId && boundary && !activeFlush && !boundaryLost && !boundaryConsumed;
        append(ReloadTraceEvent::ChangedStart,serial,linked?boundary:0,updateId,0,0,(linked?1u:0u)|(fullVanilla?2u:0u));
        // Each admitted ChangedStart consumes the previous batch, including
        // an unlinked/reentrant Start. Later cycles need fresh flush evidence.
        boundaryConsumed=true;
        return stamp;
    }
    void event(ReloadTraceEvent event, NativeTaskStamp const& stamp, std::uint64_t now,
               std::uint64_t task=0, unsigned value=0) noexcept {
        if (!enabled()) return; std::lock_guard lock(mutex); if (!admit(now)) return;
        if (!known(stamp)) { ++summary.unknownCallbacks; append(ReloadTraceEvent::CallbackUnknown); return; }
        if (event==ReloadTraceEvent::TaskTerminal) {
            ++summary.terminals;
            summary.unknownBodies += value==static_cast<unsigned>(TaskTerminal::Unknown);
            summary.errors += value==static_cast<unsigned>(TaskTerminal::Error);
        }
        append(event,stamp.serial,0,0,task,0,value);
    }
    void unknown(ReloadTraceEvent event, std::uint64_t now) noexcept {
        if (!enabled()) return; std::lock_guard lock(mutex); if (!admit(now)) return;
        ++summary.unknownCallbacks; append(event);
    }
    void stop(ReloadTraceEnd end=ReloadTraceEnd::Stopped) noexcept {
        std::lock_guard lock(mutex); if (deadline) close(end);
    }
    bool next(ReloadTraceRecord& record) noexcept {
        std::lock_guard lock(mutex); if (summary.emitted==summary.records) return false;
        record=journal[summary.emitted++]; return true;
    }
    ReloadTraceSummary view() noexcept { std::lock_guard lock(mutex); return summary; }
};
}
