#include "platform/Native.h"
#include "platform/EffectPatches.h"
#include "core/Settings.h"
#include "core/FullbrightPolicy.h"
#include "render/Renderer.h"
#include "runtime/Runtime.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
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
#include <vector>

namespace praxis::companion {
namespace {
std::atomic<IOptionRegistry const*> currentOptions{};
std::mutex optionHookMutex;
struct DynamicHook {void* target{};void* detour{};bool active=true;};
std::vector<DynamicHook> dynamicHooks;
using GammaFn=float(*)(IOptionRegistry const*);
using BoolFn=bool(*)(IOptionRegistry const*);
GammaFn originalGamma{};BoolFn originalHud{},originalFlash{};
FullbrightReadiness fullbrightReadiness;
ll::event::ListenerPtr zoomMouseListener;
ll::event::ListenerPtr worldExitListener;
float gammaHook(IOptionRegistry const* self){
 return fullbrightGamma([&]{return originalGamma(self);},[&]() -> std::optional<float> {
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
 auto s=settings();return originalHud(self)||(self==currentOptions.load(std::memory_order_acquire)&&(s->renderMask&passBit(RenderPass::Hud))!=0);
}
bool flashHook(IOptionRegistry const* self){
 auto s=settings();return originalFlash(self)||(self==currentOptions.load(std::memory_order_acquire)&&(s->renderMask&passBit(RenderPass::Lightning))!=0);
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
 if(!hideActor(context,actor))origin(context,actor,ignoreLighting);
}
using ActorRenderPositioned=void(ActorRenderDispatcher::*)(BaseActorRenderContext&,Actor&,Vec3 const&,Vec3 const&,Vec2 const&,bool);
LL_TYPE_INSTANCE_HOOK(ActorPositionedHook,ll::memory::HookPriority::Lowest,ActorRenderDispatcher,
 static_cast<ActorRenderPositioned>(&ActorRenderDispatcher::render),void,
 BaseActorRenderContext& context,Actor& actor,Vec3 const& target,Vec3 const& position,Vec2 const& rotation,bool ignoreLighting){
 if(!hideActor(context,actor))origin(context,actor,target,position,rotation,ignoreLighting);
}
LL_TYPE_INSTANCE_HOOK(BlockEntityHideHook,ll::memory::HookPriority::Lowest,LevelRendererPlayer,
 &LevelRendererPlayer::$renderBlockEntities,void,BaseActorRenderContext& context,bool alpha){
 if((settings()->renderMask&passBit(RenderPass::BlockEntities))==0)origin(context,alpha);
}
LL_TYPE_INSTANCE_HOOK(WorldOverlayHook,ll::memory::HookPriority::High,LevelRendererPlayer,
 &LevelRendererPlayer::$renderBlockEntities,void,BaseActorRenderContext& context,bool alpha){
 captureClient(context.mClientInstance);origin(context,alpha);drawWorld(context,alpha);
}
LL_TYPE_INSTANCE_HOOK(SkyHook,ll::memory::HookPriority::Lowest,LevelRendererCamera,
 &LevelRendererCamera::renderSky,void,ScreenContext& context,ViewRenderObject const& object,
 BaseSceneDirectionalLightRenderData const& light){
 if((settings()->renderMask&passBit(RenderPass::Sky))==0)origin(context,object,light);
}
LL_TYPE_INSTANCE_HOOK(ParticlesHook,ll::memory::HookPriority::Lowest,ParticleRenderer,
 &ParticleRenderer::renderParticles,void,ScreenContext& context,Vec3 const& target,Vec3 const& position,ParticleRenderData const& data){
 if(context.isDrawingUI||(settings()->renderMask&passBit(RenderPass::Particles))==0)origin(context,target,position,data);
}
LL_TYPE_INSTANCE_HOOK(WeatherHook,ll::memory::HookPriority::Normal,WeatherRenderObject,
 &WeatherRenderObject::$ctor,void*,WeatherRenderObject&& other){
 void* result=origin(std::move(other));
 if((settings()->renderMask&passBit(RenderPass::Weather))!=0){
  mDensityRain=0.0f;mDensitySnow=0.0f;
  mParams.get()[0].fAlpha=0.0f;mParams.get()[1].fAlpha=0.0f;
 }
 return result;
}
LL_TYPE_INSTANCE_HOOK(FogHook,ll::memory::HookPriority::Normal,LevelRendererPlayer,
 &LevelRendererPlayer::$setupFog,void,ScreenContext& context,float intensity){
 origin(context,intensity);
 if((settings()->renderMask&passBit(RenderPass::Fog))!=0){
  mFogControl=glm::vec2{1000.0f,1000.0f};
  mCurrentDistanceFog->mStart=1000000.0f;mCurrentDistanceFog->mEnd=1000016.0f;
  mCurrentFogDensity->mMaxDensity=0.0f;
 }
}
LL_TYPE_INSTANCE_HOOK(FovHook,ll::memory::HookPriority::Normal,LevelRendererPlayer,
 &LevelRendererPlayer::getFov,float,float frameAlpha,bool enableVariableFOV){
 float normal=origin(frameAlpha,enableVariableFOV);if(!std::isfinite(normal))return normal;
 auto client=ll::service::getClientInstance();
 if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing||!client->getLocalPlayer())return normal;
 float factor=zoomFactor.load(std::memory_order_acquire);
 return std::clamp(normal*factor,5.0f,179.0f);
}
LL_TYPE_INSTANCE_HOOK(TickHook,ll::memory::HookPriority::Normal,LocalPlayer,
 &LocalPlayer::$normalTick,void){
 origin();updateWorld(*this);
}
struct HookState {int(*install)();bool(*remove)();bool active{};};
template<class Hook>HookState state(){return {[](){return Hook::hook();},[](){return Hook::unhook();},false};}
std::array hookStates{state<ActorHook>(),state<ActorPositionedHook>(),state<BlockEntityHideHook>(),
 state<WorldOverlayHook>(),state<SkyHook>(),state<ParticlesHook>(),state<WeatherHook>(),state<FogHook>(),state<FovHook>(),state<TickHook>()};
}
void captureClient(IClientInstance& client){
 if(client.getClientInstanceState()!=ClientInstanceState::Playing){
  fullbrightReadiness.reset();inputAllowed=false;currentOptions=nullptr;return;
 }
 auto* player=client.getLocalPlayer();
 if(!player){fullbrightReadiness.reset();inputAllowed=false;currentOptions=nullptr;return;}
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
  if(!flash)availableRenderPasses.fetch_and(static_cast<RenderMask>(~passBit(RenderPass::Lightning)));
 }
 auto desired=settings()->renderMask;
 if(!updateEffectPatches(desired)){
  availableRenderPasses.fetch_and(static_cast<RenderMask>(~(passBit(RenderPass::PortalOverlay)|passBit(RenderPass::FireOverlay))));
 }
}
void resetFullbrightReadiness() noexcept{fullbrightReadiness.reset();}
bool installNative(){
 for(auto& h:hookStates){h.active=h.install()==0;if(!h.active){removeNative();return false;}}
 zoomMouseListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>(
  [](ll::event::MouseInputEvent& event){
   if(event.actionButtonId()==MouseAction::ActionWheel){
    if(handleZoomScroll(event.buttonData()>0?1:event.buttonData()<0?-1:0))event.cancel();
   }else if(event.actionButtonId()==MouseAction::ActionMoveRelative){
    auto dx=event.dx(),dy=event.dy();
    adjustZoomMouse(dx,dy);event.dx()=dx;event.dy()=dy;
   }
  });
 if(!zoomMouseListener){removeNative();return false;}
 worldExitListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>(
  [](ll::event::ClientExitLevelEvent&){resetWorldRuntime();});
 if(!worldExitListener){removeNative();return false;}
 if(!installShulkerPreview()){removeNative();return false;}
 RenderMask masks=passBit(RenderPass::Entities)|passBit(RenderPass::Items)|passBit(RenderPass::BlockEntities)|
  passBit(RenderPass::Particles)|passBit(RenderPass::Sky)|passBit(RenderPass::Weather)|passBit(RenderPass::Fog)|passBit(RenderPass::Lightning);
 masks|=inspectEffectPatches();availableRenderPasses.store(masks,std::memory_order_release);
 availableFeatures.fetch_or((1u<<static_cast<unsigned>(Feature::NoRender))
   |(1u<<static_cast<unsigned>(Feature::Zoom)));
 return true;
}
bool removeNative()noexcept{
 bool ok=shutdownGuiBackend();
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
 if(!restoreEffectPatches())ok=false;
 for(auto i=hookStates.rbegin();i!=hookStates.rend();++i)if(i->active){if(!i->remove())ok=false;else i->active=false;}
 for(auto i=dynamicHooks.rbegin();i!=dynamicHooks.rend();++i)if(i->active){if(!ll::memory::unhook(i->target,i->detour))ok=false;else i->active=false;}
 if(ok){dynamicHooks.clear();originalGamma=nullptr;originalHud=nullptr;originalFlash=nullptr;currentOptions=nullptr;}
 resetFullbrightReadiness();runtimeReady=false;inputAllowed=false;return ok;
}
}
