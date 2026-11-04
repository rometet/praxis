#pragma once
#include "resourcepacks/TaskLineage.h"
#include <atomic>
#include <memory>
#include <type_traits>

namespace praxis::companion::packs {
// A body outcome is copied CPU evidence. It never owns a native task, Async
// handle, callback capture, resource, or renderer, and is not a drain fence.
class TaskBodyWitness {
    enum class Returned { Unknown, Requeue, Done };
    std::atomic<std::uintptr_t> expected{};
    std::atomic<std::uint64_t> entered{}, running{};
    std::atomic<Returned> returned{Returned::Unknown};
    std::atomic_bool released{}, exception{}, poisoned{};
public:
    void poison() noexcept { poisoned = true; }
    void bind(std::uintptr_t task) noexcept {
        std::uintptr_t empty{};
        if (!task || !expected.compare_exchange_strong(empty, task)) poison();
    }
    void enter(std::uintptr_t task) noexcept {
        if (!task || expected.load() != task || released.load()) poison();
        auto priorRunning = running.fetch_add(1), priorEntered = entered.fetch_add(1);
        if (priorRunning || priorEntered == UINT64_MAX) poison();
        returned = Returned::Unknown;
    }
    void normalReturn(bool done) noexcept {
        returned = done ? Returned::Done : Returned::Requeue;
        if (running.fetch_sub(1) != 1) poison();
    }
    void exceptionalReturn() noexcept {
        exception = true; returned = Returned::Unknown;
        if (running.fetch_sub(1) != 1) poison();
    }
    void bodyReleased() noexcept {
        if (released.exchange(true) || running.load()) poison();
    }
    TaskTerminal classify(std::uintptr_t task) const noexcept {
        if (poisoned.load() || !task || expected.load() != task || !released.load()
            || running.load() || !entered.load()) return TaskTerminal::Unknown;
        if (exception.load()) return TaskTerminal::Error;
        return returned.load() == Returned::Done ? TaskTerminal::Success : TaskTerminal::Unknown;
    }
};

struct BodyReleaseSentinel {
    std::shared_ptr<TaskBodyWitness> witness;
    ~BodyReleaseSentinel() { if (witness) witness->bodyReleased(); }
};
template<class Callable> struct ObservedNativeBody {
    // Members are destroyed in reverse order: the ORIGINAL native callable and
    // its captures are released before the CPU witness can observe release.
    BodyReleaseSentinel released;
    Callable body;
    ObservedNativeBody(std::shared_ptr<TaskBodyWitness> witness, Callable&& callable)
        : released{std::move(witness)}, body(std::move(callable)) {}
    ObservedNativeBody(ObservedNativeBody const&) = delete;
    ObservedNativeBody& operator=(ObservedNativeBody const&) = delete;
};
template<class Callable, class Finished>
std::invoke_result_t<Callable&> invokeObservedNativeBody(ObservedNativeBody<Callable>& state,
    std::uintptr_t currentTask, Finished&& finished) {
    auto& witness = *state.released.witness; witness.enter(currentTask);
    try {
        if constexpr (std::is_void_v<std::invoke_result_t<Callable&>>) {
            state.body(); witness.normalReturn(true); return;
        } else {
            using Result = std::invoke_result_t<Callable&>;
            static_assert(std::is_nothrow_invocable_r_v<bool, Finished, Result const&>);
            auto result = state.body(); witness.normalReturn(finished(result)); return result;
        }
    } catch (...) { witness.exceptionalReturn(); throw; }
}

class TaskOutcomeRegistry {
    struct Entry {
        std::uintptr_t owner{};
        NativeTaskStamp stamp;
        std::uint64_t queue{};
        std::shared_ptr<TaskBodyWitness> witness;
    };
    std::mutex mutex;
    std::map<std::uintptr_t, Entry> entries;
    bool poisoned{};
    static constexpr std::size_t MaxEntries = 4096;
public:
    bool factory(std::uintptr_t task, std::uintptr_t owner, NativeTaskStamp const& stamp,
        std::shared_ptr<TaskBodyWitness> witness) {
        std::lock_guard lock(mutex);
        if (poisoned || !task || !owner || !stamp.valid() || !witness
            || entries.contains(task) || entries.size() >= MaxEntries) {
            poisoned = true; if (witness) witness->poison(); return false;
        }
        entries.emplace(task, Entry{owner, stamp, 0, std::move(witness)}); return true;
    }
    bool queued(TaskTicket const& ticket, std::uintptr_t owner) {
        std::lock_guard lock(mutex); auto it = entries.find(ticket.task);
        // Other native task specializations remain Unknown; never invent their
        // result from taskComplete or from another task's body witness.
        if (it == entries.end()) return !poisoned;
        auto& entry = it->second;
        if (poisoned || !ticket.queue || entry.queue || owner != entry.owner
            || !sameTaskCycle(entry.stamp, ticket.stamp)) {
            poisoned = true; entry.witness->poison(); return false;
        }
        entry.queue = ticket.queue; return true;
    }
    std::shared_ptr<TaskBodyWitness> take(TaskTicket const& ticket, std::uintptr_t owner) {
        std::lock_guard lock(mutex); auto it = entries.find(ticket.task);
        if (it == entries.end()) return {};
        auto& entry = it->second;
        if (poisoned || !ticket.queue || entry.queue != ticket.queue || owner != entry.owner
            || !sameTaskCycle(entry.stamp, ticket.stamp)) {
            poisoned = true; entry.witness->poison(); return {};
        }
        auto result = std::move(entry.witness); entries.erase(it); return result;
    }
    bool healthy() { std::lock_guard lock(mutex); return !poisoned; }
};
}
