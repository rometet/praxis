#include "platform/WeatherChanger.h"
#include "platform/TimeChanger.h"
#include "core/WeatherChangerPolicy.h"
#include "core/WeatherOwnedResultPolicy.h"
#include "core/WeatherRenderPolicy.h"
#include "core/WeatherChangerDiagnostics.h"
#include "core/WeatherSkySymbolEvidence.h"
#include "core/WeatherSunnyPolicy.h"
#include "core/WeatherResultTracePolicy.h"
#include "core/WeatherSunnyNativeContract.h"
#include "core/WeatherSunnyImagePolicy.h"
#include "core/WeatherSunnyIsolationPolicy.h"
#include "core/WeatherSunnyStartupVerifier.h"
#include "ll/api/memory/Symbol.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/objects/SkyRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/WeatherRenderObject.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/Weather.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include <array>
#include <optional>
#include <sstream>
#include <type_traits>
#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <mutex>
#include <fstream>
#include <thread>

namespace praxis::companion {
namespace {
using WeatherMove = void* (WeatherRenderObject::*)(WeatherRenderObject&&);
using SkyMove = void* (SkyRenderObject::*)(SkyRenderObject&&);
using CreateView = ViewRenderObject (LevelRendererPlayer::*)(ScreenContext&, SubClientId);
ll::memory::FuncPtr sdkViewTarget{};
CreateView sdkViewContinuation{};
std::atomic<std::uintptr_t> sdkViewSavedTarget{};
std::atomic_uint64_t sdkViewEpoch{1};
std::atomic_bool sdkViewClosed{};
bool prepareSdkViewBinding(std::uintptr_t candidate);
SunnySdkViewBindingRecord sdkViewBindingEvidence() noexcept;
using RainUpdate = LevelRendererCamera::RainState (LevelRendererCamera::*)();
using NativeSunnyParameters = WeatherSunnyParameters<mce::Color>;
static_assert(sizeof(NativeSunnyParameters) == 108 && alignof(NativeSunnyParameters) == 4);
static_assert(std::is_trivially_copyable_v<NativeSunnyParameters>);
static_assert(offsetof(NativeSunnyParameters, rainCloudWeight) == 0x2c);
static_assert(offsetof(NativeSunnyParameters, thunderSkyWeight) == 0x40);
static_assert(offsetof(NativeSunnyParameters, thunderCloudWeight) == 0x54);
static_assert(offsetof(NativeSunnyParameters, flashSkyWeight) == 0x68);
static_assert(std::is_same_v<decltype(&WeatherRenderObject::$ctor), WeatherMove>);
static_assert(std::is_same_v<decltype(&SkyRenderObject::$ctor), SkyMove>);
static_assert(std::is_same_v<decltype(&LevelRendererPlayer::createViewRenderObject), CreateView>);
static_assert(std::is_same_v<decltype(&LevelRendererCamera::doRainUpdate), RainUpdate>);
static_assert(std::is_same_v<decltype(std::declval<ViewRenderObject&>().mWeatherState.get()), WeatherRenderObject&>);
static_assert(static_cast<int>(WeatherRenderObject::PrecipitationType::Rain) == 0);
static_assert(static_cast<int>(WeatherRenderObject::PrecipitationType::Snow) == 1);

struct WeatherRendererEligibility : WeatherOwnerSnapshot {
    bool playing{};
    WeatherContextResult context = WeatherContextResult::Accepted;
};
WeatherRendererEligibility weatherRendererEligibility(LevelRendererCamera const& camera,
    std::optional<SubClientId> subClient = std::nullopt, bool observe = false) {
    WeatherRendererEligibility result;
    result.generation = worldGeneration.load(std::memory_order_acquire);
    auto client = ll::service::getClientInstance();
    bool const primaryClient = client && static_cast<IClientInstance*>(&*client) == &camera.mLevelRenderer.mClientInstance;
    bool const matchingSubClient = primaryClient && (!subClient || client->getClientSubId() == *subClient);
    bool primary = matchingSubClient;
    bool playing = primary && client->getClientInstanceState() == ClientInstanceState::Playing;
    auto* player = playing ? client->getLocalPlayer() : nullptr;
    auto* localRenderer = playing ? client->getLevelRenderer() : nullptr;
    auto* localCamera = localRenderer && localRenderer->mLevelRendererPlayer
        ? localRenderer->mLevelRendererPlayer.get() : nullptr;
    primary = primary && localRenderer == &camera.mLevelRenderer && localCamera == &camera;
    auto* dimension = player ? &player->getDimension() : nullptr;
    bool sameDimension = primary && dimension && localCamera->mDimension == dimension;
    result.eligible = primary && playing && sameDimension && !camera.mIsCameraInCaptureMode;
    result.hasWeather = sameDimension && dimension->mHasWeather;
    result.playing = playing;
    if (result.eligible) {
        result.subClient = static_cast<unsigned>(client->getClientSubId());
        result.dimension = static_cast<int>(dimension->mId.get());
    }
    if (!observe) return result;
    result.context = weatherDiagnosticContextResult(static_cast<bool>(client), primaryClient, matchingSubClient,
        playing, player != nullptr, localRenderer == &camera.mLevelRenderer, localCamera == &camera, sameDimension);
    return result;
}

SunnyIdentity sunnyIdentity(WeatherOwnerSnapshot const& e) noexcept {
    return {e.generation, e.subClient, e.dimension};
}
std::array<float,4> colorValue(mce::Color const& c) noexcept { return {c.r,c.g,c.b,c.a}; }
bool sunnyDimensionCurrent(Dimension const& dimension) {
    if (!sunnyContext.ownedCapture || !weatherVisualEnabled()
        || settings()->weatherOptions!=sunnyContext.requested
        || sunnyContext.identity.generation != worldGeneration.load(std::memory_order_acquire)) return false;
    auto client = ll::service::getClientInstance();
    if (!client || client->getClientInstanceState() != ClientInstanceState::Playing) return false;
    auto* renderer = client->getLevelRenderer();
    auto* camera = renderer && renderer->mLevelRendererPlayer ? renderer->mLevelRendererPlayer.get() : nullptr;
    auto* player = client->getLocalPlayer();
    if (!camera || !player || &player->getDimension() != &dimension || camera->mDimension != &dimension) return false;
    auto const e = weatherRendererEligibility(*camera);
    return e.eligible && e.hasWeather && sunnyIdentity(e) == sunnyContext.identity;
}
std::mutex sunnyFogMutex;
SunnyFogRecord sunnyFogRecord;
std::atomic_uint64_t sunnyColorCalls{}, sunnyCloudCalls{}, sunnyFogCalls{}, sunnySkyCalls{};
std::atomic_uint64_t sunnyViewsApplied{}, sunnyLightsApplied{}, sunnyViewsRejected{};
std::uintptr_t sunnyImageBase{};

// Native reads and SDK resolution retain the 84d fail-closed verifier contract.
// The shared verifier records only fixed stage labels/codes, never an address.
struct SunnyStartupWin32 {
    std::uintptr_t mainHandle() const { return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)); }
    bool moduleInfo(std::uintptr_t main,unsigned& size) const {
        MODULEINFO module{};
        if(!GetModuleInformation(GetCurrentProcess(),reinterpret_cast<HMODULE>(main),&module,sizeof module))return false;
        size=module.SizeOfImage;return true;
    }
    bool read(std::uintptr_t address,void* output,SIZE_T size) const {
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void const*>(address),output,size,&copied)&&copied==size;
    }
    std::uintptr_t resolve(unsigned index) const {
        return reinterpret_cast<std::uintptr_t>(ll::memory::SymbolView{weatherSkySymbols[index].decorated}.resolve(true));
    }
    TimeChangerSdkBindingRecord timeChangerBindingEvidence(TimeChangerSdkBinding binding) const noexcept {
        return timeChangerSdkBindingEvidence(binding);
    }
    SunnySdkViewBindingRecord viewBindingEvidence() const noexcept { return sdkViewBindingEvidence(); }
    bool prepareViewBinding(std::uintptr_t address) const { return prepareSdkViewBinding(address); }
    SIZE_T queryMemory(std::uintptr_t address,MEMORY_BASIC_INFORMATION& memory) const noexcept {
        return VirtualQuery(reinterpret_cast<void const*>(address),&memory,sizeof(memory));
    }
    bool owner(std::uintptr_t address,std::uintptr_t& result) const {
        HMODULE owner{};
        bool const found=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),&owner);
        result=reinterpret_cast<std::uintptr_t>(owner);return found;
    }
};
SunnyStartupOnce sunnyStartupOnce;
bool verifySunnyNativeBinding(SunnyStartupTrace& trace) {
    SunnyStartupWin32 environment;
    return verifySunnyStartupBinding(environment,trace,sunnyImageBase);
}

LL_STATIC_HOOK(SunnyNativeColorProbe, ll::memory::HookPriority::High,
    sunnyImageBase+sunnyNativeFunctions[0].rva, mce::Color,
    Dimension const& dimension, float partial, mce::Color const& baseColor, float intensity,
    NativeSunnyParameters const& parameters, bool configuredColors, bool disableFlashes) {
    WeatherNativeLease lease;
    bool const accepted=sunnyContext.stage==SunnyStage::View && sunnyDimensionCurrent(dimension) && dimension.mWeather.get();
    std::array<float,5> before{};int flash{};bool wither{};NativeSunnyParameters owned{};
    auto const base=colorValue(baseColor);
    if(accepted) {
        auto const& weather=*dimension.mWeather.get();
        before={weather.mFogLevel,weather.mOldRainLevel,weather.mRainLevel,weather.mOldLightningLevel,weather.mLightningLevel};
        flash=weather.mSkyFlashTime;wither=!dimension.mWitherIDs.get().empty();owned=parameters;
    }
    auto native=origin(dimension,partial,baseColor,intensity,parameters,configuredColors,disableFlashes);
    if(!accepted || !sunnyDimensionCurrent(dimension) || !dimension.mWeather.get())return native;
    auto const& weather=*dimension.mWeather.get();
    if(before!=std::array<float,5>{weather.mFogLevel,weather.mOldRainLevel,weather.mRainLevel,weather.mOldLightningLevel,weather.mLightningLevel}
        || flash!=weather.mSkyFlashTime || wither!=!dimension.mWitherIDs.get().empty())return native;
    for(float value:before)if(!std::isfinite(value) || value<0.f || value>1.f)return native;
    if(!std::isfinite(partial) || partial<0.f || partial>1.f || !std::isfinite(intensity))return native;
    auto const expected=weatherColorProducerValue(base,intensity,owned,before[0],before[1]+(before[2]-before[1]),
        before[3]+(before[4]-before[3])*partial,wither,configuredColors,disableFlashes,flash,partial);
    auto const actual=colorValue(native);
    for(unsigned i=0;i<4;++i)if(!sunnyValueMatches(actual[i],expected[i]))return native;
    float const envelope=weatherSkyEnvelope(sunnyContext.requested);
    auto const alternative=weatherColorProducerValue(base,intensity,owned,envelope,envelope,0.f,
        wither,configuredColors,true,0,partial);
    if(!sunnyDimensionCurrent(dimension))return native;
    auto& capture=*sunnyContext.ownedCapture;
    if(capture.colorsUsed==capture.colors.size())capture.colorsOverflow=true;
    else capture.colors[capture.colorsUsed++]={actual,alternative};
    sunnyColorCalls.fetch_add(1,std::memory_order_relaxed);
    return native; // Only the fresh owned View return may receive alternatives.
}

LL_STATIC_HOOK(SunnyNativeCloudProbe, ll::memory::HookPriority::High,
    sunnyImageBase+sunnyNativeFunctions[1].rva, mce::Color,
    Dimension const& dimension, float partial, NativeSunnyParameters const& parameters, bool configuredColors) {
    WeatherNativeLease lease;
    bool const accepted=sunnyContext.stage==SunnyStage::View && sunnyDimensionCurrent(dimension) && dimension.mWeather.get();
    std::array<float,4> before{}; bool wither{};NativeSunnyParameters owned{};
    if (accepted) {
        auto const& w=*dimension.mWeather.get();
        before={w.mOldRainLevel,w.mRainLevel,w.mOldLightningLevel,w.mLightningLevel};
        wither=!dimension.mWitherIDs.get().empty();owned=parameters;
    }
    SunnyClockCapture clock;SunnyClockScope observe(clock,accepted);
    auto native = origin(dimension,partial,parameters,configuredColors);
    if (!accepted || clock.samples!=1 || !sunnyDimensionCurrent(dimension) || !dimension.mWeather.get()) return native;
    auto const& w=*dimension.mWeather.get();
    if (before!=std::array<float,4>{w.mOldRainLevel,w.mRainLevel,w.mOldLightningLevel,w.mLightningLevel}
        || wither!=!dimension.mWitherIDs.get().empty()) return native;
    auto const index=sunnyCloudLookupIndex(clock.angle);float sine{};SIZE_T copied{};
    if (!index || sunnyCloudLookupRva+*index*sizeof(float)>sunnyImageSize-sizeof(float)
        || !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void const*>(sunnyImageBase+sunnyCloudLookupRva+*index*sizeof(float)),
            &sine,sizeof(sine),&copied) || copied!=sizeof(sine) || !std::isfinite(sine)) return native;
    auto const expected=sunnyCloudProducerValue(sine,owned,before[0]+(before[1]-before[0])*partial,
        before[2]+(before[3]-before[2])*partial,wither,configuredColors);
    auto const actual=colorValue(native);
    for(unsigned i=0;i<4;++i) if(!sunnyValueMatches(actual[i],expected[i])) return native;
    auto const clear=sunnyCloudProducerValue(sine,owned,weatherSkyEnvelope(sunnyContext.requested),0.f,wither,configuredColors);
    if (sunnyDimensionCurrent(dimension)) {
        auto& capture = *sunnyContext.ownedCapture;
        capture.cloud = {actual,clear}; capture.cloudSeen = true;
        sunnyCloudCalls.fetch_add(1,std::memory_order_relaxed);
    }
    return native;
}
LL_STATIC_HOOK(SunnyNativeFogProbe, ll::memory::HookPriority::High,
    sunnyImageBase+sunnyNativeFunctions[2].rva, float,
    Dimension const& dimension, float partial, Vec3 const& position, float minimum, float nativeFog) {
    WeatherNativeLease lease;
    bool const accepted=sunnyContext.stage==SunnyStage::PreRender && sunnyDimensionCurrent(dimension);
    std::array<float,3> const direction{position.x,position.y,position.z};
    SunnyClockCapture clock;SunnyClockScope observe(clock,accepted);
    float const native = origin(dimension,partial,position,minimum,nativeFog);
    if (!accepted || clock.samples!=1 || !sunnyDimensionCurrent(dimension)) return native;
    auto const clear=sunnyFogProducerValue(clock.angle,direction,minimum);
    if (!clear || !sunnyValueMatches(native,*clear+nativeFog*(-*clear))) return native;
    if (sunnyDimensionCurrent(dimension)) {
        auto& capture = *sunnyContext.ownedCapture;
        capture.fogNative = native; capture.fogSunny = *clear+weatherSkyEnvelope(sunnyContext.requested)*(-*clear); capture.fogSeen = true;
        sunnyFogCalls.fetch_add(1,std::memory_order_relaxed);
    }
    return native;
}
// Same Windows x64 member ABI (RCX receiver, XMM1 partial, XMM0 result), but
// no static bedrock_runtime import before our optional Weather gate.
LL_STATIC_HOOK(SunnyNativeSkyBrightnessProbe, ll::memory::HookPriority::High,
    sunnyImageBase+sunnyNativeFunctions[3].rva, float, Dimension const* receiver,float partial) {
    WeatherNativeLease lease;
    bool const accepted = (sunnyContext.stage == SunnyStage::View || sunnyContext.stage == SunnyStage::Light)
        && receiver && sunnyDimensionCurrent(*receiver) && receiver->mWeather.get();
    std::array<float,3> before{};
    if (accepted) {
        auto const& weather = *receiver->mWeather.get();
        before = {weather.mFogLevel,weather.mOldLightningLevel,weather.mLightningLevel};
    }
    float const native = origin(receiver,partial);
    if (!accepted || !sunnyDimensionCurrent(*receiver) || !receiver->mWeather.get()) return native;
    auto const& weather = *receiver->mWeather.get();
    std::array<float,3> const after{weather.mFogLevel,weather.mOldLightningLevel,weather.mLightningLevel};
    if (before != after) return native;
    auto const clear = weatherSkyBrightness(native,after[0],after[1]+(after[2]-after[1])*partial,sunnyContext.requested);
    if (clear) {
        auto& capture = *sunnyContext.ownedCapture;
        capture.skyNative = native; capture.skySunny = *clear; capture.skySeen = true;
        sunnySkyCalls.fetch_add(1,std::memory_order_relaxed);
    }
    return native;
}
// This exact SDK method is already required by TimeChanger in the 111-import
// baseline. Observe its existing result; do not call a getter a second time.
LL_TYPE_INSTANCE_HOOK(SunnyClockObservationHook,ll::memory::HookPriority::High,
    Dimension,&Dimension::$getTimeOfDay,float,int time,float partial) {
    WeatherNativeLease lease;
    float const native=origin(time,partial);
    if(sunnyClockCapture && sunnyDimensionCurrent(*this)) {
        sunnyClockCapture->angle=native;++sunnyClockCapture->samples;
    }
    return native;
}
LL_TYPE_INSTANCE_HOOK(SunnyPreRenderProbe, ll::memory::HookPriority::High,
    LevelRendererPlayer, &LevelRendererPlayer::$preRenderUpdate, void,
    ScreenContext& screen, LevelRenderPreRenderUpdateParameters& parameters) {
    WeatherNativeLease lease;
    auto const e = weatherNativeAdmission.load(std::memory_order_seq_cst)
        ? weatherRendererEligibility(*this) : WeatherRendererEligibility{};
    SunnyCapture capture;
    SunnyScope scope(SunnyStage::PreRender,sunnyIdentity(e),capture,e.eligible && e.hasWeather && !screen.isCapturingPanorama);
    origin(screen,parameters);
    if(sunnyContext.ownedCapture!=&capture || settings()->weatherOptions!=capture.requested) return;
    withOwnedWeatherResult(e,[&]{return weatherRendererEligibility(*this);},
        []{return worldGeneration.load(std::memory_order_acquire);},[&](auto const& after){
            if (!weatherVisualEnabled()) return;
            std::lock_guard lock(sunnyFogMutex);
            // Observe the SDK camera's final scalar after its original call.
            // The pinned native preRender has an explicit HDR0 skip branch;
            // preserve that result rather than requiring a nonexistent helper.
            float const native=mFakeHDR;
            bool const correlated=capture.fogSeen && native==capture.fogNative;
            bool const zeroBranch=!capture.fogSeen && native==0.f;
            sunnyFogRecord={sunnyIdentity(after),mFrameID,native,
                correlated?capture.fogSunny:native,correlated || zeroBranch,capture.requested};
        });
}
LL_TYPE_INSTANCE_HOOK(SunnyLightDataHook, ll::memory::HookPriority::High,
    BaseLightTextureImageBuilder, &BaseLightTextureImageBuilder::$createBaseLightTextureData,
    std::unique_ptr<BaseLightData>, IClientInstance* target, BaseLightData const& current) {
    WeatherNativeLease lease;
    auto snapshot = [&] {
        WeatherRendererEligibility e;
        if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return e;
        auto client = ll::service::getClientInstance();
        if (!client || static_cast<IClientInstance*>(&*client) != target) return e;
        auto* renderer = client->getLevelRenderer();
        auto* camera = renderer && renderer->mLevelRendererPlayer ? renderer->mLevelRendererPlayer.get() : nullptr;
        if (!camera || camera->mDimension != mDimension) return e;
        return weatherRendererEligibility(*camera);
    };
    auto const e = snapshot();
    SunnyCapture capture;
    SunnyScope scope(SunnyStage::Light,sunnyIdentity(e),capture,e.eligible && e.hasWeather);
    auto owned = origin(target,current);
    if (owned && capture.skySeen) withOwnedWeatherResult(e,snapshot,
        []{return worldGeneration.load(std::memory_order_acquire);},[&](auto const& after){
            if (applySunnyLight(*owned,capture,after.generation)) {
                sunnyLightsApplied.fetch_add(1,std::memory_order_relaxed);
            }
        });
    return owned;
}

template<class Revalidate>
bool applySunnyOwnedView(ViewRenderObject& view, SunnyCapture const& capture,
                         SunnyIdentity identity, unsigned frame, WeatherOptions requested,
                         Revalidate&& revalidate, WeatherDecision* observation,bool* sunnyApplied) {
    SunnyFogRecord fog;
    { std::lock_guard lock(sunnyFogMutex); fog = sunnyFogRecord; }
    return applyWeatherSkyView(view,capture,fog,identity,frame,requested,
        std::forward<Revalidate>(revalidate),observation,sunnyApplied);
}

WeatherResultValues ownedWeatherTraceValues(ViewRenderObject const& view) noexcept {
    auto const& data=view.mViewData.get();auto const& weather=view.mWeatherState.get();
    auto const& parameters=weather.mParams.get();auto const& sky=view.mSkyState.get();
    return {data.mDrawWeather,weather.mDensityRain,weather.mDensitySnow,parameters[0].fAlpha,
        parameters[1].fAlpha,sky.mFogLevel,data.mFakeHDR,sky.mSkyDarken};
}
WeatherResultResources ownedWeatherResources(ViewRenderObject const& view) noexcept {
    auto const& weather=view.mWeatherState.get();auto const& p=weather.mParams.get();
    return {static_cast<bool>(weather.mParticleMesh.get()),
        static_cast<bool>(weather.mWeatherTexture.get().mClientTexture.get()),
        weather.mRainMat!=nullptr,weather.mSnowMat!=nullptr,
        weather.mParticleCount,weather.mParticleBoxSize,p[0].fWidth,p[0].fLength,p[1].fWidth,p[1].fLength};
}
WeatherResultTraceKey weatherTraceKey(WeatherOwnerSnapshot const& e,Settings const& request,
    ScreenContext const& screen,ViewRenderObject const& view) noexcept {
    auto const& data=view.mViewData.get();
    return {e.generation,e.subClient,request.renderMask,e.dimension,request.weatherOptions,
        request.on(Feature::WeatherChanger),runtimeReady.load(std::memory_order_acquire),
        featureAvailable(Feature::WeatherChanger),e.eligible,e.hasWeather,screen.isCapturingPanorama,
        data.mIsShadowPass,data.mCameraUnderLiquid,request.on(Feature::TimeChanger),request.on(Feature::Fullbright)};
}
void startWeatherTrialWriter() noexcept {
    auto trial=weatherResultTrial;
    if(!trial->pending())return;
    std::uint64_t session{};
    try {
        session=trial->claimWriter();if(!session)return;
        auto mod=ll::mod::NativeMod::current();
        if(!mod){trial->finishWriter(session,true);return;}
        auto path=mod->getConfigDir().parent_path()/"weather-trial.log";
        // Native.cpp pins this DLL before any hook installs. The worker owns
        // the scalar mailbox/path and a lease until its bounded session ends;
        // it never accesses a world, borrowed view or destroyed UI state.
        auto lease=std::make_shared<WeatherNativeLease>();
        std::thread([trial,session,path=std::move(path),lease=std::move(lease)] {
            try {
                std::ofstream output(path,std::ios::binary|std::ios::app);
                runWeatherTrialReporter(*trial,session,[&](auto const& snapshot,char const* kind){
                    output<<formatWeatherTrialReport(snapshot,kind)<<'\n';output.flush();
                    return output.good();
                },[]{std::this_thread::sleep_for(std::chrono::seconds(1));},
                  []{return WeatherResultTrial::Clock::now();});
            } catch(...) {try{trial->finishWriter(session,true);}catch(...) {}}
        }).detach();
    } catch(...) {if(session)try{trial->finishWriter(session,true);}catch(...) {}}
}

WeatherDiagnosticState diagnosticState() noexcept {
    auto current = settings();
    return {current->on(Feature::WeatherChanger), runtimeReady.load(std::memory_order_acquire),
        weatherNativeAdmission.load(std::memory_order_seq_cst), featureAvailable(Feature::WeatherChanger),
        (current->renderMask & passBit(RenderPass::Weather)) != 0,
        (current->renderMask & passBit(RenderPass::Particles)) != 0,
        (current->renderMask & passBit(RenderPass::Fog)) != 0};
}
bool diagnosticOffPass(WeatherRendererEligibility const& e,
    WeatherDiagnosticState const& before, WeatherDiagnosticState const& after, bool applied) noexcept {
    return weatherDiagnosticOffNativePass(before, after, e.eligible, e.hasWeather,
        e.generation == worldGeneration.load(std::memory_order_acquire), applied);
}

struct VisualWeatherViewHook : LevelRendererPlayer {
    ViewRenderObject detour(ScreenContext& screen,SubClientId clientSubId);
    ViewRenderObject origin(ScreenContext& screen,SubClientId clientSubId) {
        return (this->*sdkViewContinuation)(screen,clientSubId);
    }
    static int hook() {
        static_assert(!ll::memory::virtualDetector<CreateView,&LevelRendererPlayer::createViewRenderObject>());
        if(!sdkViewTarget)sdkViewTarget=ll::memory::resolveIdentifier<CreateView>(&LevelRendererPlayer::createViewRenderObject);
        return ll::memory::hookEx(sdkViewTarget,ll::memory::toFuncPtr(&VisualWeatherViewHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&sdkViewContinuation),ll::memory::HookPriority::High,ll::memory::SaveNone,true);
    }
};
ViewRenderObject VisualWeatherViewHook::detour(ScreenContext& screen,SubClientId clientSubId) {
    WeatherNativeLease lease;
    if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return origin(screen, clientSubId);
    WeatherDiagnosticCapture capture(WeatherProducer::View);
    auto const before = capture ? diagnosticState() : WeatherDiagnosticState{};
    weatherChangerDiagnostics.viewCalls.fetch_add(1, std::memory_order_relaxed);
    bool const trialEnabled=weatherResultTrial->accepting();
    auto const e = weatherRendererEligibility(*this, clientSubId, capture || trialEnabled);
    unsigned const nativeFrame = e.eligible ? mFrameID : 0;
    auto const requestState=settings();
    auto const requested=requestState->weatherOptions;
    SunnyCapture sunny;
    SunnyScope sunnyScope(SunnyStage::View,sunnyIdentity(e),sunny,
        e.eligible && e.hasWeather && !screen.isCapturingPanorama);
    // This API produces a world view, before UI/first-person screen flags
    // necessarily describe its consumer. Match the exact primary renderer,
    // subclient and dimension instead, and reject shadow views after creation.
    WeatherDecision scopeDecision{};
    VisualWeatherScope scope(e.eligible, e.hasWeather, e.generation, capture ? &scopeDecision : nullptr);
    if (capture) capture.entry({before, e.context, scopeDecision, e.eligible, e.hasWeather, e.playing});
    auto& count = visualWeatherContext.eligible
        ? weatherChangerDiagnostics.acceptedRenderScopes
        : weatherChangerDiagnostics.rejectedRenderScopes;
    count.fetch_add(1, std::memory_order_relaxed);
    if (visualWeatherContext.eligible)
        weatherChangerDiagnostics.viewsInScope.fetch_add(1, std::memory_order_relaxed);
    auto view = origin(screen, clientSubId);
    bool const trace=trialEnabled && !view.mViewData.get().mIsShadowPass;
    WeatherProducerResult traceResult;
    if(trace) {
        traceResult.key=weatherTraceKey(e,*requestState,screen,view);traceResult.frame=nativeFrame;
        traceResult.context=e.context;
        traceResult.native=ownedWeatherTraceValues(view);
    }
    bool sunnyApplied{};
    WeatherDiagnosticResult observation;
    if (capture) {
        observation.nativeDraw = view.mViewData.get().mDrawWeather;
        observation.nativeFog = view.mSkyState.get().mFogLevel != 0.f;
    }
    observation.decision = WeatherDecision::ScopeRejected;
    bool const ownerVerified = withOwnedWeatherResult(e, [&] { return weatherRendererEligibility(*this, clientSubId); },
        [] { return worldGeneration.load(std::memory_order_acquire); }, [&](auto const& after) {
            VisualWeatherScope verified(after.eligible, after.hasWeather, after.generation);
            if(trace && mDimension && mDimension->mWeather.get()) {
                auto const& weather=*mDimension->mWeather.get();
                traceResult.world={weather.mOldRainLevel,weather.mRainLevel,weather.mOldLightningLevel,weather.mLightningLevel};
                traceResult.worldValid=true;
            }
            auto revalidate=[&] {
                    auto const finalOwner=weatherRendererEligibility(*this,clientSubId);
                    return !screen.isCapturingPanorama && nativeFrame==mFrameID
                        && settings()->weatherOptions==requested
                        && sameWeatherResultOwner(after,finalOwner,worldGeneration.load(std::memory_order_acquire));
                };
            bool const applied=applySunnyOwnedView(view,sunny,sunnyIdentity(after),nativeFrame,requested,revalidate,
                capture || trace ? &observation.decision : nullptr,&sunnyApplied);
            if (applied) {
                weatherChangerDiagnostics.viewApplications.fetch_add(1, std::memory_order_relaxed);
                observation.applied = true;
            }
            if(sunnyApplied) sunnyViewsApplied.fetch_add(1,std::memory_order_relaxed);
            else sunnyViewsRejected.fetch_add(1,std::memory_order_relaxed);
        });
    if(trace) {
        traceResult.ownerVerified=ownerVerified;traceResult.decision=observation.decision;
        traceResult.applied=observation.applied;traceResult.skyApplied=sunnyApplied;
        traceResult.owned=ownedWeatherTraceValues(view);traceResult.resources=ownedWeatherResources(view);
        weatherResultTrial->producer(traceResult);
    }
    if (capture) {
        observation.state = diagnosticState();
        observation.returnedDraw = view.mViewData.get().mDrawWeather;
        observation.returnedFog = view.mSkyState.get().mFogLevel != 0.f;
        observation.offNativePass = ownerVerified && !view.mViewData.get().mIsShadowPass && diagnosticOffPass(e, before, observation.state, observation.applied);
        observation.mutationVerified = observation.applied && !observation.returnedDraw && !observation.returnedFog;
        capture.complete(observation);
    }
    return view;
}

LL_TYPE_INSTANCE_HOOK(VisualWeatherRainSplashHook, ll::memory::HookPriority::High,
    LevelRendererCamera, &LevelRendererCamera::doRainUpdate, LevelRendererCamera::RainState) {
    WeatherNativeLease lease;
    if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return origin();
    WeatherDiagnosticCapture capture(WeatherProducer::Rain);
    auto const before = capture ? diagnosticState() : WeatherDiagnosticState{};
    weatherChangerDiagnostics.rainUpdateCalls.fetch_add(1, std::memory_order_relaxed);
    auto const e = weatherRendererEligibility(*this, std::nullopt, static_cast<bool>(capture));
    WeatherDecision scopeDecision{};
    VisualWeatherScope scope(e.eligible, e.hasWeather, e.generation, capture ? &scopeDecision : nullptr);
    if (capture) capture.entry({before, e.context, scopeDecision, e.eligible, e.hasWeather, e.playing});
    if (visualWeatherContext.eligible && visualWeatherContext.hasWeather)
        weatherChangerDiagnostics.rainUpdatesInScope.fetch_add(1, std::memory_order_relaxed);
    auto rain = origin();
    WeatherDiagnosticResult observation;
    if (capture) observation.nativeSplash = !rain.mRainSplashParticles.get().empty();
    std::size_t removed{};
    observation.decision = WeatherDecision::ScopeRejected;
    bool const ownerVerified = withOwnedWeatherResult(e, [&] { return weatherRendererEligibility(*this); },
        [] { return worldGeneration.load(std::memory_order_acquire); }, [&](auto const& after) {
            VisualWeatherScope verified(after.eligible, after.hasWeather, after.generation);
            removed = suppressOwnedRainSplashes(rain, capture ? &observation.decision : nullptr);
        });
    weatherChangerDiagnostics.rainSplashPositionsRemoved.fetch_add(removed, std::memory_order_relaxed);
    if (capture) {
        observation.state = diagnosticState();
        observation.applied = observation.decision == WeatherDecision::Applied;
        observation.removed = removed;
        observation.returnedSplash = !rain.mRainSplashParticles.get().empty();
        observation.offNativePass = ownerVerified && diagnosticOffPass(e, before, observation.state, observation.applied);
        observation.mutationVerified = observation.applied && !observation.returnedSplash;
        capture.complete(observation);
    }
    return rain;
}

// Constructors remain observable for the entry-only/toggle regression. They
// are passthroughs: never change density/alpha or NoRender's saved native state.
LL_TYPE_INSTANCE_HOOK(VisualWeatherSnapshotHook, ll::memory::HookPriority::High,
    WeatherRenderObject, &WeatherRenderObject::$ctor, void*, WeatherRenderObject&& source) {
    WeatherNativeLease lease;
    if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return origin(std::move(source));
    WeatherDiagnosticCapture capture(WeatherProducer::WeatherMove);
    if (capture) capture.entry({diagnosticState(), WeatherContextResult::Accepted, WeatherDecision::Observer,
        visualWeatherContext.eligible, visualWeatherContext.hasWeather, false});
    weatherChangerDiagnostics.weatherMoveCalls.fetch_add(1, std::memory_order_relaxed);
    auto* result = origin(std::move(source));
    if (result == this) {
        if (visualWeatherContext.eligible)
            weatherChangerDiagnostics.weatherMovesInScope.fetch_add(1, std::memory_order_relaxed);
    }
    if (capture) { WeatherDiagnosticResult r; r.state = diagnosticState(); r.decision = WeatherDecision::Observer; capture.complete(r); }
    return result;
}

LL_TYPE_INSTANCE_HOOK(VisualWeatherSkySnapshotHook, ll::memory::HookPriority::Normal,
    SkyRenderObject, &SkyRenderObject::$ctor, void*, SkyRenderObject&& source) {
    WeatherNativeLease lease;
    if (!weatherNativeAdmission.load(std::memory_order_seq_cst)) return origin(std::move(source));
    WeatherDiagnosticCapture capture(WeatherProducer::SkyMove);
    if (capture) capture.entry({diagnosticState(), WeatherContextResult::Accepted, WeatherDecision::Observer,
        visualWeatherContext.eligible, visualWeatherContext.hasWeather, false});
    weatherChangerDiagnostics.skyMoveCalls.fetch_add(1, std::memory_order_relaxed);
    auto* result = origin(std::move(source));
    if (result == this) {
        if (visualWeatherContext.eligible)
            weatherChangerDiagnostics.skyMovesInScope.fetch_add(1, std::memory_order_relaxed);
    }
    if (capture) { WeatherDiagnosticResult r; r.state = diagnosticState(); r.decision = WeatherDecision::Observer; capture.complete(r); }
    return result;
}

struct HookState { int (*install)(); bool active{}; };
template<class Hook> HookState hookState() { return {[] { return Hook::hook(); }, false}; }
std::array hooks{hookState<VisualWeatherViewHook>(), hookState<VisualWeatherSnapshotHook>(),
    hookState<VisualWeatherSkySnapshotHook>(), hookState<VisualWeatherRainSplashHook>()};
std::array sunnyHooks{hookState<SunnyNativeColorProbe>(),hookState<SunnyNativeCloudProbe>(),
    hookState<SunnyNativeFogProbe>(),hookState<SunnyNativeSkyBrightnessProbe>(),
    hookState<SunnyPreRenderProbe>(),hookState<SunnyLightDataHook>(),hookState<SunnyClockObservationHook>()};

SunnySdkViewBindingRecord sdkViewBindingEvidence() noexcept {
    auto const epoch=sdkViewEpoch.load();
    SunnySdkViewBindingRecord record{sdkViewSavedTarget.load(),reinterpret_cast<std::uintptr_t>(sdkViewTarget),
        reinterpret_cast<std::uintptr_t>(ll::memory::toFuncPtr(&LevelRendererPlayer::createViewRenderObject)),
        reinterpret_cast<std::uintptr_t>(ll::memory::toFuncPtr(sdkViewContinuation)),epoch,hooks[0].active,sdkViewClosed.load()};
    if(epoch!=sdkViewEpoch.load())record.closed=true;
    return record;
}
bool prepareSdkViewBinding(std::uintptr_t candidate) {
    if(sdkViewClosed.load() || weatherNativeAdmission.load())return false;
    auto const typed=reinterpret_cast<std::uintptr_t>(ll::memory::toFuncPtr(&LevelRendererPlayer::createViewRenderObject));
    if(!candidate || typed!=candidate || (sdkViewTarget && reinterpret_cast<std::uintptr_t>(sdkViewTarget)!=candidate))return false;
    auto& hook=hooks[0];
    if(!hook.active)hook.active=hook.install()==0;
    if(!hook.active || !sdkViewContinuation)return false;
    std::uintptr_t empty{};
    if(!sdkViewSavedTarget.compare_exchange_strong(empty,candidate) && empty!=candidate)return false;
    weatherDiagnosticHookMask.fetch_or(1u,std::memory_order_relaxed);
    return true;
}

template<std::size_t N> std::string counterList(std::array<std::uint64_t, N> const& values) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < N; ++i) { if (i) stream << ','; stream << values[i]; }
    return stream.str();
}
// Read-only, once per explicitly started bounded diagnostic session. Resolve
// identifiers only: never call these functions, install hooks or change game
// fields. The normal OFF path never invokes this observer.
void emitSkySymbolEvidence(ll::mod::NativeMod& mod, std::uint64_t session) {
    MODULEINFO module{};
    auto process = GetCurrentProcess();
    auto main = GetModuleHandleW(nullptr);
    if (!main || !GetModuleInformation(process, main, &module, sizeof(module))) {
        mod.getLogger().info("WeatherSkySymbol session={} moduleInfo=unavailable", session);
        return;
    }
    auto const base = reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll);
    auto const size = static_cast<std::uintptr_t>(module.SizeOfImage);
    auto read = [&](std::uintptr_t rva, void* output, SIZE_T bytes) {
        if (rva >= size || bytes > size - rva) return false;
        SIZE_T copied{};
        return ReadProcessMemory(process, reinterpret_cast<void const*>(base + rva), output, bytes, &copied)
            && copied == bytes;
    };
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    bool const headers = read(0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE
        && dos.e_lfanew > 0 && read(static_cast<std::uintptr_t>(dos.e_lfanew), &nt, sizeof(nt))
        && nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
        && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
        && nt.OptionalHeader.SizeOfImage == size;
    mod.getLogger().info("WeatherSkySymbol session={} mainImageSize={} PEHeaders={} timestamp={} machine={}; SDK=26.51.5 identifiers; no functions called",
        session, size, headers, headers ? nt.FileHeader.TimeDateStamp : 0, headers ? nt.FileHeader.Machine : 0);
    if (headers && nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG) {
        auto const directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        auto const entries = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        if (entries <= 64 && directory.Size % sizeof(IMAGE_DEBUG_DIRECTORY) == 0) {
            for (unsigned i = 0; i < entries; ++i) {
                IMAGE_DEBUG_DIRECTORY debug{}; std::array<unsigned char, 24> codeview{};
                if (!read(directory.VirtualAddress + i * sizeof(debug), &debug, sizeof(debug))
                    || debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug.SizeOfData < codeview.size()
                    || !read(debug.AddressOfRawData, codeview.data(), codeview.size())
                    || std::memcmp(codeview.data(), "RSDS", 4) != 0) continue;
                GUID guid{}; unsigned age{};
                std::memcpy(&guid, codeview.data() + 4, sizeof(guid));
                std::memcpy(&age, codeview.data() + 20, sizeof(age));
                mod.getLogger().info("WeatherSkySymbol session={} CodeView={:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x} age={}",
                    session, guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                    guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
            }
        }
    }
    for (auto const& symbol : weatherSkySymbols) {
        try {
            auto* address = ll::memory::SymbolView{symbol.decorated}.resolve(true);
            auto const result = classifyWeatherSkyAddress(reinterpret_cast<std::uintptr_t>(address), base, size);
            mod.getLogger().info("WeatherSkySymbol session={} name={} status={} rva={:#x}",
                session, symbol.name, static_cast<unsigned>(result.status), result.rva);
        } catch (...) {
            mod.getLogger().info("WeatherSkySymbol session={} name={} resolver=failed", session, symbol.name);
        }
    }
}

void emitDiagnosticPoll(WeatherDiagnosticPoll const& result) noexcept {
    if (!result.started && !result.finished) return;
    try {
        auto mod = ll::mod::NativeMod::current();
        if (!mod) return;
        if (result.started) mod->getLogger().info(
            "WeatherDiag start session={} maxSeconds=30 maxEvents=4096 hooksMask={} (view=1 weatherMove=2 skyMove=4 rain=8); counters only; precipitationOutputVerified covers rain/Fog or splash output, never sunny correctness; no visual-success claim",
            result.started->session, result.started->hookMask);
        if (result.started) emitSkySymbolEvidence(*mod, result.started->session);
        if (!result.finished) return;
        auto const& s = *result.finished;
        mod->getLogger().info("WeatherSunny processCumulative session={} color={} cloud={} fog={} sky={} ownedViewTransactionsCommitted={} ownedViewTransactionsRejected={} ownedLights={}; visual result unverified",
            s.session,sunnyColorCalls.load(),sunnyCloudCalls.load(),sunnyFogCalls.load(),sunnySkyCalls.load(),
            sunnyViewsApplied.load(),sunnyViewsRejected.load(),sunnyLightsApplied.load());
        mod->getLogger().info("WeatherDiag end session={} reason={} events={} inFlight={} hooksMask={} amountSaturations={}",
            s.session, static_cast<unsigned>(s.end), s.events, s.inFlight, s.hookMask, s.amountSaturations);
        mod->getLogger().info("WeatherDiag settings session={} samples={} requestedOn={} publishedOn={} mismatch={} NoRenderWeather={} NoRenderParticles={} NoRenderFog={}",
            s.session, s.serviceSamples, s.requestedOn, s.publishedOn, s.requestMismatch, s.noRenderWeather, s.noRenderParticles, s.noRenderFog);
        constexpr char const* names[]{"view", "rain", "weatherMove", "skyMove"};
        for (unsigned i = 0; i < s.producers.size(); ++i) {
            auto const& c = s.producers[i];
            mod->getLogger().info("WeatherDiag {} session={} calls={} completed={} eligible={} playing={} hasWeather={} entryOn={} exitOn={} notReady={} admissionClosed={} noCapability={} applied={} offNativePass={} precipitationOutputVerified={}",
                names[i], s.session, c.calls, c.completed, c.eligible, c.playing, c.hasWeather, c.entryOn, c.exitOn,
                c.entryNotReady, c.entryAdmissionClosed, c.entryNoCapability, c.applied, c.offNativePass, c.mutationVerified);
            mod->getLogger().info("WeatherDiag {} session={} contexts=[{}] scopes=[{}] decisions=[{}] nativeDraw={} returnedDraw={} nativeFog={} returnedFog={} nativeSplash={} returnedSplash={} removed={}",
                names[i], s.session, counterList(c.contexts), counterList(c.scopes), counterList(c.decisions), c.nativeDraw, c.returnedDraw,
                c.nativeFog, c.returnedFog, c.nativeSplash, c.returnedSplash, c.removed);
        }
    } catch (...) { /* Diagnostics must not escape into a game callback. */ }
}
}

void observeWeatherSkyConsumer(LevelRendererCamera const& camera,ScreenContext const& screen,
    ViewRenderObject const& view,bool originSelected) noexcept {
    if(!weatherResultTrial->accepting() || !weatherNativeAdmission.load(std::memory_order_seq_cst)
        || view.mViewData.get().mIsShadowPass)return;
    WeatherNativeLease lease;
    try {
        auto const e=weatherRendererEligibility(camera);
        WeatherSkyInputResult result;
        result.key=weatherTraceKey(e,*settings(),screen,view);result.frame=e.eligible?static_cast<unsigned>(camera.mFrameID):0;
        result.originSelected=originSelected;result.values=ownedWeatherTraceValues(view);
        result.resources=ownedWeatherResources(view);
        weatherResultTrial->skyInput(result);
    } catch(...) {} // Borrowed input is read only within the existing callback.
}

void pollWeatherChangerDiagnostics(bool requested) noexcept {
    startWeatherTrialWriter();
    if (!weatherDiagnosticSession.pending()) return;
    try { emitDiagnosticPoll(weatherDiagnosticSession.poll(requested, diagnosticState(), weatherDiagnosticHookMask.load())); }
    catch (...) {}
}
void expireWeatherChangerDiagnostics() noexcept {
    try {
        weatherResultTrial->stop(WeatherTrialEnd::RuntimeEnded);
        weatherDiagnosticSession.stop();
        pollWeatherChangerDiagnostics(false);
    } catch (...) {}
}

bool installWeatherChanger() {
    SunnyStartupTrace trace;
    bool const startup=diagnosedSunnyInstall(trace,[&]{return verifySunnyNativeBinding(trace);},[&]{
        if(!installSunnyStartupHooks(sunnyHooks,trace,SunnyStartupStage::SunnyHook,[](unsigned){}))return false;
        return installSunnyStartupHooks(hooks,trace,SunnyStartupStage::ViewHook,[](unsigned index){
            weatherDiagnosticHookMask.fetch_or(1u<<index,std::memory_order_relaxed);
        });
    });
    bool const admitted=weatherNativeAdmission.load(std::memory_order_seq_cst);
    sunnyStartupOnce.emit(trace,admitted,[&](std::string_view message){
        if(auto mod=ll::mod::NativeMod::current()) {
            if(admitted)mod->getLogger().info("{}",message);
            else mod->getLogger().warn("{}",message);
        }
    });
    return startup;
}

bool removeWeatherChanger() noexcept {
    sdkViewClosed.store(true);sdkViewEpoch.fetch_add(1);
    weatherNativeAdmission.store(false, std::memory_order_seq_cst);
    expireWeatherChangerDiagnostics();
    availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::WeatherChanger)), std::memory_order_acq_rel);
    // The SDK does not promise concurrent trampoline retirement. Native.cpp
    // pinned the DLL before registration; retain passthroughs until process exit.
    for (auto const& hook : hooks) if (hook.active) return false;
    for (auto const& hook : sunnyHooks) if (hook.active) return false;
    return weatherNativeReaders.load(std::memory_order_seq_cst) == 0;
}
} // namespace praxis::companion
