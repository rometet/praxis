#pragma once
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include "core/WeatherDiagnosticSession.h"
#include "core/WeatherResultTracePolicy.h"

namespace praxis::companion {

// Aggregate process-local counters only. No pointer, location, player, world
// name, weather source value or personal data is recorded or persisted.
struct WeatherChangerDiagnosticCounters {
    std::atomic<std::uint64_t> acceptedRenderScopes{}, rejectedRenderScopes{};
    std::atomic<std::uint64_t> weatherMoveCalls{}, weatherMovesInScope{};
    std::atomic<std::uint64_t> skyMoveCalls{}, skyMovesInScope{};
    std::atomic<std::uint64_t> viewCalls{}, viewsInScope{}, viewApplications{};
    std::atomic<std::uint64_t> rainUpdateCalls{}, rainUpdatesInScope{}, rainSplashPositionsRemoved{};
};
inline WeatherChangerDiagnosticCounters weatherChangerDiagnostics;
struct WeatherChangerDiagnosticSnapshot {
    std::uint64_t acceptedRenderScopes{}, rejectedRenderScopes{};
    std::uint64_t weatherMoveCalls{}, weatherMovesInScope{};
    std::uint64_t skyMoveCalls{}, skyMovesInScope{};
    std::uint64_t viewCalls{}, viewsInScope{}, viewApplications{};
    std::uint64_t rainUpdateCalls{}, rainUpdatesInScope{}, rainSplashPositionsRemoved{};
};
inline WeatherChangerDiagnosticSnapshot weatherChangerDiagnosticSnapshot() noexcept {
    auto const& d = weatherChangerDiagnostics;
    return {d.acceptedRenderScopes.load(std::memory_order_relaxed),
        d.rejectedRenderScopes.load(std::memory_order_relaxed),
        d.weatherMoveCalls.load(std::memory_order_relaxed),
        d.weatherMovesInScope.load(std::memory_order_relaxed),
        d.skyMoveCalls.load(std::memory_order_relaxed),
        d.skyMovesInScope.load(std::memory_order_relaxed),
        d.viewCalls.load(std::memory_order_relaxed),
        d.viewsInScope.load(std::memory_order_relaxed),
        d.viewApplications.load(std::memory_order_relaxed),
        d.rainUpdateCalls.load(std::memory_order_relaxed),
        d.rainUpdatesInScope.load(std::memory_order_relaxed),
        d.rainSplashPositionsRemoved.load(std::memory_order_relaxed)};
}
inline void resetWeatherChangerDiagnostics() noexcept {
    auto& d = weatherChangerDiagnostics;
    for (auto* value : {&d.acceptedRenderScopes, &d.rejectedRenderScopes,
        &d.weatherMoveCalls, &d.weatherMovesInScope,
        &d.skyMoveCalls, &d.skyMovesInScope,
        &d.viewCalls, &d.viewsInScope, &d.viewApplications,
        &d.rainUpdateCalls, &d.rainUpdatesInScope, &d.rainSplashPositionsRemoved})
        value->store(0, std::memory_order_relaxed);
    // The existing explicit reset control is the only start trigger. Sessions
    // are never persisted, automatically repeated or enabled by Weather ON.
    try {
        if(weatherResultTrial->start())weatherDiagnosticSession.start();
    } catch (...) {}
}
} // namespace praxis::companion
