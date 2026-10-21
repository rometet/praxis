#include "core/WeatherSunnyPolicy.h"
#include "core/WeatherOwnedResultPolicy.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool ok, char const* message) { ++checks; if (!ok) throw std::runtime_error(message); }
bool close(float a,float b) { return std::abs(a-b)<0.000001f; }
void publish(bool enabled) {
    auto s=std::make_shared<Settings>();s->enabled[static_cast<unsigned>(Feature::WeatherChanger)]=enabled;
    publishedSettings.store(s);
}
void ready() { runtimeReady=true;weatherNativeAdmission=true;worldGeneration=17;publish(true); }
struct Color { float r{},g{},b{},a{1}; friend bool operator==(Color const&,Color const&)=default; };
template<class T> struct Storage { T value{};T& get(){return value;} T const& get() const{return value;} friend bool operator==(Storage const&,Storage const&)=default; };
using Parameters=WeatherSunnyParameters<Color>;
static_assert(sizeof(Parameters)==108 && alignof(Parameters)==4);
static_assert(std::is_trivially_copyable_v<Parameters>);
struct Data {
    Storage<Color> mSkyColor,mSkyZenithColor,mSkyHorizonColor,mSunriseColor;
    Color fog{.1f,.2f,.3f,1};
    float mFakeHDR=.7f,celestial=.8f;
    bool mIsShadowPass{},mCameraUnderLiquid{};
    friend bool operator==(Data const&,Data const&)=default;
};
struct Sky { float mSkyDarken=.55f,geometry=42;friend bool operator==(Sky const&,Sky const&)=default; };
struct Cloud { Storage<Color> mCloudColor;float mesh=77;friend bool operator==(Cloud const&,Cloud const&)=default; };
struct View { Storage<Data> mViewData;Storage<Sky> mSkyState;Storage<Cloud> mCloudState;friend bool operator==(View const&,View const&)=default; };
struct Light { float mSkyDarken=.55f;int mSkyFlashTime=7;
    float gamma=12,nightVision=.6f,underwater=.3f,darkness=.2f;friend bool operator==(Light const&,Light const&)=default; };

void parameters() {
    Parameters native{};
    native.rainSkyWeight=.7f;native.untouched4=13;native.untouched8=-9;
    native.rainSkyColor={.11f,.12f,.13f,.14f};native.rainCloudColor={.21f,.22f,.23f,.24f};
    native.rainCloudWeight=.8f;native.thunderSkyColor={.31f,.32f,.33f,.34f};native.thunderSkyWeight=.9f;
    native.thunderCloudColor={.41f,.42f,.43f,.44f};native.thunderCloudWeight=.6f;
    native.flashSkyColor={.51f,.52f,.53f,.54f};native.flashSkyWeight=.4f;
    auto const before=native;
    for(bool wither:{false,true}) {
        auto const owned=sunnyParameters(native,wither);
        check(owned.rainSkyWeight==0 && owned.rainCloudWeight==0 && owned.flashSkyWeight==0,"owned rain/flash coefficients clear");
        check(owned.thunderSkyWeight==(wither?.9f:0) && owned.thunderCloudWeight==(wither?.6f:0),"native Wither coefficients survive");
        check(owned.untouched4==13 && owned.untouched8==-9 && owned.rainSkyColor==native.rainSkyColor
            && owned.rainCloudColor==native.rainCloudColor && owned.thunderSkyColor==native.thunderSkyColor
            && owned.thunderCloudColor==native.thunderCloudColor && owned.flashSkyColor==native.flashSkyColor,
            "resource palette and opaque non-weather scalars survive");
    }
    check(std::memcmp(&native,&before,sizeof native)==0,"borrowed input never changes");
}
void brightness() {
    // Independent native formula fixtures span day, dusk, night and Wither.
    for(float daylight:{0.f,.1f,.5f,1.f}) for(float wither:{1.f,.6875f})
        for(float fog:{0.f,.25f,1.f}) for(float lightning:{0.f,.5f,1.f}) {
            float const native=.8f*daylight*wither*(1.f-.3125f*fog)*(1.f-.3125f*lightning)+.2f;
            auto clear=sunnySkyBrightness(native,fog,lightning);
            for(unsigned mode:{0u,1u,2u}) {
                WeatherOptions requested{mode,13.5f};
                auto target=weatherSkyBrightness(native,fog,lightning,requested);
                check(target && close(*target,.8f*daylight*wither*(mode==0?1.f:.6875f)+.2f),
                    "selected sky weather retains native time and Wither while replacing only weather factors");
            }
            check(clear && close(*clear,.8f*daylight*wither+.2f),"weather removed while native clock/Wither remains");
        }
    for(float invalid:{-1.f,2.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        check(!sunnySkyBrightness(.5f,invalid,0) && !sunnySkyBrightness(.5f,0,invalid),"invalid weather rejects");
    }
    check(!sunnySkyBrightness(std::numeric_limits<float>::quiet_NaN(),0,0),"invalid native brightness rejects");
}
void scopes() {
    ready();SunnyCapture outer,inner;SunnyIdentity const id{17,0,0};
    check(sunnyContext.ownedCapture==nullptr,"starts outside scope");
    {
        SunnyScope scope(SunnyStage::View,id,outer,true);
        check(sunnyContext.ownedCapture==&outer && sunnyContext.identity==id,"accepted owned scope");
        {SunnyScope nested(SunnyStage::View,id,inner,false);check(!sunnyContext.ownedCapture,"rejected capture masks outer");}
        check(sunnyContext.ownedCapture==&outer,"nested rejection restores outer");
        try {SunnyScope nested(SunnyStage::Light,id,inner,true);throw 1;}catch(int){}
        check(sunnyContext.ownedCapture==&outer && sunnyContext.stage==SunnyStage::View,"exception restores scope");
        bool isolated=false;
        std::thread worker([&]{isolated=!sunnyContext.ownedCapture;SunnyScope nested(SunnyStage::PreRender,id,inner,true);
            isolated=isolated && sunnyContext.ownedCapture==&inner;});worker.join();
        check(isolated && sunnyContext.ownedCapture==&outer,"thread scopes isolated");
    }
    check(!sunnyContext.ownedCapture,"stack-owned observer leaves no pointer");
    for(unsigned fault=0;fault<5;++fault) {
        ready();if(fault==0)publish(false);if(fault==1)weatherNativeAdmission=false;
        if(fault==2)runtimeReady=false;if(fault==3)worldGeneration=18;
        SunnyScope scope(SunnyStage::View,id,outer,fault!=4);
        check(!sunnyContext.ownedCapture,"OFF/stop/world/capture rejects admission");
    }
    ready();
}
struct Fixture {
    View view;SunnyCapture capture;SunnyIdentity identity{17,0,0};
    SunnyFogRecord fog{identity,91,.7f,.2f,true};
    Fixture() {
        auto& data=view.mViewData.value;
        data.mSkyColor.value={.35f,.45f,.6f,1};data.mSkyZenithColor.value={.2f,.3f,.4f,1};
        data.mSkyHorizonColor.value={.5f,.6f,.7f,1};data.mSunriseColor.value={.8f,.3f,.1f,.5f};
        capture.colors[0]={{.35f,.45f,.6f,1},{.55f,.7f,.9f,1}};
        capture.colors[1]={{.2f,.3f,.4f,1},{.4f,.5f,.6f,1}};
        capture.colors[2]={{.5f,.6f,.7f,1},{.7f,.8f,.9f,1}};capture.colorsUsed=3;
        capture.cloud={{.5f,.4f,.3f,.7f},{.9f,.8f,.7f,.7f}};capture.cloudSeen=true;
        capture.skyNative=.55f;capture.skySunny=.9f;capture.skySeen=true;
        // IEEE-754 results of the pinned native SSE operation sequence at
        // 46afc03..46afcdf, independently rounded after each MUL/ADD/SUB.
        view.mCloudState.value.mCloudColor.value={std::bit_cast<float>(0x3ed33334u),
            std::bit_cast<float>(0x3e7851edu),std::bit_cast<float>(0x3e000001u),.7f};
    }
};
void ownedResults() {
    ready();Fixture f;auto const before=f.view;
    check(applySunnyView(f.view,f.capture,f.fog,f.identity,91),"complete native correspondence accepts owned result");
    auto const& data=f.view.mViewData.value;
    check(data.mSkyColor.value==Color{.55f,.7f,.9f,1} && data.mSkyZenithColor.value==Color{.4f,.5f,.6f,1}
        && data.mSkyHorizonColor.value==Color{.7f,.8f,.9f,1},"native palette alternatives map to sky/zenith/horizon");
    auto const& cloud=f.view.mCloudState.value;
    check(close(cloud.mCloudColor.value.r,.8425f) && close(cloud.mCloudColor.value.g,.6725f)
        && close(cloud.mCloudColor.value.b,.555f),"sunrise and native HDR blend recomputed for clear cloud");
    check(cloud.mCloudColor.value.a==.7f && cloud.mesh==77 && f.view.mSkyState.value.geometry==42
        && data.fog==before.mViewData.value.fog && data.celestial==.8f
        && data.mSunriseColor.value==before.mViewData.value.mSunriseColor.value,
        "alpha/mesh/fog/celestial/sunrise preserved");
    check(data.mFakeHDR==.2f && f.view.mSkyState.value.mSkyDarken==.9f,"only owned sunny lighting changes");
    for(unsigned fault=0;fault<17;++fault) {
        ready();Fixture bad;
        if(fault==0)publish(false);if(fault==1)weatherNativeAdmission=false;if(fault==2)runtimeReady=false;
        if(fault==3)worldGeneration=18;if(fault==4)bad.fog.identity.generation=18;
        if(fault==5)bad.fog.identity.dimension=1;if(fault==6)bad.fog.identity.subClient=1;
        if(fault==7)bad.fog.frame=90;if(fault==8)bad.fog.valid=false;
        if(fault==9)bad.view.mViewData.value.mIsShadowPass=true;if(fault==10)bad.view.mViewData.value.mCameraUnderLiquid=true;
        if(fault==11)bad.view.mViewData.value.mFakeHDR=.6f;if(fault==12)bad.view.mSkyState.value.mSkyDarken=.6f;
        if(fault==13)bad.capture.skySeen=false;if(fault==14)bad.capture.cloudSeen=false;
        if(fault==15)bad.view.mCloudState.value.mCloudColor.value.r=.9f;
        if(fault==16)bad.capture.colorsOverflow=true;
        auto const original=bad.view;
        check(!applySunnyView(bad.view,bad.capture,bad.fog,bad.identity,91),"unproven output rejected");
        check(bad.view==original,"rejection causes no partial sky/cloud mutation");
    }
    ready();Fixture ambiguous;
    ambiguous.capture.colors[3]={ambiguous.capture.colors[0].native,{.9f,.9f,.9f,1}};
    ambiguous.capture.colorsUsed=4;auto const original=ambiguous.view;
    check(!applySunnyView(ambiguous.view,ambiguous.capture,ambiguous.fog,ambiguous.identity,91)
        && ambiguous.view==original,"ambiguous native palette rejects atomically");
    ambiguous.capture.colors[3]=ambiguous.capture.colors[0];
    check(applySunnyView(ambiguous.view,ambiguous.capture,ambiguous.fog,ambiguous.identity,91),"equal native and clear pairs unambiguous");
    ready();Fixture unmapped;unmapped.view.mViewData.value.mSkyZenithColor.value={.11f,.22f,.33f,1};
    check(applySunnyView(unmapped.view,unmapped.capture,unmapped.fog,unmapped.identity,91)
        && unmapped.view.mViewData.value.mSkyZenithColor.value==Color{.11f,.22f,.33f,1},"independent resource zenith remains native");
    ready();Fixture returning;publish(false);
    check(!applySunnyView(returning.view,returning.capture,returning.fog,returning.identity,91)
        && returning.view.mViewData.value.mSkyColor.value==Color{.35f,.45f,.6f,1},"OFF passes current native weather without an ON snapshot");
}
void lightAndCoexistence() {
    ready();Fixture f;Light light;
    check(applySunnyLight(light,f.capture,17),"owned light accepts corresponding brightness");
    check(light.mSkyDarken==.9f && light.mSkyFlashTime==0 && light.gamma==12 && light.nightVision==.6f
        && light.underwater==.3f && light.darkness==.2f,"Fullbright gamma and unrelated native effects preserved");
    for(unsigned fault=0;fault<6;++fault) {
        ready();Fixture bad;Light native;
        if(fault==0)publish(false);if(fault==1)weatherNativeAdmission=false;if(fault==2)runtimeReady=false;
        if(fault==3)worldGeneration=18;if(fault==4)bad.capture.skySeen=false;if(fault==5)native.mSkyDarken=.6f;
        auto const before=native;
        check(!applySunnyLight(native,bad.capture,17) && native==before,"OFF/stop/world/uncorrelated light passes through");
    }
    ready();auto s=std::make_shared<Settings>();s->enabled[static_cast<unsigned>(Feature::WeatherChanger)]=true;
    s->enabled[static_cast<unsigned>(Feature::TimeChanger)]=true;s->enabled[static_cast<unsigned>(Feature::Fullbright)]=true;
    s->renderMask=passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Particles);
    publishedSettings.store(s);Fixture combined;
    check(applySunnyView(combined.view,combined.capture,combined.fog,combined.identity,91)
        && combined.view.mViewData.value.celestial==.8f && s->renderMask==(passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Particles)),
        "NoRender settings and native celestial input coexist without registry mutation");
    for(unsigned fault=0;fault<4;++fault) {
        ready();Fixture owned;WeatherOwnerSnapshot before{17,0,0,true,true},after=before;
        if(fault==0)after.dimension=1;if(fault==1)after.subClient=1;
        if(fault==2)after.eligible=false;if(fault==3)after.hasWeather=false;
        unsigned writes=0;
        check(!withOwnedWeatherResult(before,[&]{return after;},[]{return worldGeneration.load();},[&](auto const&){
            writes+=applySunnyView(owned.view,owned.capture,owned.fog,owned.identity,91);}) && writes==0,
            "SDK owner/dimension/subclient/weather revalidation prevents stale mutation");
    }
}
}
int main() {
    try { parameters();brightness();scopes();ownedResults();lightAndCoexistence();
        std::cout<<"WeatherSunnyTests PASS "<<checks<<" checks; runtime visuals NOT_RUN\n";return 0;
    }catch(std::exception const& e){std::cerr<<"WeatherSunnyTests FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}
}
