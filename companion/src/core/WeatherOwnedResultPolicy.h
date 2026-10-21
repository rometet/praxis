#pragma once

#include <cstdint>

namespace praxis::companion {

// Only copied identifiers cross origin. The native method receiver owns the
// returned value; no renderer/dimension/frame pointer is saved by this policy.
struct WeatherOwnerSnapshot {
    std::uint64_t generation{};
    unsigned subClient{};
    int dimension{};
    bool eligible{}, hasWeather{};
};
inline bool sameWeatherResultOwner(WeatherOwnerSnapshot const& before,
    WeatherOwnerSnapshot const& after, std::uint64_t currentGeneration) noexcept {
    return before.eligible && after.eligible && before.hasWeather && after.hasWeather
        && before.generation == after.generation && after.generation == currentGeneration
        && before.subClient == after.subClient && before.dimension == after.dimension;
}

// Revalidation runs after the native producer, even when generation did not
// yet advance during a dimension/subclient transition. An unknown owner is a
// passthrough and cannot inherit an outer TLS render admission.
template<class Before, class Revalidate, class CurrentGeneration, class Apply>
bool withOwnedWeatherResult(Before const& before, Revalidate&& revalidate,
    CurrentGeneration&& currentGeneration, Apply&& apply) {
    auto const after = revalidate();
    if (!sameWeatherResultOwner(before, after, currentGeneration())) return false;
    apply(after);
    return true;
}

} // namespace praxis::companion
