#pragma once
#include "resourcepacks/NativeTaskIdentity.h"
#include <map>
#include <mutex>
#include <optional>

namespace praxis::companion::packs {
// Queue-time identities are copied before native execution. No native task,
// callable, renderer or resource is owned/destroyed by this observer.
struct NativeTaskStamp {
    std::uintptr_t game{}, manager{};
    std::uint64_t world{}, serial{};
    CallbackBinding transaction;
    // Diagnostic-only owner identity. A zero world is never implicitly valid:
    // pre-game observation must carry its own explicit nonzero session.
    std::uint64_t preGameSession{};
    bool valid() const noexcept {
        return game && manager && serial && ((world && !preGameSession) || (!world && preGameSession));
    }
};
inline bool sameTaskCycle(NativeTaskStamp const& a, NativeTaskStamp const& b) noexcept {
    return a.valid() && b.valid() && a.game == b.game && a.manager == b.manager
        && a.world == b.world && a.serial == b.serial && a.preGameSession == b.preGameSession
        && a.transaction.operation == b.transaction.operation && a.transaction.game == b.transaction.game
        && a.transaction.manager == b.transaction.manager && a.transaction.token == b.transaction.token
        && a.transaction.world == b.transaction.world && a.transaction.cycle == b.transaction.cycle
        && a.transaction.nativeRevision == b.transaction.nativeRevision;
}
enum class TaskTerminal { Unknown, Success, Cancelled, Error };
struct TaskTicket {
    std::uintptr_t task{}, group{};
    std::uint64_t queue{};
    NativeTaskStamp stamp;
};
struct TaskCycleView {
    std::size_t queued{}, pending{}, unknown{}, cancelled{}, errors{};
    bool initialEnqueueReturned{}, poisoned{};
    // taskComplete alone is a terminal classification, not a success result.
    bool succeeded() const noexcept {
        return queued && initialEnqueueReturned && !pending && !unknown
            && !cancelled && !errors && !poisoned;
    }
};
class TaskLineage {
    std::mutex mutex;
    std::map<std::uintptr_t, TaskTicket> tasks;
    struct Cycle { NativeTaskStamp stamp; TaskCycleView state; };
    std::map<std::uint64_t, Cycle> cycles;
    std::uint64_t queues{};
    bool overflow{};
    static constexpr std::size_t MaxTasks = 4096, MaxCycles = 64;
public:
    void begin(NativeTaskStamp const& stamp) {
        std::lock_guard lock(mutex);
        if (!stamp.valid() || cycles.contains(stamp.serial)) { overflow = true; return; }
        // Remove only fully ended observation rows. Pending old tasks keep their
        // original identities across a new cycle; they are never relabelled.
        for (auto it = cycles.begin(); it != cycles.end();)
            if (it->second.state.initialEnqueueReturned && !it->second.state.pending) it = cycles.erase(it);
            else ++it;
        if (cycles.size() >= MaxCycles) { overflow = true; return; }
        cycles.emplace(stamp.serial, Cycle{stamp, {}});
    }
    TaskTicket enqueue(std::uintptr_t task, std::uintptr_t group, NativeTaskStamp const& stamp) {
        std::lock_guard lock(mutex);
        auto cycle = cycles.find(stamp.serial);
        if (overflow || !task || !group || !stamp.valid() || cycle == cycles.end() || !sameTaskCycle(cycle->second.stamp, stamp)) { overflow = true; return {}; }
        if (tasks.contains(task) || tasks.size() >= MaxTasks || queues == UINT64_MAX) {
            cycle->second.state.poisoned = true; overflow = true; return {};
        }
        TaskTicket ticket{task, group, ++queues, stamp};
        tasks.emplace(task, ticket); ++cycle->second.state.queued; ++cycle->second.state.pending;
        return ticket;
    }
    std::optional<TaskTicket> lookup(std::uintptr_t task) {
        std::lock_guard lock(mutex);
        auto it = tasks.find(task); return it == tasks.end() ? std::nullopt : std::optional{it->second};
    }
    void initialEnqueueReturned(NativeTaskStamp const& stamp) {
        std::lock_guard lock(mutex);
        auto it = cycles.find(stamp.serial);
        if (it != cycles.end() && sameTaskCycle(it->second.stamp, stamp)) it->second.state.initialEnqueueReturned = true;
    }
    void poison(NativeTaskStamp const& stamp) {
        std::lock_guard lock(mutex);
        auto it = cycles.find(stamp.serial); if (it != cycles.end()) it->second.state.poisoned = true;
    }
    bool terminal(TaskTicket const& entry, std::uintptr_t group, TaskTerminal result) {
        std::lock_guard lock(mutex);
        auto it = tasks.find(entry.task); auto cycle = cycles.find(entry.stamp.serial);
        if (!entry.queue || it == tasks.end() || cycle == cycles.end() || it->second.queue != entry.queue
            || group != entry.group || !sameTaskCycle(it->second.stamp, entry.stamp)) {
            if (cycle != cycles.end()) cycle->second.state.poisoned = true; return false;
        }
        auto& view = cycle->second.state; --view.pending;
        if (result == TaskTerminal::Unknown) ++view.unknown;
        else if (result == TaskTerminal::Cancelled) ++view.cancelled;
        else if (result == TaskTerminal::Error) ++view.errors;
        tasks.erase(it); return true;
    }
    TaskCycleView view(NativeTaskStamp const& stamp) {
        std::lock_guard lock(mutex); auto it = cycles.find(stamp.serial);
        auto result = it == cycles.end() ? TaskCycleView{} : it->second.state;
        result.poisoned = result.poisoned || overflow || it == cycles.end() || !sameTaskCycle(it->second.stamp, stamp); return result;
    }
};
}
