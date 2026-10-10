#pragma once
#include <cmath>
namespace praxis::companion {
enum class WeatherMode : unsigned { Clear, Rain, Snow };
struct WeatherOptions {
    unsigned mode{};
    float intensity=13.5f;
    bool operator==(WeatherOptions const&) const noexcept = default;
};
inline bool validWeatherOptions(WeatherOptions const& value) noexcept {
    return value.mode<=static_cast<unsigned>(WeatherMode::Snow)
        && std::isfinite(value.intensity) && value.intensity>=1.f && value.intensity<=30.f;
}
}
