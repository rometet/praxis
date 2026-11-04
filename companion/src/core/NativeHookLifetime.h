#pragma once

#include <atomic>
#include <mutex>
#include <utility>

namespace praxis::companion {

// The SDK does not specify retirement of an origin already executing during
// unhook. Pin before registration and retain hooks/trampolines until process
// exit. Closing admission is distinct from freeing their state or code.
class NativeHookLifetime {
    std::mutex control_;
    std::atomic_bool pinned_{}, retained_{}, admitted_{}, stopped_{};
    std::atomic<unsigned> readers_{};
public:
    template<class Pin> bool prepare(Pin&& pin) {
        std::lock_guard lock(control_);
        if (stopped_.load(std::memory_order_seq_cst)) return false;
        if (pinned_.load(std::memory_order_seq_cst)) return true;
        if (!std::forward<Pin>(pin)()) return false;
        pinned_.store(true, std::memory_order_seq_cst);
        return true;
    }
    // Called before the first registration attempt; a failed hook API must
    // never authorize unloading a possibly partially installed detour.
    void registrationStarted() noexcept { retained_.store(true, std::memory_order_seq_cst); }
    bool open() noexcept {
        std::lock_guard lock(control_);
        if (!pinned_.load(std::memory_order_seq_cst) || !retained()
            || stopped_.load(std::memory_order_seq_cst)) return false;
        admitted_.store(true, std::memory_order_seq_cst);
        return true;
    }
    void close() noexcept {
        std::lock_guard lock(control_);
        stopped_.store(true, std::memory_order_seq_cst);
        admitted_.store(false, std::memory_order_seq_cst);
    }
    bool retained() const noexcept { return retained_.load(std::memory_order_seq_cst); }
    unsigned readers() const noexcept { return readers_.load(std::memory_order_seq_cst); }
    bool admitted() const noexcept { return admitted_.load(std::memory_order_seq_cst); }

    class Lease {
        NativeHookLifetime& lifetime_;
        bool entered_{};
    public:
        explicit Lease(NativeHookLifetime& lifetime) noexcept : lifetime_(lifetime) {
            // Increment before checking admission: close cannot miss a reader
            // that was allowed to touch feature-owned resources.
            lifetime_.readers_.fetch_add(1, std::memory_order_seq_cst);
            entered_ = lifetime_.admitted();
        }
        ~Lease() { lifetime_.readers_.fetch_sub(1, std::memory_order_seq_cst); }
        bool admitted() const noexcept { return entered_ && lifetime_.admitted(); }
        Lease(Lease const&) = delete;
        Lease& operator=(Lease const&) = delete;
    };
};

} // namespace praxis::companion
