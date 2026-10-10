#include "core/WeatherChangerPolicy.h"
#include "core/WeatherChangerDiagnostics.h"
#include "core/TimeChangerPolicy.h"
#include "core/RuntimePolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool ok, char const* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class T> struct Storage { T value; auto& get() { return value; } };
struct ViewData { bool mDrawWeather = true, mIsShadowPass = false; };
struct Sky { float mFogLevel = .7f, mTimeOfDay = .75f, mSkyDarken = .35f, mAmbientBrightness = .6f; };
struct Weather { float rain = .65f, snow = .45f, ash = .5f, alpha = .8f; };
struct OwnedView {
    Storage<ViewData> mViewData;
    Storage<Sky> mSkyState;
    Weather weather;
    OwnedView() = default;
    OwnedView(OwnedView const&) = delete;
    OwnedView& operator=(OwnedView const&) = delete;
    OwnedView(OwnedView&&) = default;
    OwnedView& operator=(OwnedView&&) = default;
};
static_assert(!std::is_copy_constructible_v<OwnedView> && std::is_move_constructible_v<OwnedView>);
struct OwnedRain {
    float mRainPosX = 1.f, mRainPosY = 2.f, mRainPosZ = 3.f;
    int mRainPosSamples = 4, mRainCount = 5;
    Storage<std::vector<int>> mSmokeParticles{{7, 8}};
    Storage<std::vector<int>> mRainSplashParticles;
    OwnedRain() = default;
    OwnedRain(OwnedRain const&) = delete;
    OwnedRain& operator=(OwnedRain const&) = delete;
    OwnedRain(OwnedRain&&) = default;
    OwnedRain& operator=(OwnedRain&&) = default;
};
static_assert(!std::is_copy_constructible_v<OwnedRain> && std::is_move_constructible_v<OwnedRain>);
void publish(bool weather, bool noRenderWeather = false, bool time = false, bool fullbright = false) {
    auto next = std::make_shared<Settings>();
    next->enabled[static_cast<unsigned>(Feature::WeatherChanger)] = weather;
    next->enabled[static_cast<unsigned>(Feature::TimeChanger)] = time;
    next->enabled[static_cast<unsigned>(Feature::Fullbright)] = fullbright;
    if (noRenderWeather) next->renderMask = passBit(RenderPass::Weather);
    publishedSettings.store(std::move(next));
}
}
int main() {
    try {
        RuntimeConfig config;
        check(!config.weatherChanger && config.keybinds[8] == 0, "default OFF and unbound");
        check(static_cast<unsigned>(Feature::TimeChanger) == 7 && static_cast<unsigned>(Feature::WeatherChanger) == 8, "stable IDs");
        check(config.weatherOptions==WeatherOptions{0,13.5f} && validRuntimeConfig(config), "default Clear/intensity config valid");
        for (unsigned mode : {0u,1u,2u}) for (float intensity : {1.f,13.5f,30.f}) {
            config.weatherOptions={mode,intensity};
            check(validRuntimeConfig(config), "every supported mode/intensity boundary accepted");
        }
        for (auto invalid : {WeatherOptions{3,13.5f},WeatherOptions{255,13.5f},
             WeatherOptions{1,0.f},WeatherOptions{2,30.01f},
             WeatherOptions{0,std::numeric_limits<float>::quiet_NaN()},
             WeatherOptions{1,std::numeric_limits<float>::infinity()}}) {
            config.weatherOptions=invalid;
            check(!validRuntimeConfig(config), "unsupported mode/nonfinite/out-of-range intensity rejected");
        }
        config.weatherOptions={2,21.f};
        check(runtimeSettings(config,0,1u<<8).weatherOptions==config.weatherOptions, "runtime publication preserves mode/intensity");
        config.weatherChanger = true;
        check(!runtimeSettings(config, 0, 0).on(Feature::WeatherChanger), "missing capability fails closed");
        check(runtimeSettings(config, 0, 1u << 8).on(Feature::WeatherChanger), "confirmed capability publishes request");
        runtimeReady = true; weatherNativeAdmission = true; visualNativeAdmission = true; worldGeneration = 5;
        Weather actual; Sky actualSky;
        unsigned producerCalls{}, constructorCalls = 1;
        // A single entry-only constructor and repeated owned view generations
        // reproduce the user's toggle case without claiming native execution.
        auto nativeProducer = [&] {
            ++producerCalls;
            OwnedView view; view.weather = actual; view.mSkyState.value = actualSky;
            if (settings()->renderMask & passBit(RenderPass::Weather))
                view.weather.rain = view.weather.snow = view.weather.ash = view.weather.alpha = 0;
            return view;
        };
        auto frame = [&](bool hasWeather = true) {
            auto before = producerCalls;
            auto view = weatherRenderCallback(true, true, true, true, hasWeather, [&] {
                auto owned = nativeProducer(); applyWeatherView(owned); return owned;
            });
            check(producerCalls == before + 1, "producer runs exactly once even when OFF");
            check(!visualWeatherContext.eligible, "no borrowed view or eligibility retained");
            return view;
        };
        publish(false); auto view = frame();
        check(view.mViewData.value.mDrawWeather && view.mSkyState.value.mFogLevel == .7f, "OFF preserves native view");
        publish(true); view = frame();
        check(!view.mViewData.value.mDrawWeather && view.mSkyState.value.mFogLevel == 0, "mid-world ON applies to next view");
        check(view.weather.rain == .65f && view.weather.snow == .45f && view.weather.ash == .5f && view.weather.alpha == .8f, "NoRender native density/alpha intact");
        view.weather = actual;
        check(!view.mViewData.value.mDrawWeather, "legacy density restore cannot resurrect the weather draw gate");
        check(view.mSkyState.value.mTimeOfDay == .75f && view.mSkyState.value.mSkyDarken == .35f && view.mSkyState.value.mAmbientBrightness == .6f, "time and native lighting intact");
        actual.rain = .91f; actual.snow = .12f; actualSky.mFogLevel = .22f;
        view = frame();
        check(!view.mViewData.value.mDrawWeather && actual.rain == .91f, "native weather changes while ON");
        publish(false); view = frame();
        check(view.mViewData.value.mDrawWeather && view.weather.rain == .91f && view.weather.snow == .12f && view.mSkyState.value.mFogLevel == .22f, "mid-world OFF uses current weather instead of entry snapshot");
        check(constructorCalls == 1, "toggle requires no new constructor or reentry");
        publish(true);
        check(frame(false).mViewData.value.mDrawWeather, "weatherless dimension unchanged");
        weatherRenderCallback(true, true, true, true, true, [&] {
            for (int condition = 0; condition < 4; ++condition) {
                weatherRenderCallback(condition != 0, condition != 1, condition != 2, condition != 3, true, [&] {
                    auto outside = nativeProducer();
                    check(!applyWeatherView(outside) && outside.mViewData.value.mDrawWeather, "nested client/loading/UI/dimension rejection");
                });
                check(visualWeatherContext.eligible, "outer scope restored");
            }
            auto shadow = nativeProducer(); shadow.mViewData.value.mIsShadowPass = true;
            check(!applyWeatherView(shadow) && shadow.mViewData.value.mDrawWeather, "shadow view unchanged");
            bool workerUnchanged{};
            std::thread worker([&] { OwnedView owned; workerUnchanged = !applyWeatherView(owned) && owned.mViewData.value.mDrawWeather; });
            worker.join(); check(workerUnchanged, "scope cannot leak to another thread");
            auto pending = nativeProducer(); publish(false);
            check(!applyWeatherView(pending), "OFF during producer invalidates scope");
            publish(true); worldGeneration.fetch_add(1);
            check(!applyWeatherView(pending), "world change during producer invalidates scope");
        });
        publish(true, true); view = frame();
        check(!view.mViewData.value.mDrawWeather && view.weather.rain == 0 && view.weather.ash == 0, "both features preserve NoRender output");
        publish(false, true); view = frame();
        check(view.weather.rain == 0 && view.weather.ash == 0 && view.mSkyState.value.mFogLevel == .22f, "WeatherChanger OFF retains NoRender Weather");
        publish(true, false); view = frame();
        check(!view.mViewData.value.mDrawWeather && view.weather.ash == .5f, "NoRender OFF preserves native source state");
        int dimension{};
        for (bool day : {false, true}) for (bool bright : {false, true}) {
            publish(true, false, day, bright);
            int nativeTime{};
            visualRenderCallback(&dimension, &dimension, true, true, true, [&] {
                check(!frame().mViewData.value.mDrawWeather, "TimeChanger/Fullbright coexistence");
                visualTimeCalculation(&dimension, 18000, .5f, [&](int time, float) { nativeTime = time; return .5f; });
            });
            check(nativeTime == (day ? 6000 : 18000), "weather does not change time input");
        }
        auto retired = worldGeneration.load(); worldGeneration.fetch_add(1);
        weatherRenderCallback(true, true, true, true, true, [&] { auto owned = nativeProducer(); check(!applyWeatherView(owned), "retired renderer rejected"); }, retired);
        try { weatherRenderCallback(true, true, true, true, true, [] { throw std::runtime_error("fixture"); }); } catch (std::runtime_error const&) {}
        check(!visualWeatherContext.eligible, "exception restores scope");
        for (int i = 0; i < 2000; ++i) {
            publish(i % 2 == 0); worldGeneration.fetch_add(1); actual.rain = (i % 17) / 17.f; view = frame();
            check(view.mViewData.value.mDrawWeather == (i % 2 != 0) && view.weather.rain == actual.rain, "rapid toggles/worlds use current native weather");
        }
        // Exercise the production policy on fresh owned doRainUpdate results.
        // This fixture cannot prove the engine invokes/consumes the SDK hook.
        std::vector<int> currentSplashes{11, 12, 13};
        unsigned rainProducerCalls{}, removed{};
        auto rainProducer = [&] {
            ++rainProducerCalls;
            OwnedRain rain;
            rain.mRainSplashParticles.value = currentSplashes;
            return rain;
        };
        auto rainFrame = [&](bool eligible = true, bool hasWeather = true) {
            auto before = rainProducerCalls;
            auto rain = weatherRenderCallback(eligible, true, true, true, hasWeather, [&] {
                auto owned = rainProducer(); removed = suppressOwnedRainSplashes(owned); return owned;
            });
            check(rainProducerCalls == before + 1, "rain producer always runs exactly once");
            check(!visualWeatherContext.eligible, "rain scope does not retain camera or result");
            check(rain.mSmokeParticles.value == std::vector<int>({7, 8}) && rain.mRainPosX == 1.f
                && rain.mRainPosY == 2.f && rain.mRainPosZ == 3.f && rain.mRainPosSamples == 4
                && rain.mRainCount == 5, "smoke and native rain sample/count/position inputs preserved");
            return rain;
        };
        publish(false); auto rain = rainFrame();
        check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "OFF passes current native splashes through");
        publish(true); rain = rainFrame();
        check(removed == 3 && rain.mRainSplashParticles.value.empty() && currentSplashes.size() == 3, "ON clears only owned splash list, never source");
        currentSplashes = {21, 22}; publish(false); rain = rainFrame();
        check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "OFF uses newly generated splashes without an old snapshot");
        for (unsigned mode : {1u,2u}) for (bool noRender : {false,true}) {
            publish(true,noRender);
            auto requested=std::make_shared<Settings>(*settings());requested->weatherOptions={mode,19.f};
            publishedSettings.store(requested);rain=rainFrame();
            bool const keepNative=mode==static_cast<unsigned>(WeatherMode::Rain) && !noRender;
            check(keepNative ? (removed==0 && rain.mRainSplashParticles.value==currentSplashes)
                : (removed==currentSplashes.size() && rain.mRainSplashParticles.value.empty()),
                "Rain keeps native splash output; Snow/NoRender suppress only owned splashes");
            requested=std::make_shared<Settings>(*requested);requested->enabled[8]=false;
            publishedSettings.store(requested);currentSplashes.push_back(23);rain=rainFrame();
            check(removed==0 && rain.mRainSplashParticles.value==currentSplashes, "mode OFF uses current fresh native splashes");
        }
        currentSplashes={21,22};
        publish(true); rain = rainFrame(false);
        check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "non-primary/loading/dimension admission preserves splashes");
        rain = rainFrame(true, false);
        check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "weatherless dimensions preserve returned splashes");
        weatherRenderCallback(true, true, true, true, true, [&] {
            weatherRenderCallback(false, true, true, true, true, [&] {
                auto other = rainProducer();
                check(suppressOwnedRainSplashes(other) == 0 && other.mRainSplashParticles.value == currentSplashes, "rejected nested camera masks outer splash scope");
            });
            auto local = rainProducer();
            check(suppressOwnedRainSplashes(local) == 2, "outer splash scope restored after nested rejection");
            bool workerUnchanged{};
            std::thread worker([&] {
                auto outside = rainProducer();
                workerUnchanged = suppressOwnedRainSplashes(outside) == 0 && outside.mRainSplashParticles.value == currentSplashes;
            });
            worker.join(); check(workerUnchanged, "rain scope cannot leak to worker thread");
            auto pending = rainProducer(); publish(false);
            check(suppressOwnedRainSplashes(pending) == 0, "OFF during native rain update prevents removal");
            publish(true); worldGeneration.fetch_add(1);
            check(suppressOwnedRainSplashes(pending) == 0, "world switch during native rain update prevents removal");
        });
        auto oldRainGeneration = worldGeneration.load(); worldGeneration.fetch_add(1);
        weatherRenderCallback(true, true, true, true, true, [&] {
            auto old = rainProducer(); check(suppressOwnedRainSplashes(old) == 0, "retired rain camera generation rejected");
        }, oldRainGeneration);
        currentSplashes.clear(); rain = rainFrame();
        check(removed == 0 && rain.mRainSplashParticles.value.empty(), "empty native splash output stays empty");
        currentSplashes = {31};
        for (bool day : {false, true}) for (bool bright : {false, true}) for (bool noRender : {false, true}) {
            publish(true, noRender, day, bright); rain = rainFrame();
            check(removed == 1 && rain.mRainSplashParticles.value.empty(), "splash policy coexists with TimeChanger/Fullbright/NoRender settings");
            publish(false, noRender, day, bright); rain = rainFrame();
            check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "other features alone do not activate splash policy");
        }
        publish(true);
        runtimeReady = false;
        check(!weatherVisualEnabled() && frame().mViewData.value.mDrawWeather, "shutdown passthrough");
        rain = rainFrame(); check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "shutdown returns native splashes");
        runtimeReady = true; weatherNativeAdmission = false;
        check(!weatherVisualEnabled() && frame().mViewData.value.mDrawWeather, "retained hook admission disabled");
        rain = rainFrame(); check(removed == 0 && rain.mRainSplashParticles.value == currentSplashes, "retained rain hook admission disabled");
        { WeatherNativeLease lease; check(weatherNativeReaders == 1, "reader tracked"); }
        check(weatherNativeReaders == 0, "reader drained");
        check(actual.snow == .12f && actual.alpha == .8f && actualSky.mFogLevel == .22f, "native source never modified");
        resetWeatherChangerDiagnostics();
        check(weatherChangerDiagnosticSnapshot().viewCalls == 0, "view diagnostic reset");
        weatherChangerDiagnostics.viewCalls = 9; weatherChangerDiagnostics.viewsInScope = 7; weatherChangerDiagnostics.viewApplications = 6;
        weatherChangerDiagnostics.weatherMoveCalls = 1;
        weatherChangerDiagnostics.rainUpdateCalls = 8; weatherChangerDiagnostics.rainUpdatesInScope = 5;
        weatherChangerDiagnostics.rainSplashPositionsRemoved = 12;
        auto d = weatherChangerDiagnosticSnapshot();
        check(d.viewCalls == 9 && d.viewsInScope == 7 && d.viewApplications == 6 && d.weatherMoveCalls == 1, "view generation distinguished from entry-only move");
        check(d.rainUpdateCalls == 8 && d.rainUpdatesInScope == 5 && d.rainSplashPositionsRemoved == 12, "splash calls/scopes/removal counts distinguished");
        resetWeatherChangerDiagnostics();
        d = weatherChangerDiagnosticSnapshot();
        check(d.rainUpdateCalls == 0 && d.rainUpdatesInScope == 0 && d.rainSplashPositionsRemoved == 0, "splash diagnostics reset");
        std::cout << "WeatherChanger production owned-view/splash policy: " << checks << " checks PASS; SDK hooks and Minecraft visuals NOT RUN\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
