#include "platform/TimeChanger.h"
#include "platform/CameraCandidate.h"
#include "core/TimeChangerPolicy.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/world/level/dimension/Dimension.h"
#include <array>
#include <memory>

namespace praxis::companion {
namespace {

// The SDK macro's target/origin slots are private. These two existing hooks
// retain the same public hookEx contract in caller-owned typed storage instead.
template<class Method> struct RecordedSdkHook;
template<class Return,class Type,class... Args>
struct RecordedSdkHook<Return(Type::*)(Args...)> {
    using Method=Return(Type::*)(Args...);
    ll::memory::FuncPtr target{};
    Method continuation{};
    template<auto Target,auto Detour> int install() {
        static_assert(std::is_same_v<decltype(Target),Method>);
        static_assert(!ll::memory::virtualDetector<Method,Target>());
        if(!target) target=ll::memory::resolveIdentifier<Method>(Target);
        return ll::memory::hookEx(target,ll::memory::toFuncPtr(Detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&continuation),ll::memory::HookPriority::High,
            ll::memory::SaveNone,true);
    }
    Return origin(Type* self,Args... args) { return (self->*continuation)(std::forward<Args>(args)...); }
};
RecordedSdkHook<decltype(&LevelRendererPlayer::$preRenderUpdate)> sdkPreRender;
RecordedSdkHook<decltype(&BaseLightTextureImageBuilder::$createBaseLightTextureData)> sdkLightData;

struct RenderEligibility {
    std::uint64_t generation{};
    Dimension const* rendererDimension{};
    Dimension const* playerDimension{};
    bool primary{};
    bool playing{};
    bool worldScreen{};
};

RenderEligibility eligibility(IClientInstance* target, LevelRendererCamera const* camera,
                              ScreenContext const* screen = nullptr) {
    RenderEligibility result;
    // Capture before SDK traversal so a retired renderer/player snapshot is
    // never stamped with a later world's generation when opening the scope.
    result.generation = worldGeneration.load(std::memory_order_acquire);
    auto client = ll::service::getClientInstance();
    if (!client || !target || static_cast<IClientInstance*>(&*client) != target) return result;
    result.playing = client->getClientInstanceState() == ClientInstanceState::Playing;
    if (!result.playing) return result;
    auto* renderer = client->getLevelRenderer();
    if (!renderer || !renderer->mLevelRendererPlayer) return result;
    auto* primaryCamera = renderer->mLevelRendererPlayer.get();
    if (camera && camera != primaryCamera) return result;
    result.primary = true;
    auto* player = client->getLocalPlayer();
    if (!player) return result;
    result.playerDimension = &player->getDimension();
    result.rendererDimension = primaryCamera->mDimension;
    result.worldScreen = !screen || (!screen->isDrawingUI && !screen->isDrawingFirstPersonObjects);
    return result;
}

template<class Original>
decltype(auto) renderWithDay(RenderEligibility const& e, Original&& original) {
    return visualRenderCallback(e.rendererDimension, e.playerDimension,
        e.primary, e.playing, e.worldScreen, std::forward<Original>(original), e.generation);
}

// Snapshot generation, not renderSky's already-generated const ViewRenderObject.
struct VisualDayPreRenderHook : public LevelRendererPlayer {
    void detour(ScreenContext& screen,LevelRenderPreRenderUpdateParameters& parameters);
    void origin(ScreenContext& screen,LevelRenderPreRenderUpdateParameters& parameters) {
        sdkPreRender.origin(this,screen,parameters);
    }
    static int hook() {
        return sdkPreRender.install<&LevelRendererPlayer::$preRenderUpdate,&VisualDayPreRenderHook::detour>();
    }
};
void VisualDayPreRenderHook::detour(ScreenContext& screen,LevelRenderPreRenderUpdateParameters& parameters) {
    VisualNativeLease lease;
    if(visualNativeAdmission.load(std::memory_order_seq_cst))cameraCandidateBeforeRender(*this);
    if (!visualDayEnabled()) origin(screen, parameters);
    else renderWithDay(eligibility(&mLevelRenderer.mClientInstance, this, &screen),
        [&] { origin(screen, parameters); });
    if(visualNativeAdmission.load(std::memory_order_seq_cst))cameraCandidateRender(*this);
}

LL_TYPE_INSTANCE_HOOK(VisualDayLevelRenderHook, ll::memory::HookPriority::High,
    LevelRenderer, &LevelRenderer::renderLevel, void,
    ScreenContext& screen, FrameRenderObject const& object) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) { origin(screen, object); return; }
    auto e = eligibility(&mClientInstance, nullptr, &screen);
    auto client = ll::service::getClientInstance();
    if (!client || client->getLevelRenderer() != this) e.primary = false;
    renderWithDay(e, [&] { origin(screen, object); });
}

LL_TYPE_INSTANCE_HOOK(VisualDayCalculationHook, ll::memory::HookPriority::High,
    Dimension, &Dimension::$getTimeOfDay, float, int actualTime, float partialTick) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) return origin(actualTime, partialTick);
    return visualTimeCalculation(this, actualTime, partialTick,
        [&](int time, float alpha) { return origin(time, alpha); });
}

using ClientVisualTimeMethod = float (Dimension::*)(float) const;
LL_TYPE_INSTANCE_HOOK(VisualDayClientCalculationHook, ll::memory::HookPriority::High,
    Dimension, static_cast<ClientVisualTimeMethod>(&Dimension::getTimeOfDay), float,
    float partialTick) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) return origin(partialTick);
    if (visualTimeContext.dimension == this
        && visualTimeContext.generation == worldGeneration.load(std::memory_order_acquire)
        && runtimeReady.load(std::memory_order_acquire)
        && visualNativeAdmission.load(std::memory_order_seq_cst)
        && settings()->on(Feature::TimeChanger)) {
        // Call the native dimension-specific celestial calculation. Returning
        // a raw tick count here would violate the float-angle API contract.
        return getTimeOfDay(kVisualDayTicks, 0.0f);
    }
    return origin(partialTick);
}

// A specific visual consumer receives a native celestial angle rather than a
// broad graphics-refresh scope that could include unrelated client callbacks.
LL_TYPE_INSTANCE_HOOK(VisualDayAtmosphereHook, ll::memory::HookPriority::High,
    MinecraftGame, &MinecraftGame::_updateBiomeBlendingAndAtmosphericScattering, void,
    float actualTimeOfDay, float intensityMultiplier, DimensionType dimensionId) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) { origin(actualTimeOfDay, intensityMultiplier, dimensionId); return; }
    auto client = ll::service::getClientInstance();
    if (!client || static_cast<IMinecraftGame*>(this) != &client->getMinecraftGame_DEPRECATED()) {
        origin(actualTimeOfDay, intensityMultiplier, dimensionId);
        return;
    }
    auto e = eligibility(client ? static_cast<IClientInstance*>(&*client) : nullptr, nullptr);
    auto currentSnapshot = [&] {
        return e.generation == worldGeneration.load(std::memory_order_acquire)
            && runtimeReady.load(std::memory_order_acquire)
            && visualNativeAdmission.load(std::memory_order_seq_cst)
            && settings()->on(Feature::TimeChanger)
            && e.primary && e.playing && e.playerDimension
            && e.rendererDimension == e.playerDimension;
    };
    if (currentSnapshot() && e.playerDimension->mId.get() == dimensionId) {
        auto const dayAngle = e.playerDimension->getTimeOfDay(kVisualDayTicks, 0.0f);
        origin(currentSnapshot() ? dayAngle : actualTimeOfDay, intensityMultiplier, dimensionId);
    } else {
        origin(actualTimeOfDay, intensityMultiplier, dimensionId);
    }
}

struct VisualDayLightDataHook : public BaseLightTextureImageBuilder {
    std::unique_ptr<BaseLightData> detour(IClientInstance* client,BaseLightData const& current);
    std::unique_ptr<BaseLightData> origin(IClientInstance* client,BaseLightData const& current) {
        return sdkLightData.origin(this,client,current);
    }
    static int hook() {
        return sdkLightData.install<&BaseLightTextureImageBuilder::$createBaseLightTextureData,&VisualDayLightDataHook::detour>();
    }
};
std::unique_ptr<BaseLightData> VisualDayLightDataHook::detour(IClientInstance* client,BaseLightData const& current) {
    VisualNativeLease lease;
    if (!visualDayEnabled()) return origin(client, current);
    auto e = eligibility(client, nullptr);
    if (mDimension != e.playerDimension) e.primary = false;
    // Keep native gamma, weather, darkness, night vision and underwater data.
    // The native factory sees only a scoped visual clock; no field is guessed.
    return renderWithDay(e, [&] { return origin(client, current); });
}

struct HookState { int (*install)(); bool active{}; };
template<class Hook> HookState hookState() {
    return {[] { return Hook::hook(); }, false};
}
std::array hooks{hookState<VisualDayPreRenderHook>(), hookState<VisualDayLevelRenderHook>(),
    hookState<VisualDayAtmosphereHook>(), hookState<VisualDayCalculationHook>(),
    hookState<VisualDayClientCalculationHook>(), hookState<VisualDayLightDataHook>()};
TimeChangerSdkRegistration sdkRegistration;
template<class Method>
TimeChangerSdkBindingRecord sdkBindingEvidence(TimeChangerSdkBinding binding,
    RecordedSdkHook<Method> const& registration,Method typed) noexcept {
    bool installed=true;
    for(auto const& hook:hooks) installed=installed && hook.active;
    bool const admitted=visualNativeAdmission.load(std::memory_order_seq_cst)
        && (availableFeatures.load(std::memory_order_acquire)
            & (1u<<static_cast<unsigned>(Feature::TimeChanger)))!=0;
    return sdkRegistration.snapshot(binding,reinterpret_cast<std::uintptr_t>(registration.target),
        reinterpret_cast<std::uintptr_t>(ll::memory::toFuncPtr(typed)),
        reinterpret_cast<std::uintptr_t>(ll::memory::toFuncPtr(registration.continuation)),installed,admitted);
}
}

TimeChangerSdkBindingRecord timeChangerSdkBindingEvidence(TimeChangerSdkBinding binding) noexcept {
    if(binding==TimeChangerSdkBinding::LightData)
        return sdkBindingEvidence(binding,sdkLightData,&BaseLightTextureImageBuilder::$createBaseLightTextureData);
    if(binding==TimeChangerSdkBinding::PreRender)
        return sdkBindingEvidence(binding,sdkPreRender,&LevelRendererPlayer::$preRenderUpdate);
    return {};
}
bool installTimeChanger() {
    sdkRegistration.begin();
    visualNativeAdmission.store(false, std::memory_order_seq_cst);
    for (auto& hook : hooks) {
        if (hook.active) continue;
        hook.active = hook.install() == 0;
        if (!hook.active) { (void)removeTimeChanger(); return false; }
    }
    sdkRegistration.capture(TimeChangerSdkBinding::LightData,
        reinterpret_cast<std::uintptr_t>(sdkLightData.target),true);
    sdkRegistration.capture(TimeChangerSdkBinding::PreRender,
        reinterpret_cast<std::uintptr_t>(sdkPreRender.target),true);
    sdkRegistration.finish();
    availableFeatures.fetch_or(1u << static_cast<unsigned>(Feature::TimeChanger), std::memory_order_acq_rel);
    visualNativeAdmission.store(true, std::memory_order_seq_cst);
    return true;
}

bool removeTimeChanger() noexcept {
    sdkRegistration.retire();
    visualNativeAdmission.store(false, std::memory_order_seq_cst);
    availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::TimeChanger)), std::memory_order_acq_rel);
    // PreLoader's concurrent origin/trampoline retirement contract is not
    // public. Keep installed hooks as native passthroughs and fail cleanup so
    // CompanionMod's existing F1 path pins this DLL until process exit.
    // Ordinary feature OFF/world exit do not use this loader retirement path.
    for (auto const& hook : hooks) if (hook.active) return false;
    return visualNativeReaders.load(std::memory_order_seq_cst) == 0;
}
} // namespace praxis::companion
