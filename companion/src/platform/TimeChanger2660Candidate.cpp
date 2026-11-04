#include "platform/TimeChanger.h"
#include "core/TimeChangerPolicy.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/world/level/dimension/Dimension.h"

// Experimental 26.60-only render-scope clock using two APIs that exist in
// the 26.60 SDK. Do not advertise this as a complete time changer until
// sky, lighting and atmosphere are tested inside a real world.
namespace praxis::companion {
namespace {
LL_TYPE_INSTANCE_HOOK(
    ScopedDimensionClockHook, ll::memory::HookPriority::High,
    Dimension, &Dimension::getTimeOfDay, float, int ticks, float partialTick
) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) return origin(ticks,partialTick);
    return visualTimeCalculation(this,ticks,partialTick,
        [&](int visualTicks,float alpha){return origin(visualTicks,alpha);});
}

LL_TYPE_INSTANCE_HOOK(
    ScopedLevelRenderHook, ll::memory::HookPriority::High,
    LevelRenderer, &LevelRenderer::renderLevel, void,
    ScreenContext& screen, FrameRenderObject const& object
) {
    VisualNativeLease lease;
    if(!visualDayEnabled()) {origin(screen,object);return;}
    auto client=ll::service::getClientInstance();
    if(!client || client->getLevelRenderer()!=this
       ||client->getClientInstanceState()!=ClientInstanceState::Playing
       ||!client->getLocalPlayer() || !mLevelRendererPlayer
       ||screen.isDrawingUI || screen.isDrawingFirstPersonObjects){
        origin(screen,object);return;
    }
    auto const* playerDimension=&client->getLocalPlayer()->getDimension();
    auto const* rendererDimension=mLevelRendererPlayer->mDimension;
    visualRenderCallback(rendererDimension,playerDimension,true,true,true,
        [&]{origin(screen,object);});
}
bool clockHookInstalled{}, renderHookInstalled{};
}
TimeChangerSdkBindingRecord timeChangerSdkBindingEvidence(TimeChangerSdkBinding) noexcept {return {};}
bool installTimeChanger() {
    // Native callers may retain trampolines until process exit. We pin and
    // do not attempt hot unloading of a successfully installed hook.
    if(!clockHookInstalled)clockHookInstalled=ScopedDimensionClockHook::hook()==0;
    if(!clockHookInstalled)return false;
    if(!renderHookInstalled)renderHookInstalled=ScopedLevelRenderHook::hook()==0;
    if(!renderHookInstalled)return false;
    // Do not advertise until validated for sky/atmosphere/light in world.
    visualNativeAdmission.store(false,std::memory_order_seq_cst);
    return false;
}
bool removeTimeChanger() noexcept {
    visualNativeAdmission.store(false,std::memory_order_seq_cst);
    // Hooks were registered but are pass-through when admission is false.
    return !(clockHookInstalled||renderHookInstalled);
}
}