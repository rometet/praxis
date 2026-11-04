#pragma once
#include "core/WeatherSunnyPolicy.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace praxis::companion {
inline void disableSunnyWeather() noexcept {
    weatherNativeAdmission.store(false,std::memory_order_seq_cst);
    availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::WeatherChanger)),std::memory_order_acq_rel);
}
template<class Alternative>
auto trySunnyAlternative(Alternative&& alternative) noexcept
    -> std::optional<std::invoke_result_t<Alternative>> {
    try { return std::forward<Alternative>(alternative)(); }
    catch (...) { disableSunnyWeather(); return {}; }
}
template<class Bindings,class Resolve>
bool sunnyBindingsMatch(std::uintptr_t base,Bindings const& bindings,Resolve&& resolve) {
    for(auto const& [index,rva]:bindings) {
        if(rva>std::numeric_limits<std::uintptr_t>::max()-base
            || resolve(index)!=base+rva) return false;
    }
    return true;
}
// Missing/mismatched bindings and installer exceptions close only Weather.
// Successful hooks remain pinned passthroughs; no unsafe trampoline retirement.
template<class Verify,class Install>
bool isolatedSunnyInstall(Verify&& verify,Install&& install) noexcept {
    disableSunnyWeather();
    try {
        if(!verify() || !install()) return true;
        availableFeatures.fetch_or(1u<<static_cast<unsigned>(Feature::WeatherChanger),std::memory_order_acq_rel);
        weatherNativeAdmission.store(true,std::memory_order_seq_cst);
    }catch(...){disableSunnyWeather();}
    return true; // Overall native startup can continue, including on failure.
}
struct SunnyClockCapture { float angle{};unsigned samples{}; };
inline thread_local SunnyClockCapture* sunnyClockCapture{};
class SunnyClockScope {
    SunnyClockCapture* previous_=sunnyClockCapture;
public:
    SunnyClockScope(SunnyClockCapture& owned,bool accepted) noexcept {sunnyClockCapture=accepted?&owned:nullptr;}
    ~SunnyClockScope(){sunnyClockCapture=previous_;}
    SunnyClockScope(SunnyClockScope const&)=delete;
    SunnyClockScope& operator=(SunnyClockScope const&)=delete;
};
inline bool sunnyValueMatches(float actual,float expected) noexcept {
    return std::isfinite(actual)&&std::isfinite(expected)&&std::abs(actual-expected)<=0.000001f;
}
inline std::optional<unsigned> sunnyCloudLookupIndex(float angle) noexcept {
    float const radians=(angle*3.1415927410125732f)*2.f;
    float const index=radians*10430.3779296875f+16384.f;
    if(!std::isfinite(index)||index< -2147483648.f||index>=2147483648.f)return {};
    return static_cast<unsigned>(static_cast<int>(index))&0xffffu;
}
// Reproduce the pinned cloud consumer's arithmetic from its *observed* native
// clock and LUT scalar. No second native cloud call or world getter is needed.
template<class Color>
std::array<float,4> sunnyCloudProducerValue(float sine,WeatherSunnyParameters<Color> const& p,
    float rain,float lightning,bool wither,bool configured) noexcept {
    float const daylight=std::clamp(sine*2.f+.5f,0.f,1.f);
    std::array<float,4> tint{1,1,1,1};
    if(rain>0) {
        float const amount=std::clamp(rain*p.rainCloudWeight,0.f,1.f);
        auto const palette=configured?sunnyColorValue(p.rainCloudColor):std::array<float,4>{.6f,.6f,.6f,1};
        for(unsigned i=0;i<4;++i)tint[i]=(palette[i]*amount)+(1.f-amount);
    }
    std::array<float,4> result{(.9f*daylight+.1f)*tint[0],(.9f*daylight+.1f)*tint[1],
        (.85f*daylight+.15f)*tint[2],tint[3]};
    if(wither||lightning>0) {
        float const amount=std::clamp(p.thunderCloudWeight*(wither?1.f:lightning),0.f,1.f);
        float const grey=((.3f*result[0]+.59f*result[1])+.11f*result[2])*.2f;
        auto const palette=configured?sunnyColorValue(p.thunderCloudColor):std::array<float,4>{grey,grey,grey,1};
        for(unsigned i=0;i<4;++i)result[i]=result[i]*(1.f-amount)+palette[i]*amount;
    }
    return result;
}
// Pinned Color producer 6235bc0..6235f17. Its rain branch uses current
// rain (not interpolated rain) > .2 and fog * the native sky coefficient.
// Retain the native palette, celestial intensity, Wither and configured mode.
// Lightning/flash are supplied separately, so alternatives never modify world
// Weather or re-enter the native producer with a fabricated Dimension.
template<class Color>
std::array<float,4> weatherColorProducerValue(std::array<float,4> base,float intensity,
    WeatherSunnyParameters<Color> const& p,float fog,float rain,float lightning,
    bool wither,bool configured,bool disableFlashes,int flashTicks,float partial) noexcept {
    std::array<float,4> result{};
    for(unsigned i=0;i<4;++i)result[i]=base[i]*intensity;
    if(fog>0.f && rain>.2f) {
        float const amount=std::clamp(fog*p.rainSkyWeight,0.f,1.f);
        auto const palette=sunnyColorValue(p.rainSkyColor);
        for(unsigned i=0;i<4;++i)result[i]=result[i]*(1.f-amount)+(palette[i]*intensity)*amount;
    }
    if(wither || lightning>0.f) {
        float const amount=std::clamp(p.thunderSkyWeight*(wither?1.f:lightning),0.f,1.f);
        float const grey=((.3f*result[0]+.59f*result[1])+.11f*result[2])*.2f;
        auto const palette=configured?sunnyColorValue(p.thunderSkyColor):std::array<float,4>{grey,grey,grey,1};
        for(unsigned i=0;i<4;++i)result[i]=result[i]*(1.f-amount)+palette[i]*amount;
    }
    if(!disableFlashes && flashTicks>0) {
        float const amount=std::min(1.f,static_cast<float>(flashTicks)-partial)*p.flashSkyWeight;
        auto const palette=sunnyColorValue(p.flashSkyColor);
        for(unsigned i=0;i<4;++i)result[i]=result[i]*(1.f-amount)+palette[i]*amount;
    }
    return result;
}

// The fog consumer's last operation is g + fog*(-g). Compute g from the single
// observed native angle, retaining its input direction and minimum exactly.
inline std::optional<float> sunnyFogProducerValue(float angle,std::array<float,3> direction,float minimum) noexcept {
    if(!std::isfinite(angle)||!std::isfinite(minimum)||minimum<0||minimum>=1)return {};
    for(float component:direction)if(!std::isfinite(component))return {};
    float const radians=(angle*3.1415927410125732f)*2.f;
    float const cosine=std::cos(radians),sine=std::sin(radians);
    float const factor=cosine<-.1f?0.f:cosine<0.f?cosine/.1f+1.f:1.f;
    float const dot=((direction[1]*cosine-direction[0]*sine)+direction[2]*0.f+1.f)*.5f;
    float const value=(std::clamp(dot*factor,minimum,1.f)-minimum)/(1.f-minimum);
    if(!std::isfinite(value))return {};
    return value;
}
} // namespace praxis::companion
