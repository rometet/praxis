#include "resourcepacks/PassiveReloadObserver.h"
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
#include "core/NoRenderWeatherRegistry.h"
#include "platform/ToolProtection.h"
#include "platform/HandRestock.h"
#include "platform/InventoryRequest.h"
#include "platform/InventoryActions.h"
#include "render/Renderer.h"
#include "runtime/Runtime.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "runtime/InformationRuntime.h"
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
 if(context.mScreenContext.isDrawingUI||context.mScreenContext.isDrawingFirstPersonObjects)return false;
 auto* renderer=context.mClientInstance.getLevelRenderer();if(!renderer||!renderer->mLevelRendererPlayer)return false;
 auto const& queue=renderer->mLevelRendererPlayer->mActorRenderQueue.get();
 return queue.find(gsl::not_null<Actor*>(&actor))!=queue.end();
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
struct WeatherState {
 std::array<float,7> density{};
 std::array<float,7> alpha{};
};
NoRenderWeatherRegistry<WeatherRenderObject,WeatherState> trackedWeather;
std::atomic_bool weatherSuppressLogged{};

WeatherState captureWeatherState(WeatherRenderObject& object){
 WeatherState state{};
 state.density={
  object.mDensityRain,object.mDensitySnow,object.mDensityPlankton,
  object.mDensityRedSpores,object.mDensityBlueSpores,
  object.mDensityAsh,object.mDensityWhiteAsh};
 auto const& params=object.mParams.get();
 for(std::size_t i=0;i<state.alpha.size();++i)state.alpha[i]=params[i].fAlpha;
 return state;
}
void restoreWeatherState(WeatherRenderObject& object,WeatherState const& state){
 object.mDensityRain=state.density[0];object.mDensitySnow=state.density[1];
 object.mDensityPlankton=state.density[2];object.mDensityRedSpores=state.density[3];
 object.mDensityBlueSpores=state.density[4];object.mDensityAsh=state.density[5];
 object.mDensityWhiteAsh=state.density[6];
 auto& params=object.mParams.get();
 for(std::size_t i=0;i<state.alpha.size();++i)params[i].fAlpha=state.alpha[i];
}
void suppressWeatherState(WeatherRenderObject& object){
 object.mDensityRain=0.0f;object.mDensitySnow=0.0f;object.mDensityPlankton=0.0f;
 object.mDensityRedSpores=0.0f;object.mDensityBlueSpores=0.0f;
 object.mDensityAsh=0.0f;object.mDensityWhiteAsh=0.0f;
 auto& params=object.mParams.get();
 for(auto& param:params)param.fAlpha=0.0f;
}
LL_TYPE_INSTANCE_HOOK(WeatherHook,ll::memory::HookPriority::Normal,WeatherRenderObject,
 &WeatherRenderObject::$ctor,void*,WeatherRenderObject&& other){
 NativeHookLifetime::Lease lease(nativeLifetime);
 // A tracked source may still contain NoRender output. Restore its current
 // saved native values before moving it, including after admission closes.
 trackedWeather.restoreSource(other,restoreWeatherState);
 void* result=origin(std::move(other));
 if(result==this)trackedWeather.constructed(*this,lease.admitted(),
  lease.admitted()&&(settings()->renderMask&passBit(RenderPass::Weather))!=0,
  captureWeatherState,suppressWeatherState);
 return result;
}
LL_TYPE_INSTANCE_HOOK(WeatherDtorHook,ll::memory::HookPriority::Normal,WeatherRenderObject,
 &WeatherRenderObject::$dtor,void){
 NativeHookLifetime::Lease lease(nativeLifetime);
 trackedWeather.destroyed(*this);
 origin();
}
LL_TYPE_INSTANCE_HOOK(WeatherTickHook,ll::memory::HookPriority::Normal,LevelRendererPlayer,
 &LevelRendererPlayer::$tickRain,void){
 NativeHookLifetime::Lease lease(nativeLifetime);
 // The legacy collection belongs exclusively to NoRender. No primary-camera
 // scalar may authorize WeatherChanger writes to any of these objects.
 trackedWeather.beforeTick(restoreWeatherState);
 origin();
 bool const noRenderWeather=lease.admitted()
  &&(settings()->renderMask&passBit(RenderPass::Weather))!=0;
 trackedWeather.afterTick(noRenderWeather,captureWeatherState,restoreWeatherState,suppressWeatherState);
 if(noRenderWeather){
  mCameraInRain=false;
  if(!weatherSuppressLogged.exchange(true,std::memory_order_acq_rel))
   logGuiInfo("NoRender Weather dynamic suppression active");
 }
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
 if(lease.admitted()){updateWorld(*this);cameraCandidateTick(*this);observeToolProtection(*this);serviceInventoryActions();serviceHandRestock();}
}
// SDK-typed, local FreeCamera callbacks. Lamium v0.1.8 is a behavior reference;
// these handlers share Praxis admission/lease and do not change other owners.
LL_STATIC_HOOK(FreeCameraMovementHook,ll::memory::HookPriority::Normal,
 &ClientInputUpdateSystem::extractRawHIDInput,void,
 MovementAbilitiesComponent const& abilities,MoveInputComponent const& input,
 ActorDataFlagComponent const& flags,RawMoveInputComponent& raw,
 Optional<SneakingComponent const> sneaking,Optional<WasInWaterFlagComponent const> water){
 NativeHookLifetime::Lease lease(nativeLifetime);
 origin(abilities,input,flags,raw,sneaking,water);
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
std::array hookStates{state<ActorHook>(),state<ActorPositionedHook>(),state<BlockEntityHideHook>(),
 state<WorldOverlayHook>(),state<SkyHook>(),state<ParticlesHook>(),state<WeatherDtorHook>(),
 state<WeatherTickHook>(),state<WeatherHook>(),state<FogHook>(),state<FovHook>(),state<TickHook>()};
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
 if(!installTimeChanger()){removeNative();return false;}
 if(!installWeatherChanger()){removeNative();return false;}
  bool freeCameraReady=true;
  for(auto& hook:freeCameraHookStates){if(!hook.active)hook.active=hook.install()==0;freeCameraReady&=hook.active;}
  setFreeCameraMovementBackendAvailable(freeCameraReady);
  if(!freeCameraReady)logGuiInfo("FreeCamera optional SDK input/lifecycle backend unavailable; Freelook retained");
 if(!installToolProtection())logGuiInfo("ToolProtection/WeaponSwitch backend unavailable; existing features retained");
 bool const inventoryRequestsReady=inventory::installRequests();
 if(!inventoryRequestsReady)logGuiInfo("Inventory request observer unavailable; inventory utilities remain unavailable");
 if(inventoryRequestsReady && !installHandRestock())logGuiInfo("HandRestock backend unavailable; existing features retained");
 if(inventoryRequestsReady && !installInventoryActions())logGuiInfo("InventorySort/InventoryTransfer backend unavailable; existing features retained");
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
 if(!startInformationRuntime()){removeNative();return false;}
 if(!installShulkerPreview()){removeNative();return false;}
 RenderMask masks=passBit(RenderPass::Entities)|passBit(RenderPass::Items)|passBit(RenderPass::BlockEntities)|
  passBit(RenderPass::Hud)|passBit(RenderPass::Particles)|passBit(RenderPass::Sky)|passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Lightning);
 masks|=inspectEffectPatches();availableRenderPasses.store(masks,std::memory_order_release);
 if(masks&antiEffectPasses)availableFeatures.fetch_or(featureRegistryBit(unsigned(Feature::AntiEffect)));
 availableFeatures.fetch_or((1u<<static_cast<unsigned>(Feature::NoRender))
   |(1u<<static_cast<unsigned>(Feature::Zoom))
   |featureRegistryBit(static_cast<unsigned>(Feature::Freelook)));
  if(freeCameraReady)availableFeatures.fetch_or(featureRegistryBit(static_cast<unsigned>(Feature::FreeCamera)));
 if(!nativeLifetime.open())return false;
 packs::installPassiveReloadObserver();
 return true;
}
bool beginNativeStop()noexcept{
 // Keep owner callbacks admitted until camera restoration and owned releases
 // drain. Closing the lease first would strand a wrong-thread FreeCamera stop.
 if(!cameraCandidateCleanup())return false;
 bool const passiveObserverStopped=packs::stopPassiveReloadObserver();
 nativeLifetime.close();
 weatherNativeAdmission.store(false,std::memory_order_seq_cst);
 visualNativeAdmission.store(false,std::memory_order_seq_cst);
 runtimeReady=false;inputAllowed=false;
  setFreeCameraMovementBackendAvailable(false);
 // No blocking wait: stop may be called from an origin/GUI callback itself.
 // A non-quiescent stop retains all feature resources for process exit.
 return passiveObserverStopped&&nativeLifetime.readers()==0
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
 bool inventoryRequestsRemoved=inventory::removeRequests();
 bool ok=shutdownGuiBackend();
 if(!timeChangerRemoved||!weatherChangerRemoved)ok=false;
 if(!toolProtectionRemoved)ok=false;
 if(!handRestockRemoved||!inventoryRequestsRemoved||!inventoryActionsRemoved)ok=false;
 if(!stopInformationRuntime())ok=false;
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
}
