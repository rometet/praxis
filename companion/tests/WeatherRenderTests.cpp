#include "core/WeatherRenderPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool ok, char const* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class T> struct Storage { T value; T& get() { return value; } };
struct Parameter { float fAlpha, speed; bool operator==(Parameter const&) const = default; };
struct RenderWeather {
    float mDensityRain, mDensitySnow;
    float mDensityPlankton, mDensityRedSpores, mDensityBlueSpores, mDensityAsh, mDensityWhiteAsh;
    Storage<std::array<Parameter, 7>> mParams;
    float luminance, fog, skyDarken, time;
};
RenderWeather nativeSample(float rain, float snow) {
    RenderWeather result{rain, snow, .2f, .3f, .4f, .5f, .6f, {}, .7f, .8f, .9f, .1f};
    result.mParams.value = {Parameter{.7f, 1.f}, {.8f, 2.f}, {.9f, 3.f}, {.4f, 4.f},
        {.5f, 5.f}, {.6f, 6.f}, {.3f, 7.f}};
    return result;
}
void preserved(RenderWeather const& result, RenderWeather const& native) {
    check(result.mDensityPlankton == native.mDensityPlankton
        && result.mDensityRedSpores == native.mDensityRedSpores
        && result.mDensityBlueSpores == native.mDensityBlueSpores
        && result.mDensityAsh == native.mDensityAsh
        && result.mDensityWhiteAsh == native.mDensityWhiteAsh, "biome species stay native");
    for (unsigned i = 2; i < 7; ++i)
        check(result.mParams.value[i] == native.mParams.value[i], "other parameters stay native");
    check(result.mParams.value[0].speed == native.mParams.value[0].speed
        && result.mParams.value[1].speed == native.mParams.value[1].speed, "motion parameters stay native");
    check(result.luminance == native.luminance && result.fog == native.fog
        && result.skyDarken == native.skyDarken && result.time == native.time,
        "combined lighting/fog/time stay native");
}
}
int main() {
    try {
        for (unsigned flags = 0; flags < 32; ++flags) {
            bool noRender = flags & 1, requested = flags & 2, ready = flags & 4;
            bool admitted = flags & 8, eligible = flags & 16;
            auto expected = noRender ? WeatherRenderSuppression::All
                : requested && ready && admitted && eligible ? WeatherRenderSuppression::RainAndSnow
                : WeatherRenderSuppression::None;
            check(weatherRenderSuppression(noRender, requested, ready, admitted, eligible, 5, 5) == expected,
                "independent requests, capability, lifecycle and camera admission");
            check(weatherRenderSuppression(noRender, requested, ready, admitted, eligible, 5, 6)
                == (noRender ? WeatherRenderSuppression::All : WeatherRenderSuppression::None),
                "generation retirement closes WeatherChanger without changing NoRender");
        }
        // Repeated OFF/ON/OFF with the native source changing while ON. This
        // exercises production selection/mutation, not the engine's registry,
        // tick execution or visible frame consumption.
        for (unsigned i = 0; i < 120; ++i) {
            auto actual = nativeSample(float(i + 1) / 121.f, float(120 - i) / 121.f);
            auto rendered = actual;
            bool enabled = i % 3 == 1;
            auto mode = weatherRenderSuppression(false, enabled, true, true, true, 7, 7);
            if (mode == WeatherRenderSuppression::RainAndSnow) suppressRenderRainAndSnow(rendered);
            if (enabled) {
                check(rendered.mDensityRain == 0.f && rendered.mDensitySnow == 0.f, "mid-world request suppresses densities");
                check(rendered.mParams.value[0].fAlpha == 0.f && rendered.mParams.value[1].fAlpha == 0.f,
                    "rain/snow alpha suppressed");
                check(actual.mDensityRain != 0.f && actual.mDensitySnow != 0.f, "authoritative source not changed");
            } else {
                check(rendered.mDensityRain == actual.mDensityRain && rendered.mDensitySnow == actual.mDensitySnow,
                    "OFF uses current native output, including changes while ON");
                check(rendered.mParams.value[0].fAlpha == actual.mParams.value[0].fAlpha
                    && rendered.mParams.value[1].fAlpha == actual.mParams.value[1].fAlpha, "OFF preserves current alpha");
            }
            preserved(rendered, actual);
        }
        check(weatherRenderSuppression(false, false, true, true, true, 9, 9) == WeatherRenderSuppression::None,
            "OFF during origin cannot publish earlier WeatherChanger request");
        check(weatherRenderSuppression(false, true, false, true, true, 9, 9) == WeatherRenderSuppression::None,
            "world exit during origin closes request");
        check(weatherRenderSuppression(false, true, true, false, true, 9, 9) == WeatherRenderSuppression::None,
            "module cleanup during origin closes request");
        check(weatherRenderSuppression(false, true, true, true, false, 9, 9) == WeatherRenderSuppression::None,
            "different camera or non-weather dimension rejected");
        check(weatherRenderSuppression(true, false, false, false, false, 9, 10) == WeatherRenderSuppression::All,
            "NoRender request retains its existing behavior after WeatherChanger OFF");
        std::cout << "WeatherRenderTests PASS " << checks << " checks\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
