#include "core/WeatherSunnyPolicy.h"
#include "core/WeatherOwnedResultPolicy.h"
#include "core/WeatherResultTracePolicy.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>
#include <condition_variable>
using namespace praxis::companion;
namespace {
unsigned checks{}, failureCases{};
void check(bool ok,char const* why) { ++checks;if(!ok)throw std::runtime_error(why); }
void publish(bool enabled) {
    auto settings=std::make_shared<Settings>();
    settings->enabled[static_cast<unsigned>(Feature::WeatherChanger)]=enabled;
    settings->enabled[static_cast<unsigned>(Feature::TimeChanger)]=true;
    settings->enabled[static_cast<unsigned>(Feature::Fullbright)]=true;
    settings->renderMask=passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Particles);
    publishedSettings.store(settings);
}
void ready() { worldGeneration=17;runtimeReady=true;weatherNativeAdmission=true;publish(true);visualWeatherContext={true,true,17}; }
struct Color { float r{},g{},b{},a{1};friend bool operator==(Color const&,Color const&)=default; };
template<class T> struct Storage { T value{};T& get(){return value;}T const& get() const{return value;} };
struct Data {
    Storage<Color> mSkyColor,mSkyZenithColor,mSkyHorizonColor,mSunriseColor;
    float mFakeHDR=.7f,celestial=.8f,gamma=12;
    Color fog{.1f,.2f,.3f,1};
    bool mIsShadowPass{},mCameraUnderLiquid{},mDrawWeather=true,otherNativeFlag{};
};
struct Sky { float mSkyDarken=.55f,mFogLevel=.9f,geometry=42; };
struct Cloud { Storage<Color> mCloudColor;float mesh=77; };
struct Parameter { float fAlpha=.8f;unsigned mesh=765; };
struct Weather { float mDensityRain=.85f,mDensitySnow=.65f,ash=.4f;Storage<std::array<Parameter,7>> mParams; };
struct View { Storage<Data> mViewData;Storage<Sky> mSkyState;Storage<Cloud> mCloudState;Storage<Weather> mWeatherState;std::array<unsigned,32> opaque{}; };
static_assert(std::is_trivially_copyable_v<View>);
using Bytes=std::array<unsigned char,sizeof(View)>;
Bytes bytes(View const& view) { Bytes value;std::memcpy(value.data(),&view,sizeof view);return value; }
struct Fixture {
    View view;SunnyCapture capture;SunnyIdentity id{17,0,0};SunnyFogRecord fog{id,91,.7f,.2f,true};
    WeatherOwnerSnapshot before{17,0,0,true,true},after=before;
    unsigned currentFrame=91,revalidations{};bool panorama{};
    Fixture() {
        auto& data=view.mViewData.value;
        data.mSkyColor.value={.35f,.45f,.6f,1};data.mSkyZenithColor.value={.2f,.3f,.4f,1};
        data.mSkyHorizonColor.value={.5f,.6f,.7f,1};data.mSunriseColor.value={.8f,.3f,.1f,.5f};
        capture.colors[0]={{.35f,.45f,.6f,1},{.55f,.7f,.9f,1}};
        capture.colors[1]={{.2f,.3f,.4f,1},{.4f,.5f,.6f,1}};
        capture.colors[2]={{.5f,.6f,.7f,1},{.7f,.8f,.9f,1}};capture.colorsUsed=3;
        capture.cloud={{.5f,.4f,.3f,.7f},{.9f,.8f,.7f,.7f}};capture.cloudSeen=true;
        capture.skyNative=.55f;capture.skySunny=.9f;capture.skySeen=true;
        view.mCloudState.value.mCloudColor.value={std::bit_cast<float>(0x3ed33334u),
            std::bit_cast<float>(0x3e7851edu),std::bit_cast<float>(0x3e000001u),.7f};
        for(unsigned i=0;i<view.opaque.size();++i)view.opaque[i]=0xabcdef00+i;
        for(unsigned i=0;i<7;++i)view.mWeatherState.value.mParams.value[i]={.8f+i*.01f,765+i};
    }
    bool revalidate() {
        ++revalidations;
        return !panorama && currentFrame==91 && sameWeatherResultOwner(before,after,worldGeneration.load());
    }
};
template<class Fault>
void failure(char const* name,Fault&& inject,bool precommit=false) {
    ready();Fixture f;inject(f);auto const original=bytes(f.view);WeatherDecision decision{};
    bool const applied=applySunnyWeatherView(f.view,f.capture,f.fog,f.id,91,[&]{return f.revalidate();},&decision);
    check(!applied,name);check(bytes(f.view)==original,"every owned output byte remains native on failure");
    check(decision!=WeatherDecision::Applied,"failure cannot report a commit");
    check(f.revalidations==(precommit?1u:0u),"all checks occur before commit");
    ++failureCases;std::cout<<"native-output-equal: "<<name<<'\n';
}
void failures() {
    failure("scope ineligible",[](auto&){visualWeatherContext.eligible=false;});
    failure("scope generation changed",[](auto&){visualWeatherContext.generation=18;});
    failure("world generation changed",[](auto&){worldGeneration=18;});
    failure("no weather",[](auto&){visualWeatherContext.hasWeather=false;});
    failure("admission closed",[](auto&){weatherNativeAdmission=false;});
    failure("runtime stopped",[](auto&){runtimeReady=false;});
    failure("setting OFF",[](auto&){publish(false);});
    failure("cloud LUT read failure / cloud observation missing",[](auto& f){f.capture.cloudSeen=false;});
    failure("cloud native math/clock mismatch rejects observation",[](auto& f){f.capture.cloudSeen=false;f.capture.colorsUsed=2;});
    failure("sky observation missing",[](auto& f){f.capture.skySeen=false;});
    failure("shadow view",[](auto& f){f.view.mViewData.value.mIsShadowPass=true;});
    failure("underwater view",[](auto& f){f.view.mViewData.value.mCameraUnderLiquid=true;});
    failure("sky native value mismatch",[](auto& f){f.view.mSkyState.value.mSkyDarken=.6f;});
    failure("sky color missing",[](auto& f){f.capture.colorsUsed=0;});
    failure("color overflow",[](auto& f){f.capture.colorsOverflow=true;});
    failure("color count corrupt",[](auto& f){f.capture.colorsUsed=9;});
    failure("ambiguous sky color",[](auto& f){f.capture.colors[3]={f.capture.colors[0].native,{1,1,1,1}};f.capture.colorsUsed=4;});
    failure("fog math/clock mismatch or no prerender observation",[](auto& f){f.fog.valid=false;});
    failure("fog generation mismatch",[](auto& f){f.fog.identity.generation=18;});
    failure("fog subclient mismatch",[](auto& f){f.fog.identity.subClient=1;});
    failure("fog dimension mismatch",[](auto& f){f.fog.identity.dimension=1;});
    failure("fog frame mismatch",[](auto& f){f.fog.frame=90;});
    failure("native HDR mismatch",[](auto& f){f.view.mViewData.value.mFakeHDR=.6f;});
    for(unsigned component=0;component<3;++component)
        failure("native cloud RGB mismatch",[=](auto& f){auto& c=f.view.mCloudState.value.mCloudColor.value;if(component==0)c.r=.9f;if(component==1)c.g=.9f;if(component==2)c.b=.9f;});
    float const nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(unsigned color=0;color<3;++color)for(unsigned component=0;component<4;++component)
        failure("alternate sky/zenith/horizon is nonfinite",[=](auto& f){f.capture.colors[color].sunny[component]=nan;});
    for(unsigned component=0;component<4;++component)
        failure("alternate cloud is nonfinite",[=](auto& f){f.capture.cloud.sunny[component]=inf;});
    failure("sunrise nonfinite",[=](auto& f){f.view.mViewData.value.mSunriseColor.value.a=nan;});
    failure("alternate HDR nonfinite",[=](auto& f){f.fog.sunny=nan;});
    failure("alternate brightness nonfinite",[=](auto& f){f.capture.skySunny=nan;});
    failure("frame changes after preparation",[](auto& f){f.currentFrame=92;},true);
    failure("panorama after preparation",[](auto& f){f.panorama=true;},true);
    failure("owner no longer eligible",[](auto& f){f.after.eligible=false;},true);
    failure("owner no longer has weather",[](auto& f){f.after.hasWeather=false;},true);
    failure("owner dimension changed",[](auto& f){f.after.dimension=1;},true);
    failure("owner subclient changed",[](auto& f){f.after.subClient=1;},true);
    failure("owner generation changed",[](auto& f){f.after.generation=18;},true);
    // Inject a state change at the actual post-plan production revalidation
    // boundary, including exception. No product store has occurred yet.
    for(unsigned fault=0;fault<9;++fault) {
        ready();Fixture f;auto const original=bytes(f.view);WeatherDecision decision{};unsigned calls{};
        bool const result=applySunnyWeatherView(f.view,f.capture,f.fog,f.id,91,[&]{
            ++calls;check(bytes(f.view)==original,"native output immutable throughout preparation");
            if(fault==0)publish(false);if(fault==1)weatherNativeAdmission=false;
            if(fault==2)runtimeReady=false;if(fault==3)worldGeneration=18;
            if(fault==4)visualWeatherContext.eligible=false;if(fault==5)visualWeatherContext.hasWeather=false;
            if(fault==6)visualWeatherContext.generation=18;if(fault==7)return false;if(fault==8)throw std::runtime_error("owner read fault");
            return true;
        },&decision);
        check(!result && bytes(f.view)==original,"late state/failure/exception preserves complete native result");
        check(calls==1 && decision!=WeatherDecision::Applied,"one revalidation and no false commit");++failureCases;
        std::cout<<"native-output-equal: post-plan fault "<<fault<<'\n';
    }
}
void success() {
    ready();Fixture f;View const native=f.view;auto const original=bytes(f.view);
    auto const plan=prepareSunnyView(f.view,f.capture,f.fog,f.id,91);
    check(plan.has_value() && bytes(f.view)==original,"prepare is read-only even on success");
    WeatherDecision decision{};
    check(applySunnyWeatherView(f.view,f.capture,f.fog,f.id,91,[&]{
        check(bytes(f.view)==original,"all fields remain native at final admission");return f.revalidate();
    },&decision),"full sunny weather transaction succeeds");
    auto const& d=f.view.mViewData.value;auto const& s=f.view.mSkyState.value;auto const& w=f.view.mWeatherState.value;
    check(decision==WeatherDecision::Applied && f.revalidations==1,"single final admission reports commit");
    check(!d.mDrawWeather && s.mFogLevel==0 && w.mDensityRain==0 && w.mDensitySnow==0
        && w.mParams.value[0].fAlpha==0 && w.mParams.value[1].fAlpha==0,"rain/snow/Fog commit with sunny outputs");
    check(d.mSkyColor.value==Color{.55f,.7f,.9f,1} && d.mSkyZenithColor.value==Color{.4f,.5f,.6f,1}
        && d.mSkyHorizonColor.value==Color{.7f,.8f,.9f,1} && d.mFakeHDR==.2f && s.mSkyDarken==.9f,"sky colors and brightness commit");
    check(std::abs(f.view.mCloudState.value.mCloudColor.value.r-.8425f)<.000001f,"clear cloud commits");
    check(d.celestial==native.mViewData.value.celestial && d.gamma==12 && d.fog==native.mViewData.value.fog
        && d.mSunriseColor.value==native.mViewData.value.mSunriseColor.value && s.geometry==42
        && f.view.mCloudState.value.mesh==77 && f.view.mCloudState.value.mCloudColor.value.a==.7f
        && w.ash==.4f && f.view.opaque==native.opaque,"native time/gamma/fog/meshes/alpha/opaque fields preserved");
    for(unsigned i=0;i<7;++i) {
        check(w.mParams.value[i].mesh==native.mWeatherState.value.mParams.value[i].mesh,"native precipitation meshes preserved");
        if(i>=2)check(w.mParams.value[i].fAlpha==native.mWeatherState.value.mParams.value[i].fAlpha,"non-rain/snow species preserved");
    }
    check(settings()->renderMask==(passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Particles))
        && settings()->on(Feature::TimeChanger) && settings()->on(Feature::Fullbright),"coexisting settings remain unchanged");
    publish(false);Fixture latest;latest.view.mWeatherState.value.mDensityRain=.17f;
    auto const latestBytes=bytes(latest.view);
    check(!applySunnyWeatherView(latest.view,latest.capture,latest.fog,latest.id,91,[&]{return latest.revalidate();})
        && bytes(latest.view)==latestBytes,"OFF returns latest native output without cached restoration");
}
void publishMode(unsigned mode,float intensity=13.5f,bool noRender=false,bool enabled=true) {
    auto current=std::make_shared<Settings>(*settings());
    current->weatherOptions={mode,intensity};current->enabled[unsigned(Feature::WeatherChanger)]=enabled;
    current->renderMask=passBit(RenderPass::Fog)|passBit(RenderPass::Particles)
        |(noRender?passBit(RenderPass::Weather):0);
    publishedSettings.store(current);
}
void precipitationModes() {
    for(unsigned mode:{1u,2u})for(float intensity:{1.f,13.5f,30.f})for(bool noRender:{false,true}) {
        ready();publishMode(mode,intensity,noRender);Fixture f;
        auto expected=f.view;
        auto& weather=expected.mWeatherState.value;
        expected.mViewData.value.mDrawWeather=!noRender;
        weather.mDensityRain=!noRender&&mode==1?intensity:0.f;
        weather.mDensitySnow=!noRender&&mode==2?intensity:0.f;
        weather.mParams.value[0].fAlpha=!noRender&&mode==1?1.f:0.f;
        weather.mParams.value[1].fAlpha=!noRender&&mode==2?1.f:0.f;
        auto const request=settings()->weatherOptions;
        auto const state=settings();
        auto const original=bytes(f.view);WeatherDecision decision{};
        bool const applied=applyWeatherPrecipitationView(f.view,request,[&]{
            check(bytes(f.view)==original,"mode preparation preserves native output until final revalidation");
            return f.revalidate();
        },&decision);
        if(!applied)std::cerr<<"mode="<<mode<<" intensity="<<intensity<<" NoRender="<<noRender
            <<" decision="<<unsigned(decision)<<" revalidations="<<f.revalidations
            <<" scope="<<visualWeatherContext.eligible<<','<<visualWeatherContext.hasWeather<<','<<visualWeatherContext.generation
            <<" world="<<worldGeneration.load()<<" ready="<<runtimeReady.load()<<" admission="<<weatherNativeAdmission.load()<<'\n';
        check(applied,"Rain/Snow owned output commits");
        check(bytes(f.view)==bytes(expected),"only draw/rain/snow density and their alpha fields change; all native sky/time/material/opaque data preserved");
        check(settings()==state && decision==WeatherDecision::Applied && f.revalidations==1,
            "NoRender priority and Time/Fullbright/config retained without side effects");
        SunnyCapture untouched;SunnyScope scope(SunnyStage::View,f.id,untouched,true);
        check(sunnyContext.stage==SunnyStage::View && sunnyContext.requested==settings()->weatherOptions,
            "Rain/Snow collect alternatives for the selected weather sky with the same owned scope");
    }
    for(unsigned fault=0;fault<22;++fault) {
        ready();publishMode(1);Fixture f;auto request=settings()->weatherOptions;
        auto const nan=std::numeric_limits<float>::quiet_NaN();
        if(fault==0)visualWeatherContext.eligible=false;if(fault==1)visualWeatherContext.hasWeather=false;
        if(fault==2)visualWeatherContext.generation=18;if(fault==3)worldGeneration=18;
        if(fault==4)weatherNativeAdmission=false;if(fault==5)runtimeReady=false;
        if(fault==6)publishMode(1,13.5f,false,false);if(fault==7)publishMode(2);
        if(fault==8)request.mode=3;if(fault==9)request.intensity=nan;
        if(fault==10)f.view.mViewData.value.mIsShadowPass=true;
        if(fault==11)f.view.mViewData.value.mCameraUnderLiquid=true;
        if(fault==12)f.view.mWeatherState.value.mDensityRain=nan;
        if(fault==13)f.view.mWeatherState.value.mDensitySnow=-1;
        if(fault==14)f.view.mWeatherState.value.mParams.value[0].fAlpha=nan;
        if(fault==15)f.view.mWeatherState.value.mParams.value[1].fAlpha=-1;
        auto const original=bytes(f.view);WeatherDecision decision{};
        bool const result=applyWeatherPrecipitationView(f.view,request,[&]{
            if(fault==16)return false;if(fault==17)throw std::runtime_error("owner read exception");
            if(fault==18)publishMode(2);if(fault==19)publishMode(1,14);
            if(fault==20)publishMode(1,13.5f,false,false);if(fault==21)worldGeneration=18;
            return true;
        },&decision);
        check(!result && bytes(f.view)==original && decision!=WeatherDecision::Applied,
            "invalid, OFF, lifecycle, owner and mid-callback mode changes leave every native byte untouched");
        ++failureCases;
    }
    ready();publishMode(1);Fixture suppress;auto request=settings()->weatherOptions;
    check(applyWeatherPrecipitationView(suppress.view,request,[&]{publishMode(1,13.5f,true);return true;}),
        "NoRender changed before commit uses its latest state");
    check(!suppress.view.mViewData.value.mDrawWeather && suppress.view.mWeatherState.value.mDensityRain==0,
        "concurrent NoRender enable cannot be undone by Rain selection");
    // Each producer supplies its current native weather; no ON result becomes
    // a restoration snapshot. Exercise mode changes and OFF across generations.
    for(unsigned cycle=0;cycle<24;++cycle) {
        ready();worldGeneration=17+cycle;visualWeatherContext={true,true,17+cycle};
        for(unsigned mode:{1u,2u,1u}) {
            publishMode(mode);Fixture fresh;fresh.view.mWeatherState.value.mDensityRain=.01f*cycle;
            fresh.view.mSkyState.value.mFogLevel=.02f*cycle;
            check(applyWeatherPrecipitationView(fresh.view,settings()->weatherOptions,[]{return true;}),
                "mode switch reuses only its current native owned result");
        }
        publishMode(2,13.5f,false,false);Fixture native;
        native.view.mWeatherState.value.mDensityRain=.03f*cycle;
        native.view.mWeatherState.value.mDensitySnow=.04f*cycle;
        native.view.mSkyState.value.mFogLevel=.01f*cycle;
        auto const original=bytes(native.view);
        check(!applyWeatherPrecipitationView(native.view,settings()->weatherOptions,[]{return true;})
            && bytes(native.view)==original,"OFF returns current native rain/snow/sky across world generations");
    }
    ready();Fixture clear;publishMode(1);auto const original=bytes(clear.view);
    check(!applySunnyWeatherView(clear.view,clear.capture,clear.fog,clear.id,91,[]{return true;})
        && bytes(clear.view)==original,"stale Clear capture cannot overwrite a Rain/Snow selection");
}
void expectPrecipitation(View& expected,unsigned mode,bool suppressed,float intensity) {
    auto& weather=expected.mWeatherState.value;
    bool const rain=mode==1 && !suppressed,snow=mode==2 && !suppressed;
    expected.mViewData.value.mDrawWeather=rain || snow;
    weather.mDensityRain=rain?intensity:0;weather.mDensitySnow=snow?intensity:0;
    weather.mParams.value[0].fAlpha=rain?1:0;weather.mParams.value[1].fAlpha=snow?1:0;
    if(mode==0)expected.mSkyState.value.mFogLevel=0;
}
void nativeHdr(Fixture& f,float hdr,bool fogAvailable) {
    f.view.mViewData.value.mFakeHDR=hdr;f.fog.native=hdr;f.fog.valid=fogAvailable;
    // Native float operation order, rather than a constant-folded expression
    // whose green component differed by one ULP in the HDR0 fixture.
    std::array<unsigned,3> const bits=hdr==0.f
        ? std::array<unsigned,3>{0x3f0d70a4u,0x3ec3d70bu,0x3e87ae15u}
        : std::array<unsigned,3>{0x3ed33334u,0x3e7851edu,0x3e000001u};
    auto& color=f.view.mCloudState.value.mCloudColor.value;
    color.r=std::bit_cast<float>(bits[0]);color.g=std::bit_cast<float>(bits[1]);color.b=std::bit_cast<float>(bits[2]);
}
void clearIndependentOfSky() {
    ready();publishMode(0);Fixture skipped;nativeHdr(skipped,0,false);
    auto const original=bytes(skipped.view);bool sky=true;WeatherDecision decision{};
    check(!applySunnyWeatherView(skipped.view,skipped.capture,skipped.fog,skipped.id,91,[]{return true;})
        && bytes(skipped.view)==original,"reproduce old rejection on valid native fog-helper skip/HDR0");
    auto expected=skipped.view;expectPrecipitation(expected,0,false,13.5f);
    check(applyClearWeatherView(skipped.view,skipped.capture,skipped.fog,skipped.id,91,
        settings()->weatherOptions,[&]{check(bytes(skipped.view)==original,"HDR0 output remains native until revalidation");return skipped.revalidate();},
        &decision,&sky),"Clear suppresses precipitation on valid fog-helper skip/HDR0 path");
    check(bytes(skipped.view)==bytes(expected) && !sky && decision==WeatherDecision::Applied,
        "HDR0 retains all native sky/cloud/time/resources and reports precipitation-only commit");
    for(unsigned optionalFault=0;optionalFault<14;++optionalFault) {
        ready();publishMode(0);Fixture f;
        if(optionalFault==0)f.capture.cloudSeen=false;if(optionalFault==1)f.capture.skySeen=false;
        if(optionalFault==2)f.capture.colorsUsed=0;if(optionalFault==3)f.capture.colorsOverflow=true;
        if(optionalFault==4)f.fog.valid=false;if(optionalFault==5)f.fog.frame=90;
        if(optionalFault==6)f.fog.identity.generation=16;if(optionalFault==7)f.fog.identity.dimension=1;
        if(optionalFault==8)f.view.mViewData.value.mFakeHDR=.6f;
        if(optionalFault==9)f.view.mCloudState.value.mCloudColor.value.r=.3f;
        if(optionalFault==10)f.view.mSkyState.value.mSkyDarken=.6f;
        if(optionalFault==11)f.view.mViewData.value.mSkyColor.value.r=.31f;
        if(optionalFault==12)f.capture.skySunny=std::numeric_limits<float>::quiet_NaN();
        if(optionalFault==13)f.fog.sunny=std::numeric_limits<float>::infinity();
        auto expected=f.view;expectPrecipitation(expected,0,false,13.5f);bool sunny=true;
        check(!prepareSunnyView(f.view,f.capture,f.fog,f.id,91),"optional native sky correlation is rejected");
        check(applyClearWeatherView(f.view,f.capture,f.fog,f.id,91,settings()->weatherOptions,[&]{return f.revalidate();},nullptr,&sunny),
            "optional sky failure cannot block admitted Clear precipitation");
        check(!sunny && bytes(f.view)==bytes(expected),"incomplete sky correction preserves every unrelated native byte");
    }
    // Cover mode/OFF, helper availability, native HDR0, NoRender and both
    // Time/Fullbright states. Every expected return starts from this native view.
    unsigned matrix{};
    for(unsigned mode:{0u,1u,2u})for(bool enabled:{false,true})for(bool fog:{false,true})
    for(float hdr:{0.f,.7f})for(bool noRender:{false,true})for(bool time:{false,true})for(bool full:{false,true}) {
        ready();publishMode(mode,13.5f,noRender,enabled);
        auto state=std::make_shared<Settings>(*settings());state->enabled[unsigned(Feature::TimeChanger)]=time;
        state->enabled[unsigned(Feature::Fullbright)]=full;publishedSettings.store(state);
        Fixture f;nativeHdr(f,hdr,fog);auto expected=f.view;auto const original=bytes(f.view);
        if(enabled)expectPrecipitation(expected,mode,noRender,13.5f);
        bool expectedSky=false;
        if(enabled && mode==0) {
            auto const plan=prepareSunnyView(f.view,f.capture,f.fog,f.id,91);
            if(plan.has_value()!=fog) {
                auto const correlated=sunnyCloudColor(f.capture.cloud.native,
                    sunnyColorValue(f.view.mViewData.value.mSunriseColor.value),f.fog.native);
                auto const& cloud=f.view.mCloudState.value.mCloudColor.value;
                std::cerr<<"matrix sky mismatch mode="<<mode<<" HDR="<<hdr<<" fog="<<fog<<" NoRender="<<noRender
                    <<" Time="<<time<<" Fullbright="<<full<<" nativeCloudBits="<<std::hex
                    <<std::bit_cast<unsigned>(cloud.r)<<','<<std::bit_cast<unsigned>(cloud.g)<<','<<std::bit_cast<unsigned>(cloud.b)
                    <<" correlatedBits="<<std::bit_cast<unsigned>(correlated[0])<<','<<std::bit_cast<unsigned>(correlated[1])
                    <<','<<std::bit_cast<unsigned>(correlated[2])<<std::dec<<'\n';
            }
            check(plan.has_value()==fog,"native HDR0 with correlated fog supports optional complete sky plan");
            if(plan){commitSunnyView(expected,*plan);expectedSky=true;}
        }
        bool sunny=true;WeatherDecision observation{};
        auto revalidate=[&]{check(bytes(f.view)==original,"matrix native bytes immutable before commit");return f.revalidate();};
        bool const applied=mode==0
            ? applyClearWeatherView(f.view,f.capture,f.fog,f.id,91,state->weatherOptions,revalidate,&observation,&sunny)
            : applyWeatherPrecipitationView(f.view,state->weatherOptions,revalidate,&observation);
        if(mode!=0)sunny=false;
        check(applied==enabled && sunny==expectedSky,"mode/OFF and optional sky result accurately reported");
        check(bytes(f.view)==bytes(expected),"matrix changes only admitted weather fields and fully correlated sky plan");
        check(settings()==state && f.revalidations==(enabled?1u:0u),"coexistence settings and one owner admission preserved");
        ++matrix;
    }
    check(matrix==192,"all 192 mode/OFF/fog/HDR/NoRender/Time/Fullbright combinations executed");
    for(unsigned fault=0;fault<26;++fault) {
        ready();publishMode(0);Fixture f;auto requested=settings()->weatherOptions;
        if(fault==0)visualWeatherContext.eligible=false;if(fault==1)visualWeatherContext.hasWeather=false;
        if(fault==2)visualWeatherContext.generation=18;if(fault==3)worldGeneration=18;
        if(fault==4)weatherNativeAdmission=false;if(fault==5)runtimeReady=false;
        if(fault==6)publishMode(0,13.5f,false,false);if(fault==7)publishMode(1);
        if(fault==8)requested.intensity=0;if(fault==9)requested.mode=2;
        if(fault==10)f.view.mViewData.value.mIsShadowPass=true;if(fault==11)f.view.mViewData.value.mCameraUnderLiquid=true;
        if(fault==12)f.view.mWeatherState.value.mDensityRain=std::numeric_limits<float>::quiet_NaN();
        if(fault==13)f.view.mWeatherState.value.mParams.value[1].fAlpha=-1;
        if(fault==14)f.after.dimension=1;if(fault==15)f.after.subClient=1;
        if(fault==16)f.currentFrame=92;if(fault==17)f.panorama=true;
        auto const original=bytes(f.view);bool sunny=true;WeatherDecision observation{};
        bool const applied=applyClearWeatherView(f.view,f.capture,f.fog,f.id,91,requested,[&]{
            check(bytes(f.view)==original,"Clear failures precede first store");
            if(fault==18)return false;if(fault==19)throw std::runtime_error("owner read exception");
            if(fault==20)publishMode(1);if(fault==21)publishMode(0,14);
            if(fault==22)publishMode(0,13.5f,false,false);if(fault==23)worldGeneration=18;
            if(fault==24)weatherNativeAdmission=false;if(fault==25)runtimeReady=false;
            return f.revalidate();
        },&observation,&sunny);
        check(!applied && !sunny && observation!=WeatherDecision::Applied && bytes(f.view)==original,
            "Clear owner/lifecycle/OFF/invalid/weather/mode/exception rejection preserves complete native output");
        ++failureCases;
    }
    for(unsigned cycle=0;cycle<24;++cycle) {
        ready();worldGeneration=17+cycle;visualWeatherContext={true,true,17+cycle};
        Fixture current;current.id.generation=17+cycle;current.fog.valid=false;
        publishMode(0);bool sunny{};
        check(applyClearWeatherView(current.view,current.capture,current.fog,current.id,91,
            settings()->weatherOptions,[]{return true;},nullptr,&sunny) && !sunny,"Clear admits each current world without prior fog record");
        publishMode(0,13.5f,false,false);Fixture fresh;fresh.view.mWeatherState.value.mDensityRain=.02f*cycle;
        fresh.view.mWeatherState.value.mDensitySnow=.03f*cycle;fresh.view.mViewData.value.mFakeHDR=.01f*cycle;
        auto const native=bytes(fresh.view);
        check(!applyClearWeatherView(fresh.view,fresh.capture,fresh.fog,current.id,91,
            settings()->weatherOptions,[]{return true;}) && bytes(fresh.view)==native,
            "Clear OFF returns current native weather/HDR after world changes without restoring old view");
    }
}
void weatherSkyModes() {
    unsigned matrix{};
    for(unsigned mode:{0u,1u,2u})for(bool enabled:{false,true})for(bool fog:{false,true})
    for(float hdr:{0.f,.7f})for(bool noRender:{false,true})for(bool time:{false,true})for(bool full:{false,true}) {
        ready();publishMode(mode,13.5f,noRender,enabled);
        auto state=std::make_shared<Settings>(*settings());state->enabled[unsigned(Feature::TimeChanger)]=time;
        state->enabled[unsigned(Feature::Fullbright)]=full;publishedSettings.store(state);
        Fixture f;nativeHdr(f,hdr,fog);f.capture.requested=f.fog.requested=state->weatherOptions;
        f.fog.sunny=mode==0?.2f:0.f;
        auto const original=bytes(f.view);auto expected=f.view;
        if(enabled) {
            expectPrecipitation(expected,mode,noRender,13.5f);
            expected.mSkyState.value.mFogLevel=mode==0?0.f:1.f;
            expected.mViewData.value.mSkyColor.value={.55f,.7f,.9f,1};
            expected.mViewData.value.mSkyZenithColor.value={.4f,.5f,.6f,1};
            expected.mViewData.value.mSkyHorizonColor.value={.7f,.8f,.9f,1};
            expected.mSkyState.value.mSkyDarken=.9f;
            float const targetHdr=fog?f.fog.sunny:hdr;
            auto& color=expected.mCloudState.value.mCloudColor.value;
            // Native fixture blend: cloud(.9,.8,.7), sunrise(.8,.3,.1,.5),
            // amount=.175. Independent expected scalar values, alpha retained.
            color.r=.8825f-targetHdr*.2f;color.g=.7125f-targetHdr*.2f;color.b=.595f-targetHdr*.2f;
            expected.mViewData.value.mFakeHDR=targetHdr;
        }
        bool appliedSky=true;WeatherDecision decision{};
        bool const applied=applyWeatherSkyView(f.view,f.capture,f.fog,f.id,91,state->weatherOptions,[&]{
            check(bytes(f.view)==original,"whole-weather plan performs no stores before final owner/frame admission");return f.revalidate();
        },&decision,&appliedSky);
        check(applied==enabled && appliedSky==(enabled && fog),"all modes admit proven components; full skyApplied requires matching HDR observation including fresh HDR0 skip record");
        if(enabled) {
            auto const& c=f.view.mCloudState.value.mCloudColor.value;auto const& ec=expected.mCloudState.value.mCloudColor.value;
            check(std::abs(c.r-ec.r)<.000001f && std::abs(c.g-ec.g)<.000001f && std::abs(c.b-ec.b)<.000001f,
                "native sunrise/HDR arithmetic gives expected target cloud RGB");
            expected.mCloudState.value.mCloudColor.value=c; // Already checked floats; compare remaining bytes exactly.
        }
        check(bytes(f.view)==bytes(expected),"whole-weather mode modifies only proven weather/sky/light fields; resources and all opaque bytes preserved");
        check(settings()==state && f.revalidations==(enabled?1u:0u),"NoRender priority/Time/Fullbright/settings retained");
        ++matrix;
    }
    check(matrix==192,"whole-weather 192 case matrix executed independently of legacy precipitation matrix");
    // Native configured-color cloud branch deliberately has no sunrise blend,
    // even though SDK ViewData still contains the separately produced sunrise.
    ready();publishMode(0);Fixture palette;nativeHdr(palette,0,false);
    palette.capture.requested=palette.fog.requested=settings()->weatherOptions;palette.fog.valid=true;palette.fog.sunny=0.f;
    palette.view.mCloudState.value.mCloudColor.value={.5f,.4f,.3f,.7f};bool complete{};
    check(applyWeatherSkyView(palette.view,palette.capture,palette.fog,palette.id,91,settings()->weatherOptions,
        [&]{return palette.revalidate();},nullptr,&complete) && complete,"exact configured cloud skip-sunrise branch correlates");
    check(palette.view.mCloudState.value.mCloudColor.value==Color{.9f,.8f,.7f,.7f},"cloud replacement uses the matched native branch");
    ready();publishMode(0);Fixture ulp;nativeHdr(ulp,0,false);ulp.capture.requested=settings()->weatherOptions;
    auto& ulpColor=ulp.view.mCloudState.value.mCloudColor.value;
    ulpColor.r=std::nextafter(ulpColor.r,1.f);auto const ulpNative=ulpColor;bool ulpComplete=true;
    check(applyWeatherSkyView(ulp.view,ulp.capture,ulp.fog,ulp.id,91,settings()->weatherOptions,[]{return true;},nullptr,&ulpComplete)
        && !ulpComplete && ulp.view.mCloudState.value.mCloudColor.value==ulpNative,
        "one ULP cloud mismatch remains native; scalar guard is never relaxed");
    ready();publishMode(0);Fixture ambiguous;ambiguous.capture.requested=settings()->weatherOptions;
    ambiguous.view.mViewData.value.mFakeHDR=1.f;ambiguous.fog.native=1.f;ambiguous.fog.sunny=0.f;
    ambiguous.view.mViewData.value.mSunriseColor.value={0,0,0,.5f};
    ambiguous.capture.cloud.native={.1f,.1f,.1f,1};ambiguous.capture.cloud.sunny={.9f,.9f,.9f,1};
    ambiguous.view.mCloudState.value.mCloudColor.value={0,0,0,.7f};bool ambiguousComplete=true;
    check(applyWeatherSkyView(ambiguous.view,ambiguous.capture,ambiguous.fog,ambiguous.id,91,settings()->weatherOptions,
        []{return true;},nullptr,&ambiguousComplete) && !ambiguousComplete
        && ambiguous.view.mCloudState.value.mCloudColor.value==Color{0,0,0,.7f}
        && ambiguous.view.mViewData.value.mFakeHDR==1.f,
        "ambiguous clamped cloud branches preserve native cloud/HDR instead of guessing the blend");
    for(unsigned fault=0;fault<10;++fault) {
        ready();publishMode(0);Fixture f;nativeHdr(f,0,false);f.capture.requested=settings()->weatherOptions;
        if(fault==0)f.fog.valid=false;
        if(fault==1)f.fog.frame=90;
        if(fault==2)f.fog.requested.mode=1;
        if(fault==3)f.capture.cloudSeen=false;
        if(fault==4)f.view.mCloudState.value.mCloudColor.value.r=.123f;
        if(fault==5)f.capture.colorsUsed=0;
        if(fault==6)f.capture.colorsOverflow=true;
        if(fault==7)f.capture.skySeen=false;
        if(fault==8)f.view.mSkyState.value.mSkyDarken=.123f;
        if(fault==9)f.capture.colors[3]={f.capture.colors[0].native,{1,1,1,1}},f.capture.colorsUsed=4;
        auto const native=f.view;bool complete=true;
        check(applyWeatherSkyView(f.view,f.capture,f.fog,f.id,91,settings()->weatherOptions,[&]{return f.revalidate();},nullptr,&complete),
            "uncorrelated component does not discard other independently proven weather components");
        if(fault==3 || fault==4)check(f.view.mCloudState.value.mCloudColor.value==native.mCloudState.value.mCloudColor.value && !complete,
            "unknown cloud unchanged; no arbitrary RGB or relaxed matching");
        if(fault==5 || fault==6 || fault==9)check(f.view.mViewData.value.mSkyColor.value==native.mViewData.value.mSkyColor.value && !complete,
            "missing/ambiguous sky association leaves native sky color");
        if(fault==7 || fault==8)check(f.view.mSkyState.value.mSkyDarken==native.mSkyState.value.mSkyDarken && !complete,
            "uncorrelated sky brightness unchanged");
        if(fault<3)check(!complete && f.view.mViewData.value.mFakeHDR==0.f && f.view.mSkyState.value.mSkyDarken==.9f,
            "stale/missing HDR cannot block proven sky/cloud/brightness but never reports full HDR correlation");
    }
    for(unsigned mode:{0u,1u,2u})for(unsigned fault=0;fault<19;++fault) {
        ready();publishMode(mode);Fixture f;auto const request=settings()->weatherOptions;f.capture.requested=f.fog.requested=request;
        if(fault==0)publishMode(mode,13.5f,false,false);if(fault==1)runtimeReady=false;if(fault==2)weatherNativeAdmission=false;
        if(fault==3)worldGeneration=18;if(fault==4)visualWeatherContext.generation=18;
        if(fault==5)visualWeatherContext.eligible=false;if(fault==6)visualWeatherContext.hasWeather=false;
        if(fault==7)f.view.mViewData.value.mIsShadowPass=true;if(fault==8)f.view.mViewData.value.mCameraUnderLiquid=true;
        if(fault==9)f.after.dimension=1;if(fault==10)f.after.subClient=1;if(fault==11)f.currentFrame=92;if(fault==12)f.panorama=true;
        auto const original=bytes(f.view);bool complete=true;
        bool const applied=applyWeatherSkyView(f.view,f.capture,f.fog,f.id,91,request,[&]{
            check(bytes(f.view)==original,"whole-weather failure precedes every output store");
            if(fault==13)publishMode((mode+1)%3);if(fault==14)publishMode(mode,14);
            if(fault==15)publishMode(mode,13.5f,false,false);if(fault==16)worldGeneration=18;
            if(fault==17)return false;if(fault==18)throw std::runtime_error("owner read failed");return f.revalidate();
        },nullptr,&complete);
        check(!applied && !complete && bytes(f.view)==original,"all modes reject lifecycle/OFF/owner/frame/panorama/mode changes atomically");
        ++failureCases;
    }
    for(unsigned cycle=0;cycle<24;++cycle) {
        ready();worldGeneration=17+cycle;visualWeatherContext={true,true,17+cycle};
        for(unsigned mode:{0u,1u,2u}) {
            publishMode(mode);Fixture f;f.id.generation=17+cycle;f.capture.requested=f.fog.requested=settings()->weatherOptions;
            check(applyWeatherSkyView(f.view,f.capture,f.fog,f.id,91,settings()->weatherOptions,[]{return true;}),"fresh current generation admits selected mode");
            publishMode(mode,13.5f,false,false);Fixture current;current.view.mSkyState.value.mSkyDarken=.2f+.01f*cycle;
            current.view.mCloudState.value.mCloudColor.value={.01f*cycle,.02f*cycle,.03f*cycle,1};auto const native=bytes(current.view);
            check(!applyWeatherSkyView(current.view,current.capture,current.fog,f.id,91,settings()->weatherOptions,[]{return true;})
                && bytes(current.view)==native,"OFF returns current sky/cloud/brightness after each world/dimension generation, never an ON snapshot");
        }
    }
}
void traceBudget() {
    using Clock=WeatherResultTrial::Clock;
    Clock::time_point const now{};
    WeatherResultTrial trial;WeatherProducerResult p;p.key.requested={0,13.5f};p.frame=91;
    p.native.rain=.8f;p.owned.rain=0;p.applied=true;p.skyApplied=false;
    p.resources={true,true,true,false,17,12,.3f,.7f,.4f,.9f};
    check(!trial.accepting() && !trial.pending(),"trial is default OFF and does not start with Weather enabled");
    trial.producer(p,now);check(!trial.snapshot(now).producerSeen,"inactive capture is ignored");
    check(trial.start(now) && !trial.start(now),"one explicit trial starts; overlapping/replaced sessions rejected");
    auto id=trial.claimWriter();check(id==1 && !trial.claimWriter(),"one owned writer per session");
    trial.producer(p,now);p.native.rain=.2f;p.owned.rain=.4f;p.applied=false;p.skyApplied=true;
    trial.producer(p,now);auto latest=trial.snapshot(now);
    check(latest.producerCalls==2 && latest.producer.native.rain==.2f && latest.producer.owned.rain==.4f
        && !latest.producer.applied && latest.producer.skyApplied,"same request key still records changed native/owned/result");
    WeatherSkyInputResult sky{p.key,p.frame,true,p.owned,p.resources};trial.skyInput(sky,now);
    sky.values.rain=.7f;trial.skyInput(sky,now);
    sky.frame=92;trial.skyInput(sky,now);sky.frame=91;sky.key.generation=1;trial.skyInput(sky,now);
    latest=trial.snapshot(now);
    check(latest.sameFrameScalarMatches==1 && latest.sameFrameScalarDifferences==1 && latest.skyInputCalls==4,
        "scalar content comparison is confined to same owner/request/camera frame, not GPU success");
    auto line=formatWeatherTrialReport(latest,"latest");
    check(line.find("skyApplied=1")!=std::string::npos && line.find("weatherFinalDraw=UNVERIFIED")!=std::string::npos
        && line.find("resourcePresence(mesh,textureClient,rainMat,snowMat)=(1,1,1,0)")!=std::string::npos,
        "latest report names observed stages/resource presence and leaves final weather draw unverified");
    latest=trial.snapshot(now+weatherTrialDuration);
    check(!trial.accepting() && latest.end==WeatherTrialEnd::Deadline && latest.pending,"deadline is explicit even without any future frame");
    trial.producer({},now);check(trial.snapshot(now).producerCalls==2,"closed session does not silently accept later result");
    unsigned writes{};runWeatherTrialReporter(trial,id,[&](auto const& record,char const* kind){
        ++writes;check(record.end==WeatherTrialEnd::Deadline && std::string(kind)=="end","expired trial writes final reason and latest state");return true;
    },[]{},[&]{return now;});
    check(writes==1 && !trial.pending() && trial.snapshot(now).lastFlushSucceeded,"final record acknowledged and retained for live UI");
    for(auto reason:{WeatherTrialEnd::UserStopped,WeatherTrialEnd::RuntimeEnded}) {
        check(trial.start(now),"explicit restart is allowed after completed writer");auto session=trial.claimWriter();
        trial.stop(reason);runWeatherTrialReporter(trial,session,[](auto const&,char const*){return true;},[]{},[&]{return now;});
        check(trial.snapshot(now).end==reason && !trial.pending(),"stop/runtime reasons remain visible after end");
    }
    check(trial.start(now),"budget trial explicitly starts");auto cap=trial.claimWriter();writes=0;
    runWeatherTrialReporter(trial,cap,[&](auto const& record,char const* kind){
        ++writes;if(std::string(kind)=="end")check(record.end==WeatherTrialEnd::ReportLimit,"reserved final record reports cap exhaustion");return true;
    },[]{},[&]{return now;});
    check(writes==32 && trial.snapshot(now).flushedReports==32 && trial.snapshot(now).end==WeatherTrialEnd::ReportLimit,
        "bounded journal includes its end record; no invisible sixteen-key cutoff");
    for(bool throws:{false,true}) {
        check(trial.start(now),"writer-failure trial starts");auto failure=trial.claimWriter();
        runWeatherTrialReporter(trial,failure,[&](auto const&,char const*){if(throws)throw std::runtime_error("sink failure");return false;},[]{},[&]{return now;});
        auto status=trial.snapshot(now);
        check(status.end==WeatherTrialEnd::WriterError && !status.lastFlushSucceeded && !status.pending && !trial.accepting(),
            "write/flush failure or exception cannot claim readability; UI retains explicit failure");
    }
    // Real worker + real flushed file, read concurrently while the worker is
    // still alive. Test intervals are20ms; product worker uses one second.
    auto live=std::make_shared<WeatherResultTrial>();check(live->start(),"file trial explicitly starts");auto fileSession=live->claimWriter();
    auto const path=std::filesystem::current_path()/"weather-trial-fixture.log";
    std::mutex signal;std::condition_variable wake;std::atomic<unsigned> records{};std::atomic_bool finished{};
    std::thread worker([live,fileSession,path,&signal,&wake,&records,&finished]{
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        runWeatherTrialReporter(*live,fileSession,[&](auto const& snapshot,char const* kind){
            output<<formatWeatherTrialReport(snapshot,kind)<<'\n';output.flush();bool ok=output.good();
            if(ok){++records;wake.notify_all();}return ok;
        },[&]{std::unique_lock lock(signal);wake.wait_for(lock,std::chrono::milliseconds(20));},[]{return Clock::now();});
        finished=true;wake.notify_all();
    });
    // Always join on assertion/reader failure as well as success.
    auto finish=[&]{live->stop(WeatherTrialEnd::UserStopped);wake.notify_all();if(worker.joinable())worker.join();};
    try {
        auto waitRecords=[&](unsigned n){std::unique_lock lock(signal);return wake.wait_for(lock,std::chrono::seconds(2),[&]{return records>=n || finished.load();}) && records>=n;};
        check(waitRecords(1) && !finished,"first flushed record is readable while reporter remains active");
        p.key.requested.mode=1;p.native.rain=.17f;p.owned.rain=13.5f;p.applied=true;p.skyApplied=false;
        live->producer(p);auto before=records.load();check(waitRecords(before+2),"periodic latest report continues independent of key changes");
        auto readLive=[&]{std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
        auto text=readLive();check(text.find("mode=1")!=std::string::npos && text.find("skyApplied=0")!=std::string::npos
            && text.find("owned(draw,rain,snow,rainAlpha,snowAlpha,fog,hdr,sky)=(0,13.5")!=std::string::npos && !finished,
            "concurrent live file reader observes latest mode/native/owned/sky status without shutdown");
        p.owned.rain=9.f;p.applied=false;p.skyApplied=true;live->producer(p);before=records.load();check(waitRecords(before+2),"same-key changed result is flushed periodically");
        text=readLive();check(text.find("weatherApplied=0 skyApplied=1")!=std::string::npos
            && text.find("owned(draw,rain,snow,rainAlpha,snowAlpha,fog,hdr,sky)=(0,9,")!=std::string::npos && !finished,
            "same-key outcome/density change remains readable during runtime");
        finish();text=readLive();check(text.find("kind=end end=user-stopped")!=std::string::npos && live->snapshot().lastFlushSucceeded,
            "real file ends with explicit stop reason and last observed result");
    } catch(...) {finish();throw;}
    // Worker retains its scalar state after the UI owner drops its reference.
    auto owned=std::make_shared<WeatherResultTrial>();owned->start(now);auto ownerSession=owned->claimWriter();std::weak_ptr<WeatherResultTrial> weak=owned;
    std::thread retained([held=owned,ownerSession,now]{held->stop(WeatherTrialEnd::RuntimeEnded);
        runWeatherTrialReporter(*held,ownerSession,[](auto const&,char const*){return true;},[]{},[&]{return now;});});
    owned.reset();retained.join();check(weak.expired(),"writer owns scalar mailbox through end; no borrowed render/UI lifetime retained");
}

}
int main() {
    try { failures();success();precipitationModes();clearIndependentOfSky();weatherSkyModes();traceBudget();std::cout<<"WeatherSunnyTransactionTests PASS "<<checks<<" checks; "<<failureCases
        <<" complete-native-output failure cases; runtime visuals NOT_RUN\n";return 0; }
    catch(std::exception const& e){std::cerr<<"WeatherSunnyTransactionTests FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}
}
