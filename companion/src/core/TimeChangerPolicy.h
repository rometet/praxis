#pragma once

#include "core/Settings.h"
#include <cstdint>
#include <utility>

namespace praxis::companion {

// The value is an input to the native visual celestial-angle calculation.
// Neither the authoritative time nor a client Level/Dimension field is written.
inline constexpr int kVisualDayTicks = 6000;

struct VisualTimeContext {
    void const* dimension{};
    std::uint64_t generation{};
};
inline thread_local VisualTimeContext visualTimeContext{};
inline std::atomic_bool visualNativeAdmission{};
inline std::atomic<unsigned> visualNativeReaders{};

inline bool visualDayEnabled() noexcept {
    // Admission comes first: retained callbacks must not inspect a retired
    // world or even published gameplay settings during module cleanup.
    return visualNativeAdmission.load(std::memory_order_seq_cst)
        && runtimeReady.load(std::memory_order_acquire)
        && settings()->on(Feature::TimeChanger);
}

class VisualNativeLease {
public:
    VisualNativeLease() noexcept { visualNativeReaders.fetch_add(1, std::memory_order_seq_cst); }
    ~VisualNativeLease() { visualNativeReaders.fetch_sub(1, std::memory_order_seq_cst); }
    VisualNativeLease(VisualNativeLease const&) = delete;
    VisualNativeLease& operator=(VisualNativeLease const&) = delete;
};

class VisualTimeScope {
    VisualTimeContext previous_ = visualTimeContext;
public:
    explicit VisualTimeScope(void const* dimension,
                             std::uint64_t generation = worldGeneration.load(std::memory_order_acquire)) noexcept {
        // A rejected nested entry masks an outer scope rather than inheriting it.
        visualTimeContext = {};
        if (dimension && runtimeReady.load(std::memory_order_acquire)
            && visualNativeAdmission.load(std::memory_order_seq_cst)
            && generation == worldGeneration.load(std::memory_order_acquire)
            && settings()->on(Feature::TimeChanger)) {
            visualTimeContext = {dimension, generation};
        }
    }
    ~VisualTimeScope() { visualTimeContext = previous_; }
    VisualTimeScope(VisualTimeScope const&) = delete;
    VisualTimeScope& operator=(VisualTimeScope const&) = delete;
};

template<class Original>
decltype(auto) visualTimeCalculation(void const* dimension, int actualTime,
                                    float partialTick, Original&& original) {
    bool const change = dimension && dimension == visualTimeContext.dimension
        && visualTimeContext.generation == worldGeneration.load(std::memory_order_acquire)
        && runtimeReady.load(std::memory_order_acquire)
        && visualNativeAdmission.load(std::memory_order_seq_cst)
        && settings()->on(Feature::TimeChanger);
    return std::forward<Original>(original)(change ? kVisualDayTicks : actualTime,
                                            change ? 0.0f : partialTick);
}

template<class Original>
decltype(auto) visualRenderCallback(void const* rendererDimension, void const* playerDimension,
                                   bool primaryRenderer, bool playing, bool worldScreen,
                                   Original&& original,
                                   std::uint64_t generation = worldGeneration.load(std::memory_order_acquire)) {
    VisualTimeScope scope(primaryRenderer && playing && worldScreen
        && rendererDimension == playerDimension ? rendererDimension : nullptr, generation);
    return std::forward<Original>(original)();
}

} // namespace praxis::companion
