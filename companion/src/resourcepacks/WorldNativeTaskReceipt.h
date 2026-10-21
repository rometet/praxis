#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace praxis::companion::packs {
// A factory owns a task; only entry into its callable creates an invocation.
// Destruction and a normal return are deliberately not native completion.
struct WorldNativeProducer {
    std::uint64_t token{},attempt{},cycle{},producer{},finishProducer{};
    constexpr explicit operator bool() const noexcept {return token&&attempt&&cycle&&producer&&finishProducer;}
    constexpr bool same(WorldNativeProducer const& other) const noexcept {
        return token==other.token&&attempt==other.attempt&&cycle==other.cycle&&producer==other.producer
            &&finishProducer==other.finishProducer;
    }
};
struct WorldNativeInvocation {
    WorldNativeProducer source;
    std::uint64_t task{}, invocation{}, parentInvocation{};
    constexpr explicit operator bool() const noexcept { return bool(source) && task && invocation; }
};

inline std::uint64_t nextWorldNativeTaskId() noexcept {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

struct WorldNativeTaskReceipt {
    const WorldNativeProducer source;
    const std::uint64_t task;
    const std::uint64_t parentInvocation;
    std::atomic<unsigned> entries{0}, returns{0}, throws{0};
    std::atomic<bool> disposed{false};
    std::atomic<std::uint64_t> lastInvocation{0};

    WorldNativeTaskReceipt(WorldNativeProducer producerId, std::uint64_t parent)
        : source(producerId), task(nextWorldNativeTaskId()), parentInvocation(parent) {}
    WorldNativeInvocation enter() noexcept {
        auto id=nextWorldNativeTaskId();
        entries.fetch_add(1,std::memory_order_relaxed);
        lastInvocation.store(id,std::memory_order_release);
        return {source,task,id,parentInvocation};
    }
    void returned() noexcept {returns.fetch_add(1,std::memory_order_release);}
    void threw() noexcept {throws.fetch_add(1,std::memory_order_release);}
    // Called when the factory's final wrapper reference is released. This is
    // not an executed receipt even if no callable invocation occurred.
    void dispose() noexcept {disposed.store(true,std::memory_order_release);}
    bool neverInvoked() const noexcept {return disposed.load(std::memory_order_acquire)&&!entries.load(std::memory_order_acquire);}
};

struct WorldNativeTrackedTask {
    std::shared_ptr<WorldNativeTaskReceipt> receipt;
    explicit WorldNativeTrackedTask(std::shared_ptr<WorldNativeTaskReceipt> value):receipt(std::move(value)){}
    ~WorldNativeTrackedTask(){if(receipt)receipt->dispose();}
};
}
