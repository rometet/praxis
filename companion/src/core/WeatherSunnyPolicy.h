#pragma once
#include "core/WeatherChangerPolicy.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace praxis::companion {
inline bool weatherSunnyEnabled(WeatherDecision* observation=nullptr) noexcept {
    return weatherVisualEnabled(observation)
        && settings()->weatherOptions.mode==static_cast<unsigned>(WeatherMode::Clear);
}

// Version-bound native DTO, established from the 1.26.51.1 creator and both
// consumers. The SDK names SkyWeatherParameters but leaves it opaque. Preserve
// its two unused scalars, every color, and all non-weather coefficients.
template<class Color> struct WeatherSunnyParameters {
    float rainSkyWeight;
    float untouched4;
    float untouched8;
    Color rainSkyColor;
    Color rainCloudColor;
    float rainCloudWeight;
    Color thunderSkyColor;
    float thunderSkyWeight;
    Color thunderCloudColor;
    float thunderCloudWeight;
    Color flashSkyColor;
    float flashSkyWeight;
};

template<class Color>
WeatherSunnyParameters<Color> sunnyParameters(WeatherSunnyParameters<Color> const& native,
                                            bool witherDarkening) noexcept {
    auto owned = native;
    owned.rainSkyWeight = owned.rainCloudWeight = owned.flashSkyWeight = 0.f;
    // The native consumers intentionally reuse thunder coefficients for the
    // Wither. Its darkness remains a native non-weather visual effect.
    if (!witherDarkening) owned.thunderSkyWeight = owned.thunderCloudWeight = 0.f;
    return owned;
}

// Native getSkyDarken preserves celestial time and Wither brightness, then
// applies two weather factors and the final 0.8*x+0.2 transform. Undo only those
// factors. Never recompute time, set Dimension/Weather fields, or force noon.
inline std::optional<float> sunnySkyBrightness(float native, float fog,
                                               float lightning) noexcept {
    if (!std::isfinite(native) || !std::isfinite(fog) || !std::isfinite(lightning)
        || fog < 0.f || fog > 1.f || lightning < 0.f || lightning > 1.f) return {};
    float const factor = (1.f - fog * 0.3125f) * (1.f - lightning * 0.3125f);
    float const result = (native - 0.2f) / factor + 0.2f;
    if (!std::isfinite(result)) return {};
    return result;
}

// Clear and full Rain/Snow use the native weather envelope endpoints. Snow
// changes precipitation type; Bedrock shares its overcast sky with rain.
inline float weatherSkyEnvelope(WeatherOptions requested) noexcept {
    return requested.mode==static_cast<unsigned>(WeatherMode::Clear)?0.f:1.f;
}
inline std::optional<float> weatherSkyBrightness(float native,float fog,float lightning,
    WeatherOptions requested) noexcept {
    if(!validWeatherOptions(requested))return {};
    auto const clear=sunnySkyBrightness(native,fog,lightning);
    if(!clear)return {};
    return (*clear-.2f)*(1.f-weatherSkyEnvelope(requested)*.3125f)+.2f;
}

enum class SunnyStage : unsigned { None, View, PreRender, Light };
struct SunnyIdentity {
    std::uint64_t generation{};
    unsigned subClient{};
    int dimension{};
    friend bool operator==(SunnyIdentity const&, SunnyIdentity const&) = default;
};
struct SunnyFogRecord {
    SunnyIdentity identity{};
    unsigned frame{};
    float native{}, sunny{};
    bool valid{};
    WeatherOptions requested{};
};
struct SunnyColorValue { std::array<float, 4> native{}, sunny{}; };
struct SunnyCapture {
    WeatherOptions requested{};
    std::array<SunnyColorValue, 8> colors{};
    unsigned colorsUsed{};
    bool colorsOverflow{};
    SunnyColorValue cloud{};
    bool cloudSeen{};
    float skyNative{}, skySunny{};
    bool skySeen{};
    float fogNative{}, fogSunny{};
    bool fogSeen{};
};
struct SunnyContext {
    SunnyStage stage{};
    SunnyIdentity identity{};
    SunnyCapture* ownedCapture{};
    WeatherOptions requested{};
};
inline thread_local SunnyContext sunnyContext{};
class SunnyScope {
    SunnyContext previous_ = sunnyContext;
public:
    SunnyScope(SunnyStage stage, SunnyIdentity identity, SunnyCapture& owned,
               bool accepted) noexcept {
        sunnyContext = {};
        if (accepted && identity.generation == worldGeneration.load(std::memory_order_acquire)
            && weatherVisualEnabled()) {
            owned.requested=settings()->weatherOptions;
            sunnyContext = {stage, identity, &owned,owned.requested};
        }
    }
    ~SunnyScope() { sunnyContext = previous_; }
    SunnyScope(SunnyScope const&) = delete;
    SunnyScope& operator=(SunnyScope const&) = delete;
};

inline std::optional<std::array<float, 4>> sunnyColorReplacement(
    SunnyCapture const& capture, std::array<float, 4> const& native) noexcept {
    if (capture.colorsOverflow || capture.colorsUsed > capture.colors.size()) return {};
    std::optional<std::array<float, 4>> result;
    for (unsigned i = 0; i < capture.colorsUsed; ++i) {
        auto const& color = capture.colors[i];
        if (color.native != native) continue;
        // Equal native colors can arise after a full weather blend. Reject an
        // ambiguous match instead of inventing a sky/biome color association.
        if (result && *result != color.sunny) return {};
        result = color.sunny;
    }
    return result;
}

inline std::array<float, 3> sunnyCloudColor(std::array<float, 4> const& cloud,
    std::array<float, 4> const& sunrise, float fakeHdr) noexcept {
    float const blend = sunrise[3] * 0.35f;
    std::array<float, 3> result{};
    for (unsigned i = 0; i < 3; ++i)
        result[i] = std::fmax(cloud[i] * (1.f - blend)
            + sunrise[i] * blend - fakeHdr * 0.2f, 0.f);
    return result;
}

template<class Light>
bool applySunnyLight(Light& owned, SunnyCapture const& capture, std::uint64_t generation) {
    if (!capture.skySeen || !weatherVisualEnabled()
        || settings()->weatherOptions!=capture.requested
        || generation != worldGeneration.load(std::memory_order_acquire)
        || owned.mSkyDarken != capture.skyNative) return false;
    owned.mSkyDarken = capture.skySunny;
    owned.mSkyFlashTime = 0;
    return true;
}

template<class Color>
std::array<float,4> sunnyColorValue(Color const& color) noexcept {
    return {color.r,color.g,color.b,color.a};
}
template<class Color>
void setSunnyColor(Color& color, std::array<float,4> const& value) noexcept {
    color.r=value[0]; color.g=value[1]; color.b=value[2]; color.a=value[3];
}

// This plan owns only scalar/color values. Preparation reads the native owned
// return without modifying it. No render pointer survives the producer.
struct SunnyViewPlan {
    std::array<float,4> sky{};
    std::optional<std::array<float,4>> zenith, horizon;
    std::array<float,3> cloud{};
    float fakeHdr{}, skyBrightness{};
};

template<class View>
std::optional<SunnyViewPlan> prepareSunnyView(View const& view, SunnyCapture const& capture,
    SunnyFogRecord const& fog, SunnyIdentity identity, unsigned frame) {
    auto const& data = view.mViewData.get();
    auto const& sky = view.mSkyState.get();
    if (!capture.cloudSeen || !capture.skySeen || data.mIsShadowPass || data.mCameraUnderLiquid
        || !weatherSunnyEnabled() || identity.generation != worldGeneration.load(std::memory_order_acquire)
        || sky.mSkyDarken != capture.skyNative) return {};
    auto const clearSky = sunnyColorReplacement(capture,sunnyColorValue(data.mSkyColor.get()));
    if (!clearSky || !fog.valid || fog.identity != identity || fog.frame != frame
        || data.mFakeHDR != fog.native) return {};
    auto const sunrise = sunnyColorValue(data.mSunriseColor.get());
    auto const clearCloud = sunnyCloudColor(capture.cloud.sunny,sunrise,fog.sunny);
    auto const nativeCloud = sunnyCloudColor(capture.cloud.native,sunrise,fog.native);
    auto const& cloud = view.mCloudState.get().mCloudColor.get();
    if (cloud.r != nativeCloud[0] || cloud.g != nativeCloud[1] || cloud.b != nativeCloud[2]) return {};
    SunnyViewPlan plan{*clearSky,
        sunnyColorReplacement(capture,sunnyColorValue(data.mSkyZenithColor.get())),
        sunnyColorReplacement(capture,sunnyColorValue(data.mSkyHorizonColor.get())),
        clearCloud,fog.sunny,capture.skySunny};
    // A captured alternate must be usable before any field is changed.
    auto finite = [](auto const& values) {
        for (float value : values) if (!std::isfinite(value)) return false;
        return true;
    };
    if (!finite(plan.sky) || (plan.zenith && !finite(*plan.zenith))
        || (plan.horizon && !finite(*plan.horizon)) || !finite(capture.cloud.sunny)
        || !finite(sunrise) || !finite(plan.cloud) || !std::isfinite(plan.fakeHdr)
        || !std::isfinite(plan.skyBrightness)) return {};
    return plan;
}

template<class View>
void commitSunnyView(View& view, SunnyViewPlan const& plan) noexcept {
    auto& data = view.mViewData.get();
    auto& cloud = view.mCloudState.get().mCloudColor.get();
    setSunnyColor(data.mSkyColor.get(),plan.sky);
    if (plan.zenith) setSunnyColor(data.mSkyZenithColor.get(),*plan.zenith);
    if (plan.horizon) setSunnyColor(data.mSkyHorizonColor.get(),*plan.horizon);
    cloud.r=plan.cloud[0]; cloud.g=plan.cloud[1]; cloud.b=plan.cloud[2];
    data.mFakeHDR=plan.fakeHdr;
    view.mSkyState.get().mSkyDarken=plan.skyBrightness;
}

// Kept for the sky/light policy fixtures. Sky correction requires the full
// native correlation; precipitation admission is independent below.
template<class View>
bool applySunnyView(View& view, SunnyCapture const& capture, SunnyFogRecord const& fog,
                    SunnyIdentity identity, unsigned frame) {
    auto const plan=prepareSunnyView(view,capture,fog,identity,frame);
    if (!plan) return false;
    commitSunnyView(view,*plan);
    return true;
}

// Revalidate owner and frame after preparation, then recheck admission/settings
// and generation. Every rejection/exception precedes the first output store.
// The commit consists solely of writes into this owned result; no SDK/native
// callback, allocation, validation or fallible alternative follows a store.
template<class View, class Revalidate>
bool applySunnyWeatherView(View& view, SunnyCapture const& capture, SunnyFogRecord const& fog,
    SunnyIdentity identity, unsigned frame, Revalidate&& revalidate, WeatherDecision* observation=nullptr) {
    auto reject=[&](WeatherDecision reason) { if (observation) *observation=reason; return false; };
    auto admitted=[&] {
        if (!visualWeatherContext.eligible) return reject(WeatherDecision::ScopeRejected);
        if (visualWeatherContext.generation != identity.generation
            || identity.generation != worldGeneration.load(std::memory_order_acquire))
            return reject(WeatherDecision::GenerationChanged);
        if (!visualWeatherContext.hasWeather) return reject(WeatherDecision::NoWeather);
        return weatherSunnyEnabled(observation);
    };
    if (!admitted()) return false;
    if (view.mViewData.get().mIsShadowPass) return reject(WeatherDecision::Shadow);
    auto const plan=prepareSunnyView(view,capture,fog,identity,frame);
    if (!plan) return reject(WeatherDecision::ScopeRejected);
    try { if (!revalidate()) return reject(WeatherDecision::ScopeRejected); }
    catch (...) { return reject(WeatherDecision::ScopeRejected); }
    if (!admitted()) return false;
    commitSunnyView(view,*plan);
    auto& data=view.mViewData.get();
    auto& weather=view.mWeatherState.get();
    data.mDrawWeather=false;
    view.mSkyState.get().mFogLevel=0.f;
    weather.mDensityRain=weather.mDensitySnow=0.f;
    auto& parameters=weather.mParams.get();
    parameters[0].fAlpha=parameters[1].fAlpha=0.f;
    if (observation) *observation=WeatherDecision::Applied;
    return true;
}

// All modes use the fresh SDK-owned precipitation result and its native
// materials/runtime data. Clear does not require an optional sky observation.
// No previous view or modified density is cached for OFF restoration.
template<class View,class Revalidate>
bool applyWeatherPrecipitationView(View& view,WeatherOptions requested,Revalidate&& revalidate,
    WeatherDecision* observation=nullptr) {
    auto reject=[&](WeatherDecision reason) { if(observation)*observation=reason;return false; };
    auto admitted=[&] {
        if(!visualWeatherContext.eligible)return reject(WeatherDecision::ScopeRejected);
        if(visualWeatherContext.generation!=worldGeneration.load(std::memory_order_acquire))
            return reject(WeatherDecision::GenerationChanged);
        if(!visualWeatherContext.hasWeather)return reject(WeatherDecision::NoWeather);
        if(!weatherVisualEnabled(observation))return false;
        return validWeatherOptions(requested) && settings()->weatherOptions==requested;
    };
    if(!admitted())return false;
    auto& data=view.mViewData.get();
    if(data.mIsShadowPass)return reject(WeatherDecision::Shadow);
    if(data.mCameraUnderLiquid)return reject(WeatherDecision::ScopeRejected);
    auto& weather=view.mWeatherState.get();
    auto& parameters=weather.mParams.get();
    if(!std::isfinite(static_cast<float>(weather.mDensityRain))
        || !std::isfinite(static_cast<float>(weather.mDensitySnow))
        || !std::isfinite(static_cast<float>(parameters[0].fAlpha))
        || !std::isfinite(static_cast<float>(parameters[1].fAlpha))
        || weather.mDensityRain<0.f || weather.mDensitySnow<0.f
        || parameters[0].fAlpha<0.f || parameters[1].fAlpha<0.f)return reject(WeatherDecision::ScopeRejected);
    try { if(!revalidate())return reject(WeatherDecision::ScopeRejected); }
    catch(...) { return reject(WeatherDecision::ScopeRejected); }
    if(!admitted())return false;
    bool const suppressed=(settings()->renderMask & passBit(RenderPass::Weather))!=0;
    bool const rain=!suppressed && requested.mode==static_cast<unsigned>(WeatherMode::Rain);
    bool const snow=!suppressed && requested.mode==static_cast<unsigned>(WeatherMode::Snow);
    data.mDrawWeather=rain || snow;
    weather.mDensityRain=rain?requested.intensity:0.f;
    weather.mDensitySnow=snow?requested.intensity:0.f;
    parameters[0].fAlpha=rain?1.f:0.f;
    parameters[1].fAlpha=snow?1.f:0.f;
    if(requested.mode==static_cast<unsigned>(WeatherMode::Clear))view.mSkyState.get().mFogLevel=0.f;
    if(observation)*observation=WeatherDecision::Applied;
    return true;
}

// Each component owns scalar values and has its own native correspondence.
// A missing HDR observation must not discard an independently proven sky color
// or sky brightness. Never infer a color association from a similar value.
struct WeatherSkyViewPlan {
    std::optional<std::array<float,4>> sky,zenith,horizon;
    std::optional<std::array<float,3>> cloud;
    std::optional<float> fakeHdr,brightness;
    bool hdrCorrelated{};
    bool complete() const noexcept {return sky && cloud && brightness && hdrCorrelated;}
};
template<class View>
WeatherSkyViewPlan prepareWeatherSkyView(View const& view,SunnyCapture const& capture,
    SunnyFogRecord const& fog,SunnyIdentity identity,unsigned frame,WeatherOptions requested) {
    WeatherSkyViewPlan plan;
    auto const& data=view.mViewData.get();
    if(data.mIsShadowPass || data.mCameraUnderLiquid || !weatherVisualEnabled()
        || settings()->weatherOptions!=requested || capture.requested!=requested
        || identity.generation!=worldGeneration.load(std::memory_order_acquire))return plan;
    auto finite=[](auto const& values){for(float value:values)if(!std::isfinite(value))return false;return true;};
    auto replacement=[&](auto const& color){
        auto value=sunnyColorReplacement(capture,sunnyColorValue(color));
        if(value && !finite(*value))value.reset();
        return value;
    };
    plan.sky=replacement(data.mSkyColor.get());
    plan.zenith=replacement(data.mSkyZenithColor.get());
    plan.horizon=replacement(data.mSkyHorizonColor.get());
    if(capture.skySeen && view.mSkyState.get().mSkyDarken==capture.skyNative
        && std::isfinite(capture.skySunny))plan.brightness=capture.skySunny;
    float const nativeHdr=data.mFakeHDR;
    plan.hdrCorrelated=fog.valid && fog.identity==identity && fog.frame==frame
        && fog.requested==requested && fog.native==nativeHdr && std::isfinite(fog.sunny);
    float const targetHdr=plan.hdrCorrelated?fog.sunny:nativeHdr;
    if(!capture.cloudSeen || !finite(capture.cloud.native) || !finite(capture.cloud.sunny)
        || !std::isfinite(nativeHdr) || !std::isfinite(targetHdr))return plan;
    auto const sunrise=sunnyColorValue(data.mSunriseColor.get());
    auto const& cloud=view.mCloudState.get().mCloudColor.get();
    // The pinned CreateView producer has two explicit cloud branches:
    // configured palette skips sunrise; the other blends its sunrise color.
    // SDK ViewData retains sunrise in either branch. Match the actual owned
    // RGB exactly before selecting a branch, rejecting ambiguous alternatives.
    for(auto const& input:std::array{sunrise,std::array<float,4>{0,0,0,0}}) {
        if(!finite(input))continue;
        auto const expected=sunnyCloudColor(capture.cloud.native,input,nativeHdr);
        if(cloud.r!=expected[0] || cloud.g!=expected[1] || cloud.b!=expected[2])continue;
        auto const alternate=sunnyCloudColor(capture.cloud.sunny,input,targetHdr);
        if(!finite(alternate))continue;
        if(plan.cloud && *plan.cloud!=alternate){plan.cloud.reset();plan.fakeHdr.reset();return plan;}
        plan.cloud=alternate;
        if(targetHdr!=nativeHdr)plan.fakeHdr=targetHdr;
    }
    return plan;
}
template<class View>
void commitWeatherSkyView(View& view,WeatherSkyViewPlan const& plan) noexcept {
    auto& data=view.mViewData.get();
    if(plan.sky)setSunnyColor(data.mSkyColor.get(),*plan.sky);
    if(plan.zenith)setSunnyColor(data.mSkyZenithColor.get(),*plan.zenith);
    if(plan.horizon)setSunnyColor(data.mSkyHorizonColor.get(),*plan.horizon);
    if(plan.cloud){auto& color=view.mCloudState.get().mCloudColor.get();
        color.r=(*plan.cloud)[0];color.g=(*plan.cloud)[1];color.b=(*plan.cloud)[2];}
    if(plan.fakeHdr)data.mFakeHDR=*plan.fakeHdr;
    if(plan.brightness)view.mSkyState.get().mSkyDarken=*plan.brightness;
}
template<class View,class Revalidate>
bool applyWeatherSkyView(View& view,SunnyCapture const& capture,SunnyFogRecord const& fog,
    SunnyIdentity identity,unsigned frame,WeatherOptions requested,Revalidate&& revalidate,
    WeatherDecision* observation=nullptr,bool* skyApplied=nullptr) {
    if(skyApplied)*skyApplied=false;
    auto const plan=prepareWeatherSkyView(view,capture,fog,identity,frame,requested);
    if(!applyWeatherPrecipitationView(view,requested,std::forward<Revalidate>(revalidate),observation))return false;
    commitWeatherSkyView(view,plan);
    view.mSkyState.get().mFogLevel=weatherSkyEnvelope(requested);
    if(skyApplied)*skyApplied=plan.complete();
    return true;
}

// Prepare the optional sky plan before any output store. Valid native paths
// can skip the fog helper and produce HDR0, or interpolate sky colors. Those
// paths still admit Clear precipitation while retaining uncorrelated sky data.
template<class View,class Revalidate>
bool applyClearWeatherView(View& view,SunnyCapture const& capture,SunnyFogRecord const& fog,
    SunnyIdentity identity,unsigned frame,WeatherOptions requested,Revalidate&& revalidate,
    WeatherDecision* observation=nullptr,bool* sunnyApplied=nullptr) {
    if(sunnyApplied)*sunnyApplied=false;
    if(requested.mode!=static_cast<unsigned>(WeatherMode::Clear))return false;
    auto const plan=prepareSunnyView(view,capture,fog,identity,frame);
    if(!applyWeatherPrecipitationView(view,requested,std::forward<Revalidate>(revalidate),observation))return false;
    if(plan) {
        commitSunnyView(view,*plan);
        if(sunnyApplied)*sunnyApplied=true;
    }
    return true;
}

} // namespace praxis::companion
