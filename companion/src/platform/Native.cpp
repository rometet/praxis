#include "resourcepacks/TitlePackSwitch.h"
#include "resourcepacks/WorldPackSwitch.h"
#include "platform/Native.h"
#include "platform/EffectPatches.h"
#include "core/AntiEffectPolicy.h"
#include "core/Settings.h"
#include "core/FullbrightPolicy.h"
#include "core/TimeChangerPolicy.h"
#include "platform/TimeChanger.h"
#include "platform/WeatherChanger.h"
#include "platform/CameraCandidate.h"
#include "core/WeatherChangerPolicy.h"
#include "core/NativeHookLifetime.h"
#include "core/NoRenderWeatherRegistry.h" // legacy registry retained for 26.51 tests only
#include "platform/ToolProtection.h"
#include "platform/HandRestock.h"
#include "platform/InventoryRequest.h"
#include "platform/InventoryActions.h"
#include "platform/ItemScrollerExtras.h"
#include "platform/FlexiblePlacement.h"
#include "platform/FastClick.h"
#include "platform/FastBlockPlacement.h"
#include "render/Renderer.h"
#include "runtime/Runtime.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "runtime/InformationRuntime.h"
#include "runtime/GameplayRuntime.h"
#include "runtime/ConfigStorage.h"
#include "map/MapRuntime.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/entity/systems/ClientInputUpdateSystem.h"
#include "mc/client/input/ClientMoveInputHandler.h"
#include "mc/deps/vanilla_components/MovementAbilitiesComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/deps/vanilla_components/ActorDataFlagComponent.h"
#include "mc/entity/components/RawMoveInputComponent.h"
#include "mc/entity/components/SneakingComponent.h"
#include "mc/entity/components/WasInWaterFlagComponent.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/particlesystem/particle/ParticleRenderer.h"
#include "mc/deps/minecraft_renderer/objects/WeatherRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/FrameRenderObject.h"
#include "core/WeatherOptions.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/actor/Actor.h"
#include "mc/deps/input/MouseAction.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace praxis::companion {
namespace {
NativeHookLifetime nativeLifetime;
void nativeModuleAnchor() {}
bool pinNativeModule() {
 HMODULE module{};
 return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
  reinterpret_cast<LPCWSTR>(&nativeModuleAnchor), &module) != 0;
}
std::atomic<IOptionRegistry const*> currentOptions{};
std::mutex optionHookMutex;
struct DynamicHook {void* target{};void* detour{};bool active=true;};
std::vector<DynamicHook> dynamicHooks;
using GammaFn=float(*)(IOptionRegistry const*);
using BoolFn=bool(*)(IOptionRegistry const*);
GammaFn originalGamma{};BoolFn originalHud{},originalFlash{};
FullbrightReadiness fullbrightReadiness;
ll::event::ListenerPtr zoomMouseListener;
ll::event::ListenerPtr cameraKeyListener;
ll::event::ListenerPtr worldExitListener;
float gammaHook(IOptionRegistry const* self){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return originalGamma(self);
 return fullbrightGamma([&]{return originalGamma(self);},[&]() -> std::optional<float> {
  if(!lease.admitted())return std::nullopt;
  auto client=ll::service::getClientInstance();
  if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing){
   fullbrightReadiness.reset();return std::nullopt;
  }
  auto* player=client->getLocalPlayer();
  fullbrightReadiness.observe(player,false);
  auto s=settings();
  if(self!=currentOptions.load(std::memory_order_acquire)
   ||!s->on(Feature::Fullbright)||!fullbrightReadiness.readyFor(player))return std::nullopt;
  return s->brightness;
 });
}
bool hudHook(IOptionRegistry const* self){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return originalHud(self);
 bool const native=originalHud(self);if(!lease.admitted())return native;
 auto s=settings();return native||(self==currentOptions.load(std::memory_order_acquire)&&(s->renderMask&passBit(RenderPass::Hud))!=0);
}
bool flashHook(IOptionRegistry const* self){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return originalFlash(self);
 bool const native=originalFlash(self);if(!lease.admitted())return native;
 auto s=settings();return native||(self==currentOptions.load(std::memory_order_acquire)&&(s->renderMask&passBit(RenderPass::Lightning))!=0);
}
template<class Method,class Fn>
bool hookOption(IOptionRegistry const& options,Method method,Fn detour,Fn& original){
 auto index=ll::memory::getVtableIndex(method);if(!index)return false;
 auto target=(*reinterpret_cast<void*const*const*>(&options))[*index];
 for(auto const& h:dynamicHooks)if(h.active&&h.target==target&&h.detour==reinterpret_cast<void*>(detour))return true;
 if(original)return false;
 if(ll::memory::hook(target,reinterpret_cast<void*>(detour),reinterpret_cast<void**>(&original),ll::memory::HookPriority::Normal)!=0){original=nullptr;return false;}
 dynamicHooks.push_back({target,reinterpret_cast<void*>(detour),true});return true;
}
bool worldActor(BaseActorRenderContext& context,Actor& actor){
 // 26.60 can dispatch world actors outside the old render queue. Restrict
 // suppression to the primary playing world and exclude UI/first-person.
 (void)actor;
 if(context.mScreenContext.isDrawingUI
    ||context.mScreenContext.isDrawingFirstPersonObjects
    ||context.mScreenContext.isCapturingPanorama
    ||!runtimeReady.load(std::memory_order_acquire))return false;
 auto client=ll::service::getClientInstance();
 if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing
    ||static_cast<IClientInstance*>(&*client)!=&context.mClientInstance)return false;
 auto* player=client->getLocalPlayer();
 auto* renderer=client->getLevelRenderer();
 return player&&renderer&&renderer->mLevelRendererPlayer
     &&renderer->mLevelRendererPlayer->mDimension==&player->getDimension();
}
bool hideActor(BaseActorRenderContext& context,Actor& actor){
 auto s=settings();if(!s->renderMask||!worldActor(context,actor))return false;
 auto const& type=actor.getTypeName();
 ActorVisual visual=type=="minecraft:item"?ActorVisual::DroppedItem:
  type=="minecraft:lightning_bolt"?ActorVisual::LightningBolt:ActorVisual::Other;
 return suppressActor(s->renderMask,visual,RenderScope::VanillaWorld);
}
using ActorRenderSimple=void(ActorRenderDispatcher::*)(BaseActorRenderContext&,Actor&,bool);
LL_TYPE_INSTANCE_HOOK(ActorHook,ll::memory::HookPriority::Lowest,ActorRenderDispatcher,
 static_cast<ActorRenderSimple>(&ActorRenderDispatcher::render),void,
 BaseActorRenderContext& context,Actor& actor,bool ignoreLighting){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,actor,ignoreLighting);return;}
 if(!hideActor(context,actor))origin(context,actor,ignoreLighting);
}
using ActorRenderPositioned=void(ActorRenderDispatcher::*)(BaseActorRenderContext&,Actor&,Vec3 const&,Vec3 const&,Vec2 const&,bool);
LL_TYPE_INSTANCE_HOOK(ActorPositionedHook,ll::memory::HookPriority::Lowest,ActorRenderDispatcher,
 static_cast<ActorRenderPositioned>(&ActorRenderDispatcher::render),void,
 BaseActorRenderContext& context,Actor& actor,Vec3 const& target,Vec3 const& position,Vec2 const& rotation,bool ignoreLighting){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,actor,target,position,rotation,ignoreLighting);return;}
 if(!hideActor(context,actor))origin(context,actor,target,position,rotation,ignoreLighting);
}
LL_TYPE_INSTANCE_HOOK(BlockEntityHideHook,ll::memory::HookPriority::Lowest,LevelRendererPlayer,
 &LevelRendererPlayer::$renderBlockEntities,void,BaseActorRenderContext& context,bool alpha){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,alpha);return;}
 if((settings()->renderMask&passBit(RenderPass::BlockEntities))==0)origin(context,alpha);
}
LL_TYPE_INSTANCE_HOOK(WorldOverlayHook,ll::memory::HookPriority::High,LevelRendererPlayer,
 &LevelRendererPlayer::$renderBlockEntities,void,BaseActorRenderContext& context,bool alpha){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,alpha);return;}
 captureClient(context.mClientInstance);origin(context,alpha);
 if(lease.admitted()){cameraCandidateRender(*this);drawWorld(context,alpha);}
}
LL_TYPE_INSTANCE_HOOK(SkyHook,ll::memory::HookPriority::Lowest,LevelRendererCamera,
 &LevelRendererCamera::renderSky,void,ScreenContext& context,ViewRenderObject const& object,
 BaseSceneDirectionalLightRenderData const& light){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,object,light);return;}
 bool const selected=(settings()->renderMask&passBit(RenderPass::Sky))==0;
 observeWeatherSkyConsumer(*this,context,object,selected);
 if(selected)origin(context,object,light);
}
LL_TYPE_INSTANCE_HOOK(ParticlesHook,ll::memory::HookPriority::Lowest,ParticleRenderer,
 &ParticleRenderer::renderParticles,void,ScreenContext& context,Vec3 const& target,Vec3 const& position,ParticleRenderData const& data){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,target,position,data);return;}
 if(context.isDrawingUI||(settings()->renderMask&passBit(RenderPass::Particles))==0)origin(context,target,position,data);
}
// The 26.60 SDK has no WeatherRenderObject move constructor/destructor hooks.
// Suppress only the fresh, caller-owned render frame; do not save WeatherRenderObject
// pointers or mutate world weather, so the next frame restores OFF automatically.
void suppressWeatherState(WeatherRenderObject& object) {
 object.mDensityRain=0.0f;object.mDensitySnow=0.0f;object.mDensityPlankton=0.0f;
 object.mDensityRedSpores=0.0f;object.mDensityBlueSpores=0.0f;
 object.mDensityAsh=0.0f;object.mDensityWhiteAsh=0.0f;
 auto& params=object.mParams.get();
 for(auto& param:params)param.fAlpha=0.0f;
}
bool noRenderWeatherEligible(LevelRendererCamera const& camera) {
 auto client=ll::service::getClientInstance();
 if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing)return false;
 auto* level=client->getLevelRenderer();
 auto* player=client->getLocalPlayer();
 if(!level||!player||level!=&camera.mLevelRenderer
    ||!level->mLevelRendererPlayer||level->mLevelRendererPlayer.get()!=&camera
    ||camera.mDimension!=&player->getDimension()
    ||camera.mIsCameraInCaptureMode)return false;
 return true;
}
bool noRenderWeatherRequested() {
 return runtimeReady.load(std::memory_order_acquire)
     &&(settings()->renderMask&passBit(RenderPass::Weather))!=0;
}
// 26.60/MSVC x64 aggregate-return ABI: RCX is caller-owned output storage,
// RDX is LevelRendererPlayer. Match the existing WeatherChanger view hook.
using NoRenderViewMethod=ViewRenderObject(LevelRendererPlayer::*)(ScreenContext&,SubClientId);
using NoRenderViewRaw=ViewRenderObject* (*)(ViewRenderObject*,LevelRendererPlayer*,ScreenContext&,SubClientId);
NoRenderViewRaw noRenderViewOrigin{};
std::atomic_bool noRenderWeatherLogged{};
struct NoRenderWeatherViewHook {
 static ViewRenderObject* detour(ViewRenderObject* out,LevelRendererPlayer* renderer,
                                ScreenContext& screen,SubClientId subClient) {
  NativeHookLifetime::Lease lease(nativeLifetime);
  // 26.60 does not install the old TimeChanger pre-render callback that
  // powered FreeCamera. Reuse this already validated view-production hook,
  // without registering another native trampoline during world entry.
  bool const cameraWorldView=lease.admitted()&&renderer&&out
      &&!screen.isDrawingUI&&!screen.isDrawingFirstPersonObjects
      &&!screen.isCapturingPanorama;
  if(cameraWorldView)cameraCandidateBeforeRender(*renderer);
  ViewRenderObject* returned=noRenderViewOrigin(out,renderer,screen,subClient);
  if(cameraWorldView)cameraCandidateRender(*renderer);
  if(!lease.admitted()||!out||returned!=out||!renderer
     ||screen.isCapturingPanorama||!noRenderWeatherRequested()
     ||!noRenderWeatherEligible(*renderer))return returned;
  auto client=ll::service::getClientInstance();
  if(!client||client->getClientSubId()!=subClient)return returned;
  auto& data=out->mViewData.get();
  if(data.mIsShadowPass)return returned;
  data.mDrawWeather=false;
  suppressWeatherState(out->mWeatherState.get());
  if(!noRenderWeatherLogged.exchange(true,std::memory_order_relaxed))
      logGuiInfo("NoRender Weather 26.60: owned frame precipitation suppressed");
  return returned;
 }
 static int hook() {
  auto target=ll::memory::resolveIdentifier<NoRenderViewMethod>(
      &LevelRendererPlayer::createViewRenderObject);
  if(!target)return -1;
  // Run outermost: NoRender wins after any WeatherChanger frame edits.
  return ll::memory::hookEx(target,ll::memory::toFuncPtr(&NoRenderWeatherViewHook::detour),
      reinterpret_cast<ll::memory::FuncPtr*>(&noRenderViewOrigin),
      ll::memory::HookPriority::Highest,ll::memory::SaveNone,true);
 }
};
LL_TYPE_INSTANCE_HOOK(NoRenderRainSplashHook,ll::memory::HookPriority::Highest,
 LevelRendererCamera,&LevelRendererCamera::doRainUpdate,LevelRendererCamera::RainState) {
 NativeHookLifetime::Lease lease(nativeLifetime);
 auto rain=origin();
 if(lease.admitted()&&noRenderWeatherRequested()&&noRenderWeatherEligible(*this))
     rain.mRainSplashParticles.get().clear();
 return rain;
}
// 26.60 also carries an already assembled FrameRenderObject to renderLevel.
// In this version the old 26.51 weather/sun producers are not always called.
// Apply visual-only switches at the actual consuming boundary, without writing
// Dimension, Weather, world time, weather source or game settings.
std::atomic_bool visualFrameSeen{},visualNoRenderSeen{},visualWeatherSeen{},visualTimeSeen{};
LL_TYPE_INSTANCE_HOOK(RenderFrameVisual2660Hook,ll::memory::HookPriority::Normal,
 LevelRenderer,&LevelRenderer::renderLevel,void,
 ScreenContext& screen,FrameRenderObject const& frame) {
 NativeHookLifetime::Lease lease(nativeLifetime);
 if(lease.admitted()) {
  auto client=ll::service::getClientInstance();
  if(client && client->getClientInstanceState()==ClientInstanceState::Playing
     &&client->getLevelRenderer()==this &&client->getLocalPlayer()
     &&mLevelRendererPlayer &&!screen.isDrawingUI
     &&!screen.isDrawingFirstPersonObjects &&!screen.isCapturingPanorama
     &&runtimeReady.load(std::memory_order_acquire)) {
    auto* player=client->getLocalPlayer();
    auto* camera=mLevelRendererPlayer.get();
    if(camera && camera->mDimension==&player->getDimension()) {
     auto const requested=settings();
     bool const noWeather=(requested->renderMask&passBit(RenderPass::Weather))!=0;
     bool const customWeather=requested->on(Feature::WeatherChanger)
          &&requested->weatherOptions.mode<=static_cast<unsigned>(WeatherMode::Snow);
     bool const customTime=requested->on(Feature::TimeChanger);
     // The native owner constructed this frame as mutable storage; renderLevel
     // takes a const view of it. Only touch the owned view for this render call.
     auto& views=const_cast<FrameRenderObject&>(frame).mViews.get();
     if(!visualFrameSeen.exchange(true,std::memory_order_relaxed))
         logGuiInfo("VISUAL2660 renderLevel frame hook reached");
     for(auto& view:views) {
      auto& data=view.mViewData.get();
      if(data.mIsShadowPass)continue;
      auto& sky=view.mSkyState.get();
      auto& weather=view.mWeatherState.get();
      if(customTime &&!data.mIsEndDimension) {
       float const day=player->getDimension().getTimeOfDay(kVisualDayTicks,0.f);
       if(std::isfinite(day)&&day>=0.f&&day<=1.f) {
        float const original=sky.mTimeOfDay;
        sky.mTimeOfDay=(std::isfinite(original)&&original>1.f &&original<=24000.f)
            ? static_cast<float>(kVisualDayTicks):day;
        sky.mStarBrightness=0.f;
        // mSkyDarken is the native 0..1 day brightness multiplier.
        float const darken=sky.mSkyDarken;
        if(std::isfinite(darken)&&darken>=0.f&&darken<=1.f)sky.mSkyDarken=1.f;
        // This frame's colors have already been calculated for the actual
        // night-time clock. The sun's angle changes above, but the atmosphere
        // must be adjusted independently. Only touch fresh gameplay-view data.
        if(player->getDimension().showSky() && !data.mCameraUnderLiquid) {
         auto daylightFloor=[](mce::Color& color,float r,float g,float b) {
          if(!std::isfinite(color.r)||!std::isfinite(color.g)||!std::isfinite(color.b))
              return;
          color.r=std::clamp(std::max(color.r,r),0.f,1.f);
          color.g=std::clamp(std::max(color.g,g),0.f,1.f);
          color.b=std::clamp(std::max(color.b,b),0.f,1.f);
         };
         daylightFloor(data.mSkyColor.get(),.46f,.66f,.94f);
         daylightFloor(data.mSkyZenithColor.get(),.37f,.61f,.94f);
         daylightFloor(data.mSkyHorizonColor.get(),.55f,.74f,.96f);
         daylightFloor(data.mFogColor.get(),.57f,.72f,.89f);
         float const brightness=data.mSkyBrightnessScalar;
         if(std::isfinite(brightness)&&brightness>=0.f&&brightness<=1.f)
             data.mSkyBrightnessScalar=1.f;
         float const ambient=sky.mAmbientBrightness;
         if(std::isfinite(ambient)&&ambient>=0.f&&ambient<=1.f)
             sky.mAmbientBrightness=std::max(ambient,.85f);
        }
        if(!visualTimeSeen.exchange(true,std::memory_order_relaxed))
            logGuiInfo("VISUAL2660 TimeChanger frame time overridden");
       }
      }
      if(noWeather||customWeather) {
       auto& params=weather.mParams.get();
       bool const rain=customWeather&&!noWeather
          &&requested->weatherOptions.mode==static_cast<unsigned>(WeatherMode::Rain);
       bool const snow=customWeather&&!noWeather
          &&requested->weatherOptions.mode==static_cast<unsigned>(WeatherMode::Snow);
       data.mDrawWeather=rain||snow;
       if(noWeather) {
        suppressWeatherState(weather);
        if(!visualNoRenderSeen.exchange(true,std::memory_order_relaxed))
            logGuiInfo("VISUAL2660 NoRender Weather frame suppressed");
       } else {
        weather.mDensityRain=rain?requested->weatherOptions.intensity:0.f;
        weather.mDensitySnow=snow?requested->weatherOptions.intensity:0.f;
        params[0].fAlpha=rain?1.f:0.f;
        params[1].fAlpha=snow?1.f:0.f;
        if(!rain&&!snow)sky.mFogLevel=0.f;
        if(!visualWeatherSeen.exchange(true,std::memory_order_relaxed))
            logGuiInfo("VISUAL2660 WeatherChanger frame override applied");
       }
      }
     }
    }
  }
 }
 origin(screen,frame);
}
LL_TYPE_INSTANCE_HOOK(FogHook,ll::memory::HookPriority::Normal,LevelRendererPlayer,
 &LevelRendererPlayer::$setupFog,void,ScreenContext& context,float intensity){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin(context,intensity);return;}
 origin(context,intensity);
 if(lease.admitted()&&(settings()->renderMask&passBit(RenderPass::Fog))!=0){
  mFogControl=glm::vec2{1000.0f,1000.0f};
  mCurrentDistanceFog->mStart=1000000.0f;mCurrentDistanceFog->mEnd=1000016.0f;
  mCurrentFogDensity->mMaxDensity=0.0f;
 }
}
LL_TYPE_INSTANCE_HOOK(FovHook,ll::memory::HookPriority::Normal,LevelRendererPlayer,
 &LevelRendererPlayer::getFov,float,float frameAlpha,bool enableVariableFOV){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return origin(frameAlpha,enableVariableFOV);
 float normal=origin(frameAlpha,enableVariableFOV);if(!lease.admitted()||!std::isfinite(normal))return normal;
 auto client=ll::service::getClientInstance();
 if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing||!client->getLocalPlayer())return normal;
 float factor=zoomFactor.load(std::memory_order_acquire);
 return std::clamp(normal*factor,5.0f,179.0f);
}
LL_TYPE_INSTANCE_HOOK(TickHook,ll::memory::HookPriority::Normal,LocalPlayer,
 &LocalPlayer::$normalTick,void){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted()){origin();return;}
 VisualTimeScope nonRendering(nullptr);
 VisualWeatherScope nonWeatherRendering(false);
 cameraCandidateBeforeTick(*this);
 origin();
 if(lease.admitted()){updateWorld(*this);tickFlexiblePlacement(*this);tickFastBlockPlacement(*this);cameraCandidateTick(*this);observeToolProtection(*this);serviceInventoryActions();serviceHandRestock();tickFastClicks(*this);}
}
// SDK-typed, local FreeCamera callbacks. Lamium v0.1.8 is a behavior reference;
// these handlers share Praxis admission/lease and do not change other owners.
LL_STATIC_HOOK(FreeCameraMovementHook,ll::memory::HookPriority::Normal,
 &ClientInputUpdateSystem::extractRawHIDInput,void,
 MovementAbilitiesComponent const& abilities,MoveInputComponent const& input,
 ActorDataFlagComponent const& flags,RawMoveInputComponent& raw,
 Optional<SneakingComponent const> sneaking,Optional<WasInWaterFlagComponent const> water){
 NativeHookLifetime::Lease lease(nativeLifetime);
 // Reuse this ALREADY INSTALLED camera input bridge; do not install a new
 // detour. Vanilla sees only a transient copy, not persistent synthetic bits.
 auto intent=lease.admitted()?gameplayToggleInputIntent():ToggleInputIntent{};
 bool applied=false;
 if(intent.sneak||intent.sprint) {
  auto client=ll::service::getClientInstance();
  if(client && ClientMoveInputHandler::getMoveInput(*client)==&input) {
   auto augmented=input;
   auto& bits=augmented.mRawInputState->mFlagValues;
   if(intent.sneak)bits->set(static_cast<std::size_t>(MoveInputState::Flag::SneakDown));
   if(intent.sprint)bits->set(static_cast<std::size_t>(MoveInputState::Flag::SprintDown));
   origin(abilities,augmented,flags,raw,sneaking,water);
   applied=true;
  }
 }
 if(!applied)origin(abilities,input,flags,raw,sneaking,water);
 // One-shot, non-spam diagnosis for game input admission. This reports that
 // the exact local MoveInputComponent was augmented before vanilla extraction.
 if(intent.sneak||intent.sprint) {
  static std::atomic_bool loggedMatch{},loggedMismatch{};
  if(applied && !loggedMatch.exchange(true))
   logGuiInfo("ToggleSneakSprint: native input extraction bridge MATCHED");
  if(!applied && !loggedMismatch.exchange(true))
   logGuiInfo("ToggleSneakSprint: input extraction bridge missing/mismatch");
 }
 if(lease.admitted())cameraCandidateConsumeMovement(input,raw);
}
LL_TYPE_INSTANCE_HOOK(FreeCameraDimensionHook,ll::memory::HookPriority::Normal,
 LevelRendererPlayer,&LevelRendererPlayer::$onWillChangeDimension,void,Player& player){
 NativeHookLifetime::Lease lease(nativeLifetime);
 if(lease.admitted())cameraCandidateWillChangeDimension(*this);
 origin(player);
}
struct HookState {int(*install)();bool active{};};
template<class Hook>HookState state(){
 return {[](){return Hook::hook();},false};
}
std::array freeCameraHookStates{state<FreeCameraMovementHook>(),state<FreeCameraDimensionHook>()};
// 26.60 still links the old sky render detour, but no longer exposes
// the non-positioned actor render overload. Keep the positioned actor hook.
std::array hookStates{state<ActorPositionedHook>(),state<BlockEntityHideHook>(),
 state<WorldOverlayHook>(),state<SkyHook>(),state<ParticlesHook>(),
 state<NoRenderWeatherViewHook>(),state<NoRenderRainSplashHook>(),
 state<FogHook>(),state<FovHook>(),state<TickHook>()};
}
void captureClient(IClientInstance& client){
 NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return;
 if(client.getClientInstanceState()!=ClientInstanceState::Playing){
  restoreEffectPatches();
  fullbrightReadiness.reset();inputAllowed=false;currentOptions=nullptr;return;
 }
 auto* player=client.getLocalPlayer();
 if(!player){restoreEffectPatches();fullbrightReadiness.reset();inputAllowed=false;currentOptions=nullptr;return;}
 auto screen=client.getTopScreenName();
 bool gameplay=screen=="hud_screen"||screen=="in_game_play_screen";
 fullbrightReadiness.observe(player,gameplay);
 inputAllowed.store(gameplay,std::memory_order_release);
 auto const& options=client.getOptions();
 if(currentOptions.load(std::memory_order_acquire)!=&options){
  std::lock_guard lock(optionHookMutex);
  bool gamma=hookOption(options,&IOptionRegistry::getGamma,&gammaHook,originalGamma);
  bool hud=hookOption(options,&IOptionRegistry::getHideHud,&hudHook,originalHud);
  bool flash=hookOption(options,&IOptionRegistry::getHideSkyFlash,&flashHook,originalFlash);
  currentOptions.store(&options,std::memory_order_release);
  if(gamma)availableFeatures.fetch_or(1u<<static_cast<unsigned>(Feature::Fullbright));
  if(hud)availableRenderPasses.fetch_or(passBit(RenderPass::Hud));
  else availableRenderPasses.fetch_and(static_cast<RenderMask>(~passBit(RenderPass::Hud)));
  if(!flash)availableRenderPasses.fetch_and(static_cast<RenderMask>(~passBit(RenderPass::Lightning)));
 }
 auto desired=frameEffectMask(settings()->renderMask,gameplay,runtimeReady.load());
 if(!updateEffectPatches(desired)){
  availableRenderPasses.fetch_and(static_cast<RenderMask>(~(passBit(RenderPass::PortalOverlay)|passBit(RenderPass::FireOverlay))));
  availableFeatures.fetch_and(~featureRegistryBit(unsigned(Feature::AntiEffect)));
 }
}
void resetFullbrightReadiness() noexcept{fullbrightReadiness.reset();}
bool installNative(){
 // Pin must succeed before *any* native registration; failure is a no-hook stop.
 if(!nativeLifetime.prepare(pinNativeModule))return false;
 nativeLifetime.registrationStarted();
 cameraCandidateStart();
 for(auto& h:hookStates){h.active=h.install()==0;if(!h.active){removeNative();return false;}}
 if(!installTimeChanger())logGuiInfo("TimeChanger native hooks unavailable; 26.60 frame visual path will be used when present");
 if(!installWeatherChanger())logGuiInfo("WeatherChanger 26.51 native Sunny path unavailable; 26.60 frame path will be used when present");
 bool const visualFrame2660Ready=RenderFrameVisual2660Hook::hook()==0;
 if(!visualFrame2660Ready)logGuiError("VISUAL2660 frame hook unavailable; weather/time fallback disabled");
  bool freeCameraReady=true;
  for(auto& hook:freeCameraHookStates){if(!hook.active)hook.active=hook.install()==0;freeCameraReady&=hook.active;}
  setFreeCameraMovementBackendAvailable(freeCameraReady);
  if(!freeCameraReady)logGuiInfo("FreeCamera optional SDK input/lifecycle backend unavailable; Freelook retained");
 if(!installToolProtection())logGuiInfo("ToolProtection/WeaponSwitch backend unavailable; existing features retained");
 bool const inventoryRequestsReady=inventory::installRequests();
 if(!inventoryRequestsReady)logGuiInfo("Inventory request observer unavailable; inventory utilities remain unavailable");
 // 26.60: HandRestock HUD reads are guarded by runtime admission and
 // a complete exception barrier; retain fail-closed behavior on error.
 if(inventoryRequestsReady && !installHandRestock())logGuiInfo("HandRestock backend unavailable; existing features retained");
 if(inventoryRequestsReady && !installInventoryActions())logGuiInfo("InventorySort/InventoryTransfer backend unavailable; existing features retained");
 if(inventoryRequestsReady && !installItemScrollerExtras())logGuiInfo("ItemScroller recipe/trade extension unavailable; existing sort/transfer retained");
 zoomMouseListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>(
  [](ll::event::MouseInputEvent& event){
   NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return;
   bool inventoryOwned=handleInventoryMouse(event.actionButtonId(),event.buttonData(),event.isCancelled());
   if(inventoryOwned)event.cancel();
   if(!inventoryOwned&&!event.isCancelled()&&event.actionButtonId()==MouseAction::ActionWheel){
    if(handleZoomScroll(event.buttonData()>0?1:event.buttonData()<0?-1:0))event.cancel();
   }else if(!inventoryOwned&&!event.isCancelled()&&event.actionButtonId()==MouseAction::ActionMoveRelative){
    auto dx=event.dx(),dy=event.dy();
    adjustZoomMouse(dx,dy);adjustFreelookMouse(dx,dy,event.isCancelled());event.dx()=dx;event.dy()=dy;
   }
   if(cameraCandidateMouse(event.actionButtonId(),event.buttonData(),event.dx(),event.dy(),event.isCancelled()))event.cancel();
  });
 if(!zoomMouseListener){removeNative();return false;}
 cameraKeyListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
  [](ll::event::KeyInputEvent& event){
   NativeHookLifetime::Lease lease(nativeLifetime);if(!lease.admitted())return;
   if(cameraCandidateKey(event.keyCode(),event.isDown(),event.isCancelled()))event.cancel();
  });
 if(!cameraKeyListener){removeNative();return false;}
 worldExitListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>(
  [](ll::event::ClientExitLevelEvent& event){NativeHookLifetime::Lease lease(nativeLifetime);if(lease.admitted()){cameraCandidateExit(event.self());resetWorldRuntime();}});
 if(!worldExitListener){removeNative();return false;}
 if(!startGameplayRuntime()){removeNative();return false;}
 if(!startInformationRuntime()){removeNative();return false;}
 if(!installShulkerPreview()){removeNative();return false;}
 if(!installFlexiblePlacement()){removeNative();return false;}
 installFastClicks();
 installFastBlockPlacement();
 RenderMask masks=passBit(RenderPass::Entities)|passBit(RenderPass::Items)|passBit(RenderPass::BlockEntities)|
  passBit(RenderPass::Hud)|passBit(RenderPass::Particles)|passBit(RenderPass::Sky)|
   passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Lightning);
 masks|=inspectEffectPatches();availableRenderPasses.store(masks,std::memory_order_release);
 if(masks&antiEffectPasses)availableFeatures.fetch_or(featureRegistryBit(unsigned(Feature::AntiEffect)));
 availableFeatures.fetch_or((1u<<static_cast<unsigned>(Feature::NoRender))
   |(1u<<static_cast<unsigned>(Feature::Zoom))
   |featureRegistryBit(static_cast<unsigned>(Feature::AutoTool))
   |featureRegistryBit(static_cast<unsigned>(Feature::EffectTimer))
   |featureRegistryBit(static_cast<unsigned>(Feature::AngleSnap))
   |featureRegistryBit(static_cast<unsigned>(Feature::ToggleSneakSprint))
   |featureRegistryBit(static_cast<unsigned>(Feature::DebugScreen))
   |featureRegistryBit(static_cast<unsigned>(Feature::Freelook)));
  if(freeCameraReady)availableFeatures.fetch_or(featureRegistryBit(static_cast<unsigned>(Feature::FreeCamera)));
 if(visualFrame2660Ready) {
  availableFeatures.fetch_or(featureRegistryBit(static_cast<unsigned>(Feature::WeatherChanger))
   |featureRegistryBit(static_cast<unsigned>(Feature::TimeChanger)),std::memory_order_acq_rel);
  logGuiInfo("VISUAL2660 frame-based WeatherChanger and TimeChanger backends installed");
 }
 if(!nativeLifetime.open())return false;
 if(!packs::installTitlePackSwitch())logGuiInfo("Resourcepack title apply backend unavailable; local proposal UI retained");
 if(!packs::installWorldPackSwitch())logGuiInfo("Resourcepack world apply backend unavailable; local proposal UI retained");
 return true;
}
bool beginNativeStop()noexcept{
 // Keep owner callbacks admitted until camera restoration and owned releases
 // drain. Closing the lease first would strand a wrong-thread FreeCamera stop.
 if(!cameraCandidateCleanup())return false;
 bool const worldPackStopped=packs::stopWorldPackSwitch();
 bool const titlePackStopped=packs::stopTitlePackSwitch();
 nativeLifetime.close();
 weatherNativeAdmission.store(false,std::memory_order_seq_cst);
 visualNativeAdmission.store(false,std::memory_order_seq_cst);
 runtimeReady=false;inputAllowed=false;
  setFreeCameraMovementBackendAvailable(false);
 // No blocking wait: stop may be called from an origin/GUI callback itself.
 // A non-quiescent stop retains all feature resources for process exit.
 return worldPackStopped&&titlePackStopped&&nativeLifetime.readers()==0
  &&weatherNativeReaders.load(std::memory_order_seq_cst)==0
  &&visualNativeReaders.load(std::memory_order_seq_cst)==0;
}
bool removeNative()noexcept{
 if(!beginNativeStop())return false;
 if(!cameraCandidateCleanup())return false;
 bool weatherChangerRemoved=removeWeatherChanger();
 bool timeChangerRemoved=removeTimeChanger();
 bool toolProtectionRemoved=removeToolProtection();
 bool handRestockRemoved=removeHandRestock();
 bool inventoryActionsRemoved=removeInventoryActions();
 bool itemScrollerRemoved=removeItemScrollerExtras();
 bool inventoryRequestsRemoved=inventory::removeRequests();
 bool flexiblePlacementRemoved=removeFlexiblePlacement();
 removeFastClicks();
 removeFastBlockPlacement();
 bool ok=shutdownGuiBackend();
 if(!timeChangerRemoved||!weatherChangerRemoved)ok=false;
 if(!toolProtectionRemoved)ok=false;
 if(!handRestockRemoved||!inventoryRequestsRemoved||!inventoryActionsRemoved||!itemScrollerRemoved)ok=false;
 if(!flexiblePlacementRemoved)ok=false;
 if(!stopInformationRuntime())ok=false;
 if(!stopGameplayRuntime())ok=false;
 availableFeatures.fetch_and(~(featureRegistryBit(unsigned(Feature::AutoTool))
   |featureRegistryBit(unsigned(Feature::EffectTimer))
   |featureRegistryBit(unsigned(Feature::AngleSnap))
   |featureRegistryBit(unsigned(Feature::ToggleSneakSprint))
   |featureRegistryBit(unsigned(Feature::DebugScreen))));
 if(!removeShulkerPreview())ok=false;
 if(zoomMouseListener){
  bool removed=ll::event::EventBus::getInstance().removeListener<ll::event::MouseInputEvent>(zoomMouseListener);
  if(!removed)ok=false;
  if(removed)zoomMouseListener.reset();
 }
 if(worldExitListener){
  bool removed=ll::event::EventBus::getInstance().removeListener<ll::event::ClientExitLevelEvent>(worldExitListener);
  if(!removed)ok=false;
  if(removed)worldExitListener.reset();
 }
 if(cameraKeyListener){
  bool removed=ll::event::EventBus::getInstance().removeListener<ll::event::KeyInputEvent>(cameraKeyListener);
  if(!removed)ok=false;
  if(removed)cameraKeyListener.reset();
 }
 if(!restoreEffectPatches())ok=false;
 // No SDK concurrent-origin retirement contract: do not unhook, clear the
 // registry, discard original function pointers, or free dynamic hook state.
 // Existing weather entries restore on the next native tick and erase only
 // at their destructor. DLL pinning happened before registration, not here.
 if(nativeLifetime.retained())ok=false;
 shutdownMap();
 if(!stopConfigStorage())ok=false;
  resetFullbrightReadiness();runtimeReady=false;inputAllowed=false;return ok;
 }
NativeHookLifetime& nativeHookLifetime() noexcept { return nativeLifetime; }
}
