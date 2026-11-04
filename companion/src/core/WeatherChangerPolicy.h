#pragma once

#include "core/Settings.h"
#include "core/WeatherDiagnosticSession.h"
#include <cstddef>
#include <cstdint>
#include <utility>

namespace praxis::companion {

struct VisualWeatherContext {
    bool eligible{};
    bool hasWeather{};
    std::uint64_t generation{};
};
inline thread_local VisualWeatherContext visualWeatherContext{};
inline std::atomic_bool weatherNativeAdmission{};
inline std::atomic<unsigned> weatherNativeReaders{};

inline bool weatherVisualEnabled(WeatherDecision* observation = nullptr) noexcept {
    auto reject = [&](WeatherDecision reason) { if (observation) *observation = reason; return false; };
    if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return reject(WeatherDecision::AdmissionClosed);
    if (!runtimeReady.load(std::memory_order_acquire)) return reject(WeatherDecision::RuntimeNotReady);
    if (!settings()->on(Feature::WeatherChanger)) return reject(WeatherDecision::SettingOff);
    return true;
}

class WeatherNativeLease {
public:
    WeatherNativeLease() noexcept { weatherNativeReaders.fetch_add(1, std::memory_order_seq_cst); }
    ~WeatherNativeLease() { weatherNativeReaders.fetch_sub(1, std::memory_order_seq_cst); }
    WeatherNativeLease(WeatherNativeLease const&) = delete;
    WeatherNativeLease& operator=(WeatherNativeLease const&) = delete;
};

class VisualWeatherScope {
    VisualWeatherContext previous_ = visualWeatherContext;
public:
    VisualWeatherScope(bool eligible, bool hasWeather = false,
                       std::uint64_t generation = worldGeneration.load(std::memory_order_acquire),
                       WeatherDecision* observation = nullptr) noexcept {
        // Rejected nested UI/gameplay work must mask an outer render scope.
        visualWeatherContext = {};
        if (observation) *observation = WeatherDecision::ScopeRejected;
        if (!eligible || !weatherVisualEnabled(observation)) return;
        if (generation != worldGeneration.load(std::memory_order_acquire)) {
            if (observation) *observation = WeatherDecision::GenerationChanged;
            return;
        }
        visualWeatherContext = {true, hasWeather, generation};
        if (observation) *observation = WeatherDecision::ScopeAccepted;
    }
    ~VisualWeatherScope() { visualWeatherContext = previous_; }
    VisualWeatherScope(VisualWeatherScope const&) = delete;
    VisualWeatherScope& operator=(VisualWeatherScope const&) = delete;
};

template<class Original>
decltype(auto) weatherRenderCallback(bool primary, bool playing, bool worldScreen,
                                    bool sameDimension, bool hasWeather,
                                    Original&& original,
                                    std::uint64_t generation = worldGeneration.load(std::memory_order_acquire)) {
    VisualWeatherScope scope(primary && playing && worldScreen && sameDimension, hasWeather, generation);
    return std::forward<Original>(original)();
}

// Called on the owned value returned by createViewRenderObject, before it is
// handed to renderLevel/renderSky. Constructors may only run on world entry;
// their rain/alpha fields also belong to the unchanged NoRender tracker.
// Leave those fields intact and gate this view's weather draw instead. No
// borrowed frame, source object, or earlier weather value is retained.
template<class View>
bool applyWeatherView(View& view, WeatherDecision* observation = nullptr) {
    auto reject = [&](WeatherDecision reason) { if (observation) *observation = reason; return false; };
    if (!visualWeatherContext.eligible) return reject(WeatherDecision::ScopeRejected);
    if (visualWeatherContext.generation != worldGeneration.load(std::memory_order_acquire))
        return reject(WeatherDecision::GenerationChanged);
    if (!visualWeatherContext.hasWeather) return reject(WeatherDecision::NoWeather);
    if (!weatherVisualEnabled(observation)) return false;
    auto& data = view.mViewData.get();
    if (data.mIsShadowPass) return reject(WeatherDecision::Shadow);
    data.mDrawWeather = false;
    // Keep the same weather-display fog adjustment as the previous candidate,
    // on this fresh owned view rather than a long-lived sky constructor result.
    // Time, sky darkening, ambient brightness and all meshes remain native.
    view.mSkyState.get().mFogLevel = 0.f;
    if (observation) *observation = WeatherDecision::Applied;
    return true;
}

// doRainUpdate returns this camera-owned visual result by value. Discard only
// its explicitly named rain-splash positions. Leave smoke, rain sample/count
// inputs and all native weather/timing fields intact. No particle-engine or
// world object is stored or modified here.
template<class RainState>
std::size_t suppressOwnedRainSplashes(RainState& rain, WeatherDecision* observation = nullptr) {
    auto reject = [&](WeatherDecision reason) -> std::size_t { if (observation) *observation = reason; return 0; };
    if (!visualWeatherContext.eligible) return reject(WeatherDecision::ScopeRejected);
    if (!visualWeatherContext.hasWeather) return reject(WeatherDecision::NoWeather);
    if (visualWeatherContext.generation != worldGeneration.load(std::memory_order_acquire))
        return reject(WeatherDecision::GenerationChanged);
    if (!weatherVisualEnabled(observation)) return 0;
    auto const current=settings();
    if (current->weatherOptions.mode==static_cast<unsigned>(WeatherMode::Rain)
        && !(current->renderMask & passBit(RenderPass::Weather))) return 0;
    auto& positions = rain.mRainSplashParticles.get();
    auto const removed = positions.size();
    positions.clear();
    if (observation) *observation = WeatherDecision::Applied;
    return removed;
}

} // namespace praxis::companion
