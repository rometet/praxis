#pragma once

#include <cstdint>

namespace praxis::companion {

enum class WeatherRenderSuppression : unsigned char { None, RainAndSnow, All };

// NoRender keeps ownership of its existing all-species policy. WeatherChanger
// adds a separate request without modifying the saved settings/render mask.
inline WeatherRenderSuppression weatherRenderSuppression(bool noRenderWeather,
    bool weatherRequested, bool ready, bool admitted, bool cameraEligible,
    std::uint64_t capturedGeneration, std::uint64_t currentGeneration) noexcept {
    if (noRenderWeather) return WeatherRenderSuppression::All;
    if (weatherRequested && ready && admitted && cameraEligible
        && capturedGeneration == currentGeneration)
        return WeatherRenderSuppression::RainAndSnow;
    return WeatherRenderSuppression::None;
}

// The SDK explicitly assigns Rain=0 and Snow=1. No weather simulation, source
// target level, sky/time, particle mesh or other precipitation species changes.
template<class WeatherObject>
void suppressRenderRainAndSnow(WeatherObject& object) {
    object.mDensityRain = 0.0f;
    object.mDensitySnow = 0.0f;
    auto& parameters = object.mParams.get();
    parameters[0].fAlpha = 0.0f;
    parameters[1].fAlpha = 0.0f;
}

} // namespace praxis::companion
