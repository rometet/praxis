#include "platform/CameraCandidate.h"
#include "platform/CameraRigTransaction.h"
#include "platform/FreeCameraOffsetTransaction.h"
#include "platform/FreeCameraInputPolicy.h"
#include "mc/client/input/ClientMoveInputHandler.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "core/CameraFlightDirection.h"
#include "core/CameraInputPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "runtime/Runtime.h"
#include "map/MapRuntime.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/ecs/gamerefs_entity/EntityRegistry.h"
#include "mc/deps/minecraft_camera/CameraRegistry.h"
#include "mc/deps/minecraft_camera/CameraClientInstanceComponent.h"
#include "mc/deps/minecraft_camera/ICameraClientInstance.h"
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraDirectLookComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOrbitComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOffsetComponent.h"
#include "mc/deps/minecraft_camera/components/CameraUsageComponent.h"
#include "mc/deps/vanilla_camera/components/UpdatePlayerFromCameraComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/entity/components/ActorHeadRotationComponent.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/deps/input/MouseAction.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>

namespace praxis::companion {
namespace {
using Direct=MinecraftCamera::CameraDirectLookComponent;
using Offset=MinecraftCamera::CameraOffsetComponent;
using Link=VanillaCamera::UpdatePlayerFromCameraComponent;
std::mutex cameraMutex;
CameraCandidateOptions lookOptions, freeOptions;
CameraActivation lookActivation, freeActivation;
CameraCandidateMotion motion;
std::atomic_bool mouseAdmission{}, stopRequested{}, retiring{}, freeCameraMovementReady{};
std::atomic<unsigned> suppressedButtons{};
CameraInputLedger inputLedger;
DWORD cameraThread{};
int pendingDx{},pendingDy{},pendingWheel{};
CameraFlightDirection flightDirection;
std::optional<std::array<float,2>> nativeViewAngles;
std::uint64_t sessionEpoch{};
std::atomic<char const*> status{"OFF"};
std::uint64_t previousMovementAt{};
struct Saved {
    WeakEntityRef rig, actor;
    Link::LookMode lookMode{};
    DetachedCameraMode mode{};
    std::uint64_t generation{};
    int dimension{};
    float yaw{},pitch{},bodyYaw{},bodyPitch{};
    float headYaw{},headPrevious{};
    bool hasDirect{},hasOrbit{};
    float orbitAzimuth{},orbitPolar{},idealAzimuth{},idealPolar{};
    glm::vec3 offset{},anchorEye{};
    std::optional<FreeCameraOffsetSnapshot> freeOffset;
};
std::optional<Saved> saved;
// Diagnostic-only: fixed tuple storage reserves room for both modes' evidence.
// No activation, actor/server state or camera ownership is changed here.
enum class CameraDiagnosticMode : unsigned { Common, Freelook, FreeCamera };
enum class CameraDiagnosticPhase : unsigned {
    Start, TickArrival, TickEligible, TickIneligible, TickDesired,
    LookDown, LookUp, FreeDown, FreeUp, LookAccepted, FreeAccepted,
    LookRejected, FreeRejected, Rig, BeginRejected, BeginAngles, BeginBounds,
    BeginSuccess, MovementNeutral, PrimaryMismatch, ThreadMismatch,
    KeyException, TickException, Exit, Cleanup,
    FreelookActivationTick, FreeCameraActivationTick
};
static_assert(static_cast<unsigned>(CameraDiagnosticPhase::FreeCameraActivationTick)<32);
struct CameraDiagnosticKey {
    CameraDiagnosticMode mode{};
    CameraDiagnosticPhase phase{};
    char const* reason{}; // Only stable diagnostic literals, never formatted values.
};
struct CameraDiagnosticState {
    std::uint64_t generation{};
    std::array<CameraDiagnosticKey,32> records{};
    unsigned lines{}, ticks{}, lookArrivals{}, freeArrivals{}, activationTickPending{};
} cameraDiagnostics;
void refreshCameraDiagnostics() noexcept {
    auto generation=worldGeneration.load();
    if(cameraDiagnostics.generation!=generation)cameraDiagnostics={generation};
}
char const* cameraDiagnosticModeName(CameraDiagnosticMode mode) noexcept {
    return mode==CameraDiagnosticMode::Freelook?"Freelook":mode==CameraDiagnosticMode::FreeCamera?"FreeCamera":"Common";
}
CameraDiagnosticMode cameraDiagnosticMode(DetachedCameraMode mode) noexcept {
    return mode==DetachedCameraMode::FreeCamera?CameraDiagnosticMode::FreeCamera:
        mode==DetachedCameraMode::Freelook?CameraDiagnosticMode::Freelook:CameraDiagnosticMode::Common;
}
unsigned cameraDiagnosticAnchor(CameraDiagnosticMode mode,CameraDiagnosticPhase phase) noexcept {
    if(mode==CameraDiagnosticMode::Common)return 0;
    if(phase==CameraDiagnosticPhase::LookDown||phase==CameraDiagnosticPhase::FreeDown)return 1;
    if(phase==CameraDiagnosticPhase::LookAccepted||phase==CameraDiagnosticPhase::FreeAccepted
        ||phase==CameraDiagnosticPhase::LookRejected||phase==CameraDiagnosticPhase::FreeRejected)return 2;
    if(phase==CameraDiagnosticPhase::FreelookActivationTick||phase==CameraDiagnosticPhase::FreeCameraActivationTick)return 4;
    if(phase==CameraDiagnosticPhase::Rig)return 8;
    if(phase==CameraDiagnosticPhase::BeginRejected||phase==CameraDiagnosticPhase::BeginAngles
        ||phase==CameraDiagnosticPhase::BeginBounds||phase==CameraDiagnosticPhase::BeginSuccess)return 16;
    return 0;
}
bool cameraDiagnosticPending(CameraDiagnosticMode mode,CameraDiagnosticPhase phase,char const* reason) noexcept {
    refreshCameraDiagnostics();
    if(!lookOptions.enabled&&!freeOptions.enabled&&!saved&&!inputLedger.anyOwned()&&!suppressedButtons.load())return false;
    unsigned common=0,lookAnchors=0,freeAnchors=0;
    for(unsigned i=0;i<cameraDiagnostics.lines;++i) {
        auto const& key=cameraDiagnostics.records[i];
        if(key.mode==mode&&key.phase==phase&&std::strcmp(key.reason,reason)==0)return false;
        auto anchor=cameraDiagnosticAnchor(key.mode,key.phase);
        if(key.mode==CameraDiagnosticMode::Common)++common;
        else if(key.mode==CameraDiagnosticMode::Freelook)lookAnchors|=anchor;
        else freeAnchors|=anchor;
    }
    if(cameraDiagnostics.lines>=32||(mode==CameraDiagnosticMode::Common&&common>=4))return false;
    auto anchor=cameraDiagnosticAnchor(mode,phase);
    if(mode==CameraDiagnosticMode::Freelook)lookAnchors|=anchor;
    else if(mode==CameraDiagnosticMode::FreeCamera)freeAnchors|=anchor;
    auto missing=[](unsigned anchors) {
        unsigned result=0;
        for(unsigned bit=1;bit<=16;bit<<=1)if(!(anchors&bit))++result;
        return result;
    };
    // Startup/common traffic cannot spend the five first-observation slots
    // still reserved for either mode. Later changed reasons share the rest.
    return cameraDiagnostics.lines+1+missing(lookAnchors)+missing(freeAnchors)<=32;
}
template<class... Args>
void cameraDiagnostic(CameraDiagnosticMode mode,CameraDiagnosticPhase phase,char const* reason,
                      char const* format,Args... args) noexcept {
    if(!cameraDiagnosticPending(mode,phase,reason))return;
    cameraDiagnostics.records[cameraDiagnostics.lines++]={mode,phase,reason};
    char details[384]{},message[512]{};
    std::snprintf(details,sizeof(details),format,args...);
    std::snprintf(message,sizeof(message),"CameraNativeDiag CLIENT_CAMERA_CANDIDATE mode=%s reason=%s slot=%u/32 %s",
        cameraDiagnosticModeName(mode),reason,cameraDiagnostics.lines,details);
    logGuiInfo(message);
}
void cameraDiagnosticSummary(CameraDiagnosticPhase phase,char const* stage) noexcept {
    refreshCameraDiagnostics();
    cameraDiagnostic(CameraDiagnosticMode::Common,phase,stage,"stage=%s ticks=%u lookArrivals=%u freeArrivals=%u saved=%u",
        stage,cameraDiagnostics.ticks,cameraDiagnostics.lookArrivals,cameraDiagnostics.freeArrivals,unsigned(bool(saved)));
}
void cameraDiagnosticActivationTick(char const* point,bool eligibilityComputed,bool eligible,
                                    char const* reason,char const* desired) noexcept {
    refreshCameraDiagnostics();
    auto pending=cameraDiagnostics.activationTickPending;
    cameraDiagnostics.activationTickPending=0;
    auto emit=[&](unsigned bit,CameraDiagnosticMode mode,CameraDiagnosticPhase phase,
                  CameraCandidateOptions const& options,CameraActivation const& activation) {
        if(!(pending&bit))return;
        bool wanted=activation.wanted();
        auto tupleReason=!eligibilityComputed||!eligible?reason:
            std::strcmp(point,"after_hold_and_desired")!=0?point:
            !options.enabled?"disabled":!wanted?"wanted_cleared":
            std::strcmp(desired,cameraDiagnosticModeName(mode))==0?"desired":"other_mode_desired";
        cameraDiagnostic(mode,phase,tupleReason,
            "stage=activation_tick point=%s eligibilityComputed=%u eligible=%u eligibilityReason=%s wanted=%u enabled=%u desired=%s",
            point,unsigned(eligibilityComputed),unsigned(eligible),reason,
            unsigned(wanted),unsigned(options.enabled),desired);
    };
    emit(1,CameraDiagnosticMode::Freelook,CameraDiagnosticPhase::FreelookActivationTick,lookOptions,lookActivation);
    emit(2,CameraDiagnosticMode::FreeCamera,CameraDiagnosticPhase::FreeCameraActivationTick,freeOptions,freeActivation);
}
bool cameraContextRejected(char const** reason,char const* value) noexcept {
    if(reason)*reason=value;
    return false;
}
bool focus() noexcept {
    DWORD process{}; auto window=GetForegroundWindow();
    return window && GetWindowThreadProcessId(window,&process) && process==GetCurrentProcessId();
}
bool workingGui() noexcept {
    auto backend=guiBackendKind();
    return backend==GuiBackendKind::Standalone || backend==GuiBackendKind::LholoBridge;
}
bool inputContext(char const** reason=nullptr) noexcept {
    if(retiring.load())return cameraContextRejected(reason,"retiring");
    if(!runtimeReady.load())return cameraContextRejected(reason,"runtime_not_ready");
    if(!inputAllowed.load())return cameraContextRejected(reason,"input_not_allowed");
    if(!focus())return cameraContextRejected(reason,"focus");
    if(!workingGui())return cameraContextRejected(reason,"gui_backend");
    if(guiInputCaptured())return cameraContextRejected(reason,"gui_captured");
    if(reason)*reason="ok";
    return true;
}
bool gameplayInputContext(char const** reason=nullptr) noexcept {
    try {
        if(!inputContext(reason))return false;
        auto primary=ll::service::getClientInstance();
        if(!primary)return cameraContextRejected(reason,"primary_missing");
        if(!primary->getLocalPlayer())return cameraContextRejected(reason,"player_missing");
        if(primary->getClientInstanceState()!=ClientInstanceState::Playing)return cameraContextRejected(reason,"not_playing");
        if(!primary->currentInputModeIsMouseAndKeyboard())return cameraContextRejected(reason,"not_mouse_keyboard");
        if(!primary->getMouseGrabbed())return cameraContextRejected(reason,"mouse_not_grabbed");
        auto screen=primary->getTopScreenName();
        if(screen!="hud_screen"&&screen!="in_game_play_screen")return cameraContextRejected(reason,"not_hud");
        return inputContext(reason);
    }catch(...){return cameraContextRejected(reason,"context_exception");}
}
void closeInput() noexcept {
    mouseAdmission=false; pendingDx=pendingDy=pendingWheel=0; flightDirection.invalidate();nativeViewAngles.reset();
    lookActivation.cancel(); freeActivation.cancel();
    previousMovementAt=0;
}
void retireCameraKeyRelease(int key) noexcept {
    inputLedger.released(key);
    if(key==lookOptions.key)lookActivation.release(lookOptions);
    if(key==freeOptions.key)freeActivation.release(freeOptions);
}
bool physicalCameraKeyDown(int key) noexcept {
    if(key<=0||key>255||!inputContext())return false;
    bool down=(GetAsyncKeyState(key)&0x8000)!=0;
    if(!inputContext())return false;
    if(!down)retireCameraKeyRelease(key);
    return down;
}
bool cameraKeyAdmissionAllowed(int key) noexcept {
    return key>0&&key<=255&&!inputLedger.inhibited(key);
}
CameraDirectionContext directionContext() noexcept {
    if(!saved||!motion.pose()||worldGeneration.load()!=saved->generation)return {};
    auto const& pose=*motion.pose();
    return {sessionEpoch,saved->generation,saved->dimension,pose.yaw,pose.pitch};
}
bool same(WeakEntityRef const& a, EntityContext const& b) {
    auto registry=a.mRegistry.lock();
    return registry && &*registry==&b.mRegistry && a.mEntity==b.mEntity;
}
bool same(WeakEntityRef const& a, WeakEntityRef const& b) {
    auto first=a.mRegistry.lock(), second=b.mRegistry.lock();
    return first && second && &*first==&*second && a.mEntity==b.mEntity;
}
// This retains a registry strong reference only inside this callback. No raw
// client/player/renderer/component pointer survives the function call.
bool restoreFreelook();
bool restoreFreeCamera();
bool restore() {
    closeInput();
    if(!saved){motion.reset();return true;}
    return saved->mode==DetachedCameraMode::Freelook?restoreFreelook():restoreFreeCamera();
}
unsigned cameraDiagnosticComponentMask(EntityContext const& context) {
    auto const& ecs=context.getRegistry();auto id=context.mEntity;
    if(!ecs.valid(id))return 0;
    return unsigned(ecs.all_of<MinecraftCamera::ActiveCameraComponent>(id))
        |(unsigned(ecs.all_of<Direct>(id))<<1)|(unsigned(ecs.all_of<Link>(id))<<2)
        |(unsigned(ecs.all_of<MinecraftCamera::CameraOrbitComponent>(id))<<3)
        |(unsigned(ecs.all_of<Offset>(id))<<4)
        |(unsigned(ecs.all_of<MinecraftCamera::CameraUsageComponent>(id))<<5);
}
void cameraDiagnosticRig(unsigned eligible,unsigned scanned,unsigned actorActive,unsigned currentMask,unsigned rawMask,
                         bool targetMatches,bool complete,CameraDiagnosticMode mode) noexcept {
    auto reason=eligible==0?"zero":eligible==1?"unique":"multiple";
    if(!cameraDiagnosticPending(mode,CameraDiagnosticPhase::Rig,reason))return;
    cameraDiagnostic(mode,CameraDiagnosticPhase::Rig,reason,
        "stage=rig source=client_camera_refs targetMatches=%u scanned=%u eligibleObserved=%u complete=%u actorActive=%u currentMask=%u rawMask=%u maskBits=Active1,Direct2,Link4,Orbit8,Offset16,Usage32",
        unsigned(targetMatches),scanned,eligible,unsigned(complete),actorActive,currentMask,rawMask);
}

// Behavioral reference: amatouhake/Lamium (LGPL-3.0-only), commit
// 2ae2465ae15db4e6b185eddbd1a478be3033134b, DetachedCameraRig/CameraSessions.
// This integration keeps Praxis input/lifetime policy; no Lamium file is imported.
// Lamium's render camera route is the local player's ECS Active+Link view.
// Client camera/target weak refs are not a camera-component factory. Keep the
// actor registry provenance and versioned EntityId together in the SDK ref.
std::optional<WeakEntityRef> freelookRig(IClientInstance& client,LocalPlayer& player,bool starting) {
    auto generation=worldGeneration.load();auto dimension=static_cast<int>(player.getDimensionId());
    auto primary=ll::service::getClientInstance();
    if(!primary||static_cast<IClientInstance*>(&*primary)!=&client||primary->getLocalPlayer()!=&player)return {};
    auto actor=player.getEntityContext().getWeakRef();auto pin=actor.mRegistry.lock();
    if(!pin||!pin->mRegistry->valid(actor.mEntity))return {};
    auto context=actor.lock();if(!context)return {};
    auto const& ecs=context->getRegistry();
    std::optional<WeakEntityRef> result;unsigned active=0,eligible=0,foreign=0,mask=0;bool complete=true;
    std::array<EntityId,32> targets{};
    // Finish the bounded view before native adapter/weak-lock calls: never
    // keep an ECS iterator or component borrow across an engine query.
    for(auto id:ecs.view<MinecraftCamera::ActiveCameraComponent>()) {
        if(active==targets.size()){complete=false;break;}targets[active++]=id;
    }
    for(unsigned index=0;complete&&index<active;++index) {
        auto id=targets[index];
        if(!ecs.valid(id)||(!ecs.all_of<Direct>(id)&&!ecs.all_of<MinecraftCamera::CameraOrbitComponent>(id))
            ||(starting&&!ecs.all_of<Link>(id)))continue;
        auto binding=ecs.try_get<CameraClientInstanceComponent>(id);
        // As in Lamium, an adapter is not required by a render camera. An
        // explicit foreign/null adapter still forbids taking that viewport.
        if(binding&&(!binding->mClientInstance.get()||binding->mClientInstance.get()->getLocalPlayer()!=&player)) {
            ++foreign;continue;
        }
        ++eligible;if(result){complete=false;break;}
        auto weak=actor;weak.mEntity=id;
        auto camera=weak.lock();
        if(!camera||camera->mEntity!=id||&camera->getRegistry()!=&ecs){complete=false;break;}
        mask=cameraDiagnosticComponentMask(*camera);result=std::move(weak);
    }
    auto finalPrimary=ll::service::getClientInstance();
    bool stable=worldGeneration.load()==generation&&static_cast<int>(player.getDimensionId())==dimension
        &&same(actor,player.getEntityContext())&&finalPrimary
        &&static_cast<IClientInstance*>(&*finalPrimary)==&client&&finalPrimary->getLocalPlayer()==&player;
    if(starting)cameraDiagnostic(CameraDiagnosticMode::Freelook,CameraDiagnosticPhase::Rig,
        !complete?"incomplete":eligible==1?"unique":eligible==0?"zero":"multiple",
        "stage=rig source=local_player_ecs activeObserved=%u eligibleObserved=%u foreign=%u complete=%u stable=%u selectedMask=%u limit=32",
        active,eligible,foreign,unsigned(complete),unsigned(stable),mask);
    if(!complete||!stable)return {};
    return result;
}

std::optional<WeakEntityRef> primaryRig(IClientInstance& client, LocalPlayer& player, bool starting,
                                       CameraDiagnosticMode diagnosticMode=CameraDiagnosticMode::Common) {
    if(diagnosticMode==CameraDiagnosticMode::Freelook||diagnosticMode==CameraDiagnosticMode::FreeCamera
        ||(!starting&&saved))return freelookRig(client,player,starting);
    // CameraRegistry entries need not identify the currently rendered rig. Use
    // the client's regular weak camera/target references. Never select a camera
    // merely because it is the sole Active entity in a shared actor registry.
    auto primary=ll::service::getClientInstance();
    if(!primary||static_cast<IClientInstance*>(&*primary)!=&client||client.getLocalPlayer()!=&player)return {};
    auto actor=player.getEntityContext().getWeakRef();
    auto actorRegistry=actor.mRegistry.lock();
    if(!actorRegistry||!actorRegistry->mRegistry->valid(actor.mEntity))return {};
    WeakEntityRef rawCamera,rawTarget;
    client.getRawCameraEntities(rawCamera,rawTarget);
    bool targetMatches=same(rawTarget,player.getEntityContext());
    if(!targetMatches) {
        if(starting)cameraDiagnostic(diagnosticMode,CameraDiagnosticPhase::Rig,"camera_target_mismatch","stage=rig source=client_camera_refs camera_target_mismatch");
        return {};
    }
    auto current=client.getCameraEntity();
    std::optional<WeakEntityRef> result;
    unsigned scanned=0,eligible=0,currentMask=0,rawMask=0,actorActive=0;
    auto actorContext=actor.lock();
    if(!actorContext)return {};
    for(auto id:actorContext->getRegistry().view<MinecraftCamera::ActiveCameraComponent>()) {
        (void)id;++actorActive;
    }
    auto visit=[&](WeakEntityRef const& weak) {
        ++scanned;
        auto pin=weak.mRegistry.lock();if(!pin)return true;
        auto context=weak.lock();if(!context)return true;
        auto const& ecs=context->getRegistry();auto id=context->mEntity;
        if (!ecs.valid(id) || !ecs.all_of<MinecraftCamera::ActiveCameraComponent,Direct>(id)
            || ecs.all_of<MinecraftCamera::CameraOrbitComponent>(id)) return true;
        auto binding=ecs.try_get<CameraClientInstanceComponent>(id);
        if(binding&&(!binding->mClientInstance.get()||binding->mClientInstance.get()->getLocalPlayer()!=&player))return true;
        if (starting && !ecs.all_of<Link>(id)) return true;
        if (result && same(*result,*context)) return true;
        ++eligible;
        if (result) return false;
        result=weak;
        return true;
    };
    auto root=[&](WeakEntityRef const& weak,unsigned& mask) {
        auto pin=weak.mRegistry.lock();if(!pin)return true;
        auto context=weak.lock();if(!context)return true;
        mask=cameraDiagnosticComponentMask(*context);
        // CameraUsage exposes a weak EntityId without registry provenance in
        // this SDK. Do not graft that ID onto this root's registry or guess a
        // camera from the actor ECS. Require a complete client weak entity ref.
        return visit(weak);
    };
    if(!root(current,currentMask)||!root(rawCamera,rawMask)) {
        if(starting)cameraDiagnosticRig(eligible,scanned,actorActive,currentMask,rawMask,true,false,diagnosticMode);
        return {};
    }
    WeakEntityRef finalRaw,finalTarget;
    client.getRawCameraEntities(finalRaw,finalTarget);
    auto finalCurrent=client.getCameraEntity();
    // Both roots and the target must remain the same across native queries.
    auto unchanged=[](WeakEntityRef const& first,WeakEntityRef const& second) {
        bool firstLive=bool(first.mRegistry.lock()),secondLive=bool(second.mRegistry.lock());
        return firstLive?secondLive&&same(first,second):!secondLive;
    };
    auto finalPrimary=ll::service::getClientInstance();
    if(!unchanged(current,finalCurrent)||!unchanged(rawCamera,finalRaw)
        ||!same(rawTarget,finalTarget)||!same(actor,player.getEntityContext())
        ||!finalPrimary||static_cast<IClientInstance*>(&*finalPrimary)!=&client
        ||finalPrimary->getLocalPlayer()!=&player)return {};
    if(starting)cameraDiagnosticRig(eligible,scanned,actorActive,currentMask,rawMask,true,true,diagnosticMode);
    return result;
}
bool freelookShapeMatches(auto const& ecs,EntityId id) {
    return saved&&saved->mode==DetachedCameraMode::Freelook&&ecs.valid(id)
        &&ecs.template all_of<Direct>(id)==saved->hasDirect
        &&ecs.template all_of<MinecraftCamera::CameraOrbitComponent>(id)==saved->hasOrbit;
}
bool currentCameraInputContext(char const** reason=nullptr) {
    if(saved&&GetCurrentThreadId()!=cameraThread)return cameraContextRejected(reason,"saved_thread");
    if(!gameplayInputContext(reason))return false;
    if(!saved)return true;
    auto primary=ll::service::getClientInstance();
    auto* player=primary?primary->getLocalPlayer():nullptr;
    if(!player||worldGeneration.load()!=saved->generation
        ||static_cast<int>(player->getDimensionId())!=saved->dimension
        ||!same(saved->actor,player->getEntityContext()))return cameraContextRejected(reason,"saved_actor_context");
    auto rig=primaryRig(*primary,*player,false);
    auto context=saved->rig.lock();
    if(saved->mode==DetachedCameraMode::Freelook&&context
        &&!freelookShapeMatches(context->getRegistry(),context->mEntity)) {
        closeInput();stopRequested=true;
        return cameraContextRejected(reason,"saved_camera_shape_changed");
    }
    if(saved->mode==DetachedCameraMode::FreeCamera&&(!saved->freeOffset||!context
        ||!freeCameraOffsetMatches(context->getRegistry(),context->mEntity,*saved->freeOffset))) {
        closeInput();stopRequested=true;return cameraContextRejected(reason,"free_camera_owner_or_shape_changed");
    }
    if(!rig||!same(*rig,saved->rig)||!context
        ||context->getRegistry().all_of<Link>(context->mEntity))return cameraContextRejected(reason,"saved_rig_context");
    if(!gameplayInputContext(reason))return false;
    if(worldGeneration.load()!=saved->generation)return cameraContextRejected(reason,"generation_changed");
    auto finalPrimary=ll::service::getClientInstance();
    bool result=finalPrimary&&static_cast<IClientInstance*>(&*finalPrimary)==static_cast<IClientInstance*>(&*primary)
        &&finalPrimary->getLocalPlayer()==player
        &&static_cast<int>(player->getDimensionId())==saved->dimension
        &&same(saved->actor,player->getEntityContext());
    if(result&&saved->mode==DetachedCameraMode::Freelook
        &&!freelookShapeMatches(context->getRegistry(),context->mEntity)) {
        closeInput();stopRequested=true;
        return cameraContextRejected(reason,"saved_camera_shape_changed");
    }
    if(result&&saved->mode==DetachedCameraMode::FreeCamera
        &&(!saved->freeOffset||!freeCameraOffsetMatches(context->getRegistry(),context->mEntity,*saved->freeOffset))) {
        closeInput();stopRequested=true;return cameraContextRejected(reason,"free_camera_offset_changed");
    }
    return result?true:cameraContextRejected(reason,"final_primary_context");
}

bool finiteFreelookCamera(auto const& ecs,EntityId id) {
    auto direct=ecs.template try_get<Direct>(id);
    auto orbit=ecs.template try_get<MinecraftCamera::CameraOrbitComponent>(id);
    if(!direct&&!orbit)return false;
    if(direct&&(!std::isfinite(direct->mYaw)||!std::isfinite(direct->mPitch)))return false;
    return !orbit||(std::isfinite(orbit->mCurrentSpherical->mAzimuth)&&std::isfinite(orbit->mCurrentSpherical->mPolarAngle)
        &&std::isfinite(orbit->mIdealSpherical->mAzimuth)&&std::isfinite(orbit->mIdealSpherical->mPolarAngle));
}
bool keepFreelookHead(LocalPlayer& player) {
    if(!saved||saved->mode!=DetachedCameraMode::Freelook||!same(saved->actor,player.getEntityContext()))return false;
    auto context=saved->rig.lock();
    if(!context||!freelookShapeMatches(context->getRegistry(),context->mEntity)) {
        closeInput();stopRequested=true;return false;
    }
    auto head=player.getEntityContext().tryGetComponent<ActorHeadRotationComponent>();
    if(!head||!std::isfinite(head->mYHeadRot)||!std::isfinite(head->mYHeadRotO))return false;
    // Vanilla can turn the visual head independently of the removed Link.
    // Keep both interpolation endpoints at the captured local orientation.
    head->mYHeadRot=saved->headYaw;head->mYHeadRotO=saved->headYaw;return true;
}
bool restoreFreelook() {
    auto pin=saved->rig.mRegistry.lock();
    if(!pin||!pin->mRegistry->valid(saved->rig.mEntity)) {
        saved.reset();motion.reset();status="Camera retired; OFF";return true;
    }
    auto primary=ll::service::getClientInstance();auto player=primary?primary->getLocalPlayer():nullptr;
    if(!player||!same(saved->actor,player->getEntityContext())) {
        status="Restore pending: original camera owner unavailable";return false;
    }
    auto context=saved->rig.lock();if(!context)return false;
    auto& ecs=context->getRegistry();auto id=context->mEntity;
    if(&ecs!=&player->getEntityContext().getRegistry()||ecs.all_of<Link>(id)) {
        status="Restore pending: camera owner conflict";return false;
    }
    auto binding=ecs.try_get<CameraClientInstanceComponent>(id);
    if(binding&&(!binding->mClientInstance.get()||binding->mClientInstance.get()->getLocalPlayer()!=player))return false;
    auto finalPrimary=ll::service::getClientInstance();
    if(!finalPrimary||finalPrimary->getLocalPlayer()!=player||!same(saved->actor,player->getEntityContext())
        ||!ecs.valid(id)||ecs.all_of<Link>(id))return false;
    // Reacquire component borrows after the native owner query. All values are
    // captured/restored in their SDK-native units, before re-enabling the copy.
    auto direct=ecs.try_get<Direct>(id);auto orbit=ecs.try_get<MinecraftCamera::CameraOrbitComponent>(id);
    if(saved->hasDirect!=bool(direct)||saved->hasOrbit!=bool(orbit)) {
        closeInput();stopRequested=true;
        status="Restore pending: camera shape changed";return false;
    }
    if(saved->hasDirect){direct->mYaw=saved->yaw;direct->mPitch=saved->pitch;direct->mYawDelta=0.f;}
    if(saved->hasOrbit) {
        orbit->mCurrentSpherical->mAzimuth=saved->orbitAzimuth;orbit->mCurrentSpherical->mPolarAngle=saved->orbitPolar;
        orbit->mIdealSpherical->mAzimuth=saved->idealAzimuth;orbit->mIdealSpherical->mPolarAngle=saved->idealPolar;
        orbit->mAzimuthVelocity=0.f;orbit->mPolarAngleVelocity=0.f;
    }
    if(auto head=player->getEntityContext().tryGetComponent<ActorHeadRotationComponent>()) {
        head->mYHeadRot=saved->headYaw;head->mYHeadRotO=saved->headPrevious;
    }
    ecs.emplace<Link>(id).mLookMode=saved->lookMode;
    saved.reset();motion.reset();status="OFF; camera restored";
    logGuiInfo("CameraCandidate Freelook restored native camera and Link");return true;
}
bool beginFreelook(IClientInstance& client,LocalPlayer& player) {
    auto generation=worldGeneration.load();auto dimension=static_cast<int>(player.getDimensionId());
    auto actor=player.getEntityContext().getWeakRef();auto weak=freelookRig(client,player,true);
    if(!weak){status="Needs one active local camera rig";return false;}
    // Use the existing SDK/lifecycle query boundary, then select again before
    // mutation. No component pointer survives that native query.
    auto eye=player.getEyePos();
    auto primary=ll::service::getClientInstance();auto finalRig=freelookRig(client,player,true);
    if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z)
        ||!inputContext()||GetCurrentThreadId()!=cameraThread||worldGeneration.load()!=generation
        ||static_cast<int>(player.getDimensionId())!=dimension||!same(actor,player.getEntityContext())
        ||!primary||static_cast<IClientInstance*>(&*primary)!=&client||primary->getLocalPlayer()!=&player
        ||!finalRig||!same(*weak,*finalRig))return false;
    auto pin=weak->mRegistry.lock();auto context=weak->lock();if(!pin||!context)return false;
    auto& ecs=context->getRegistry();auto id=context->mEntity;
    auto link=ecs.try_get<Link>(id);auto head=player.getEntityContext().tryGetComponent<ActorHeadRotationComponent>();
    auto rotation=player.getEntityContext().tryGetComponent<ActorRotationComponent>();
    if(!link||!head||!rotation||!finiteFreelookCamera(ecs,id)
        ||!std::isfinite(head->mYHeadRot)||!std::isfinite(head->mYHeadRotO)
        ||!std::isfinite(rotation->mRot->x)||!std::isfinite(rotation->mRot->y))return false;
    Saved next{};next.rig=*weak;next.actor=actor;next.lookMode=link->mLookMode;
    next.mode=DetachedCameraMode::Freelook;next.generation=generation;next.dimension=dimension;
    next.bodyPitch=rotation->mRot->x;next.bodyYaw=rotation->mRot->y;
    next.headYaw=head->mYHeadRot;next.headPrevious=head->mYHeadRotO;
    if(auto direct=ecs.try_get<Direct>(id)){next.hasDirect=true;next.yaw=direct->mYaw;next.pitch=direct->mPitch;}
    if(auto orbit=ecs.try_get<MinecraftCamera::CameraOrbitComponent>(id)) {
        next.hasOrbit=true;next.orbitAzimuth=orbit->mCurrentSpherical->mAzimuth;next.orbitPolar=orbit->mCurrentSpherical->mPolarAngle;
        next.idealAzimuth=orbit->mIdealSpherical->mAzimuth;next.idealPolar=orbit->mIdealSpherical->mPolarAngle;
    }
    ++sessionEpoch;if(!sessionEpoch)++sessionEpoch;
    motion.reset();flightDirection.invalidate();pendingDx=pendingDy=pendingWheel=0;
    saved=std::move(next);ecs.remove<Link>(id);
    status="Freelook active";logGuiInfo("CameraCandidate Freelook begin; native look input");
    cameraDiagnostic(CameraDiagnosticMode::Freelook,CameraDiagnosticPhase::BeginSuccess,"success",
        "stage=begin success=1 freeMode=0 source=local_player_ecs nativeLook=1 direct=%u orbit=%u",
        unsigned(saved->hasDirect),unsigned(saved->hasOrbit));return true;
}

bool restoreFreeCamera() {
    if(!saved||saved->mode!=DetachedCameraMode::FreeCamera)return false;
    auto pin=saved->rig.mRegistry.lock();
    if(!pin||!pin->mRegistry->valid(saved->rig.mEntity)) {
        // Only registry expiry or versioned entity retirement permits dropping
        // the snapshot. A logical generation change stops input, not ownership.
        saved.reset();motion.reset();status="FreeCamera owner retired; OFF";return true;
    }
    auto restorationGeneration=worldGeneration.load();
    auto primary=ll::service::getClientInstance();auto player=primary?primary->getLocalPlayer():nullptr;
    if(!player||static_cast<int>(player->getDimensionId())!=saved->dimension
        ||!same(saved->actor,player->getEntityContext())) {
        status="FreeCamera restoration waiting for original owner";return false;
    }
    auto rig=freelookRig(*primary,*player,false);
    auto finalPrimary=ll::service::getClientInstance();
    if(!rig||!same(*rig,saved->rig)||!finalPrimary
        ||static_cast<IClientInstance*>(&*finalPrimary)!=static_cast<IClientInstance*>(&*primary)
        ||finalPrimary->getLocalPlayer()!=player||!same(saved->actor,player->getEntityContext())
        ||worldGeneration.load()!=restorationGeneration
        ||static_cast<int>(player->getDimensionId())!=saved->dimension)return false;
    auto context=saved->rig.lock();if(!context)return false;
    auto& ecs=context->getRegistry();auto id=context->mEntity;
    if(!saved->freeOffset||!restoreFreeCameraOffset(ecs,id,*saved->freeOffset)) {
        status="FreeCamera stopped: restoration pending after owner/shape change";return false;
    }
    // Removing an added Offset can invalidate storage borrows; reacquire now.
    auto direct=ecs.try_get<Direct>(id);
    if(!direct||ecs.all_of<Link>(id))return false;
    direct->mYaw=saved->yaw;direct->mPitch=saved->pitch;direct->mYawDelta=0.f;
    if(auto head=player->getEntityContext().tryGetComponent<ActorHeadRotationComponent>()) {
        head->mYHeadRot=saved->headYaw;head->mYHeadRotO=saved->headPrevious;
    }
    ecs.emplace<Link>(id).mLookMode=saved->lookMode;
    saved.reset();motion.reset();status="OFF; FreeCamera restored";
    logGuiInfo("CameraCandidate FreeCamera restored native camera, Offset and Link");return true;
}
bool beginFreeCamera(IClientInstance& client,LocalPlayer& player) {
    if(!freeCameraMovementReady.load()){status="FreeCamera input backend unavailable";return false;}
    auto generation=worldGeneration.load();auto dimension=static_cast<int>(player.getDimensionId());
    auto actor=player.getEntityContext().getWeakRef();auto weak=freelookRig(client,player,true);
    auto eye=player.getEyePos();
    auto primary=ll::service::getClientInstance();auto finalRig=freelookRig(client,player,true);
    if(!weak||!finalRig||!same(*weak,*finalRig)||!primary
        ||static_cast<IClientInstance*>(&*primary)!=&client||primary->getLocalPlayer()!=&player
        ||!same(actor,player.getEntityContext())||worldGeneration.load()!=generation
        ||static_cast<int>(player.getDimensionId())!=dimension||GetCurrentThreadId()!=cameraThread
        ||!inputContext()||!finiteFreeCameraOffset(glm::vec3{eye.x,eye.y,eye.z}))return false;
    auto pin=weak->mRegistry.lock();auto context=weak->lock();if(!pin||!context)return false;
    auto& ecs=context->getRegistry();auto id=context->mEntity;
    auto direct=ecs.try_get<Direct>(id);auto link=ecs.try_get<Link>(id);
    auto head=player.getEntityContext().tryGetComponent<ActorHeadRotationComponent>();
    auto rotation=player.getEntityContext().tryGetComponent<ActorRotationComponent>();
    if(!freeCameraDirectShape(ecs,id)||!direct||!link||!head||!rotation
        ||!std::isfinite(head->mYHeadRot)||!std::isfinite(head->mYHeadRotO)
        ||!std::isfinite(rotation->mRot->x)||!std::isfinite(rotation->mRot->y)
        ||!std::isfinite(direct->mYaw)||!std::isfinite(direct->mPitch)) {
        status="FreeCamera requires one active first-person Direct rig";return false;
    }
    Saved next{};next.rig=*weak;next.actor=actor;next.lookMode=link->mLookMode;
    next.mode=DetachedCameraMode::FreeCamera;next.generation=generation;next.dimension=dimension;
    next.hasDirect=true;next.yaw=direct->mYaw;next.pitch=direct->mPitch;
    next.bodyPitch=rotation->mRot->x;next.bodyYaw=rotation->mRot->y;
    next.headYaw=head->mYHeadRot;next.headPrevious=head->mYHeadRotO;
    next.anchorEye={eye.x,eye.y,eye.z};
    // Private motion stores displacement only. Native camera angle units and
    // constraints remain entirely with vanilla; never convert these SDK values.
    if(!motion.begin(0,0,-1,1))return false;
    next.freeOffset=takeFreeCameraOffset(ecs,id);
    if(!next.freeOffset){motion.reset();status="FreeCamera Offset admission rejected";return false;}
    // No engine query or frame-spanning component borrow follows acquisition.
    next.offset=next.freeOffset->original;
    ++sessionEpoch;if(!sessionEpoch)++sessionEpoch;
    flightDirection.invalidate();pendingDx=pendingDy=pendingWheel=0;previousMovementAt=0;
    saved=std::move(next);ecs.remove<Link>(id);
    status="FreeCamera active; WASD/Space/Shift flies";
    logGuiInfo("CameraCandidate FreeCamera begin: local Direct rig / SDK Offset / native look");
    cameraDiagnostic(CameraDiagnosticMode::FreeCamera,CameraDiagnosticPhase::BeginSuccess,"success",
        "stage=begin success=1 freeMode=1 source=local_player_ecs offsetAdded=%u",unsigned(saved->freeOffset->added));
    return true;
}
bool begin(IClientInstance& client,LocalPlayer& player,DetachedCameraMode mode) {
    return mode==DetachedCameraMode::Freelook?beginFreelook(client,player):beginFreeCamera(client,player);
}
bool keepFreeCameraHead(LocalPlayer& player) {
    auto head=player.getEntityContext().tryGetComponent<ActorHeadRotationComponent>();
    if(!head||!std::isfinite(head->mYHeadRot)||!std::isfinite(head->mYHeadRotO))return false;
    head->mYHeadRot=saved->headYaw;head->mYHeadRotO=saved->headYaw;return true;
}
bool observeFreeCameraLook(Direct const& direct) {
    std::array<float,2> values{direct.mYaw,direct.mPitch};
    if(!std::isfinite(values[0])||!std::isfinite(values[1]))return false;
    if(!nativeViewAngles||*nativeViewAngles!=values) {
        nativeViewAngles=values;++sessionEpoch;if(!sessionEpoch)++sessionEpoch;
        flightDirection.invalidate();
    }
    return true;
}
bool updateFreeCameraMotion(LocalPlayer& player) {
    if(!currentCameraInputContext()||!keepFreeCameraHead(player)||!motion.pose())return false;
    auto context=saved->rig.lock();if(!context)return false;
    auto direct=context->getRegistry().try_get<Direct>(context->mEntity);
    return direct&&observeFreeCameraLook(*direct);
}
bool advanceFreeCameraMotion(std::array<float,3> forward,std::uint64_t now) {
    auto elapsed=previousMovementAt&&now>=previousMovementAt?now-previousMovementAt:0;
    previousMovementAt=now;
    auto axes=inputLedger.movement();
    if((axes[0]||axes[1]||axes[2])&&elapsed) {
        if(!motion.moveTimed(axes[0],axes[1],axes[2],freeOptions.flightSpeed,
            std::min(elapsed,UINT64_C(100))*.001f,forward))return false;
    }
    if(pendingWheel&&!motion.move(pendingWheel,freeOptions.wheelDistance,forward))return false;
    pendingDx=pendingDy=pendingWheel=0;return true;
}
bool bindAllowed(int key,unsigned modifiers) {
    if(!key)return true;
    if(key<8||key==VK_ESCAPE||key==VK_F5||key==VK_F10||key==VK_F11||cameraMovementKey(key)
        ||(key>=VK_LSHIFT&&key<=VK_RMENU)||key==VK_CONTROL||key==VK_MENU||key==VK_LWIN||key==VK_RWIN)return false;
    auto current=settings();HotkeyChord chord{unsigned(key),cameraHotkeyModifiers(modifiers),1};
    if(hotkeyConflict(chord,{unsigned(current->zoomActionKey),current->zoomActionModifiers,1})
        ||hotkeyConflict(chord,{unsigned(current->mapToggleActionKey),current->mapToggleActionModifiers,1}))return false;
    for(unsigned i=0;i<featureCount;++i)if((ownedFeatureSlotMask&featureRegistryBit(i))
        &&i!=20&&i!=21&&hotkeyConflict(chord,{unsigned(current->keys[i]),current->keyModifiers[i],1}))return false;
    for(auto key:mapActionBindings())if(hotkeyConflict(chord,{unsigned(key),anyModifiers,1}))return false;
    return true;
}
void syncCameraOptions() {
    auto current=settings();
    auto make=[&](Feature feature,CameraOptions const& options){
        CameraCandidateOptions result;
        auto id=static_cast<unsigned>(feature);
        result.enabled=current->on(feature);result.key=current->keys[id];result.modifiers=current->keyModifiers[id];
        result.hold=feature==Feature::FreeCamera?false:options.hold;result.degreesPerPixel=options.degreesPerPixel;
        result.wheelDistance=options.wheelDistance;result.flightSpeed=options.flightSpeed;return result;
    };
    auto look=make(Feature::Freelook,current->freelookOptions),free=make(Feature::FreeCamera,current->freeCameraOptions);
    if(look==lookOptions&&free==freeOptions)return;
    closeInput();stopRequested=true;
    lookOptions=look;freeOptions=free;
    auto seed=[](CameraCandidateOptions const& options,CameraActivation& activation){
        if(options.key<=0)return;
        if(!inputContext()){inputLedger.inhibit(options.key);activation.update(options,false,inputLedger.down(options.key));return;}
        bool down=physicalCameraKeyDown(options.key);
        if(down){(void)inputLedger.observe(options.key,true,false);inputLedger.inhibit(options.key);}
        activation.update(options,false,down);
    };
    seed(lookOptions,lookActivation);seed(freeOptions,freeActivation);
}
bool retireReleasedCameraInput() noexcept {
    // Stop/cleanup may set retiring, but the gameplay focus/GUI owner must still
    // be known both before and after each physical read. No poll ever activates.
    auto trusted=[] {return runtimeReady.load()&&inputAllowed.load()&&focus()&&workingGui()&&!guiInputCaptured();};
    if(!trusted())return !inputLedger.anyOwned();
    for(int key=1;key<256;++key)if(inputLedger.owned(key)||inputLedger.inhibited(key)){
        bool released=(GetAsyncKeyState(key)&0x8000)==0;
        if(!trusted())return false;
        if(released)retireCameraKeyRelease(key);
    }
    return !inputLedger.anyOwned();
}
bool movementNeutral() noexcept {
    if(!inputContext())return false;
    for(auto key:cameraMovementKeys){
        bool down=(GetAsyncKeyState(key)&0x8000)!=0;
        if(!inputContext())return false;
        if(down){
            // A known cancelled/owned Down never reached vanilla. Unknown
            // pre-held input remains a possible body Down until a formal Up.
            if(!inputLedger.owned(key)&&!inputLedger.inhibited(key)
                &&(!cameraShiftKey(key)||!inputLedger.knownShift()))inputLedger.suspectBodyDown(key);
            return false;
        }
        inputLedger.released(key);
    }
    return inputLedger.neutral();
}

} // namespace
void cameraCandidateStart() noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        if(saved||inputLedger.anyOwned()||suppressedButtons.load())return;
        retiring=false;stopRequested=false;mouseAdmission=false;cameraThread=0;previousMovementAt=0;
        lookActivation.cancel();freeActivation.cancel();status="OFF";
        syncCameraOptions();
        cameraDiagnostic(CameraDiagnosticMode::Common,CameraDiagnosticPhase::Start,"start","stage=start diagnostics_only=1 maxLinesPerWorld=32");
    }catch(...){retiring=true;mouseAdmission=false;stopRequested=true;}
}
void cameraCandidateTick(LocalPlayer& player) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        syncCameraOptions();
        (void)retireReleasedCameraInput();
        refreshCameraDiagnostics();
        if(cameraDiagnostics.ticks<65535)++cameraDiagnostics.ticks;
        auto primary=ll::service::getClientInstance();
        bool primaryMatches=primary&&primary->getLocalPlayer()==&player;
        cameraDiagnostic(CameraDiagnosticMode::Common,CameraDiagnosticPhase::TickArrival,"arrival",
            "stage=tick_arrival primaryMatches=%u ready=%u inputAllowed=%u lookEnabled=%u freeEnabled=%u lookBound=%u freeBound=%u lookArrivals=%u freeArrivals=%u",
            unsigned(primaryMatches),unsigned(runtimeReady.load()),unsigned(inputAllowed.load()),unsigned(lookOptions.enabled),unsigned(freeOptions.enabled),
            unsigned(lookOptions.key>0),unsigned(freeOptions.key>0),cameraDiagnostics.lookArrivals,cameraDiagnostics.freeArrivals);
        if(!primaryMatches) {
            cameraDiagnostic(CameraDiagnosticMode::Common,CameraDiagnosticPhase::PrimaryMismatch,"primary_player_mismatch","stage=tick rejected=primary_player_mismatch");
            closeInput();stopRequested=true;
            cameraDiagnosticActivationTick("primary_rejected",false,false,"primary_player_mismatch","not_computed");
            return;
        }
        auto tid=GetCurrentThreadId(); if(!cameraThread) cameraThread=tid;
        if(tid!=cameraThread) {
            cameraDiagnostic(CameraDiagnosticMode::Common,CameraDiagnosticPhase::ThreadMismatch,"thread_changed","stage=tick rejected=thread_changed");
            closeInput();stopRequested=true;status="Camera thread changed";
            cameraDiagnosticActivationTick("thread_rejected",false,false,"thread_changed","not_computed");
            return;
        }
        auto& client=static_cast<IClientInstance&>(*primary);
        auto screen=client.getTopScreenName();
        char const* eligibilityReason="ok";
        bool eligible=inputContext(&eligibilityReason);
        if(eligible&&client.getClientInstanceState()!=ClientInstanceState::Playing){eligible=false;eligibilityReason="not_playing";}
        if(eligible&&!client.currentInputModeIsMouseAndKeyboard()){eligible=false;eligibilityReason="not_mouse_keyboard";}
        if(eligible&&!client.getMouseGrabbed()){eligible=false;eligibilityReason="mouse_not_grabbed";}
        if(eligible&&screen!="hud_screen"&&screen!="in_game_play_screen"){eligible=false;eligibilityReason="not_hud";}
        if(eligible&&!player.isAlive()){eligible=false;eligibilityReason="actor_admission";}
        if(eligible&&player.isSleeping()){eligible=false;eligibilityReason="actor_admission";}
        if(eligible&&player.getVehicle()){eligible=false;eligibilityReason="actor_admission";}
        if(eligible&&player.getStatusFlag(ActorFlags::Usingitem)){eligible=false;eligibilityReason="actor_admission";}
        if(eligible&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0){eligible=false;eligibilityReason="mouse_button_admission";}
        if(eligible&&(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0){eligible=false;eligibilityReason="mouse_button_admission";}
        cameraDiagnostic(CameraDiagnosticMode::Common,eligible?CameraDiagnosticPhase::TickEligible:CameraDiagnosticPhase::TickIneligible,eligibilityReason,
            "stage=tick_eligibility eligible=%u reason=%s lookWanted=%u freeWanted=%u lookArrivals=%u freeArrivals=%u",
            unsigned(eligible),eligibilityReason,unsigned(lookActivation.wanted()),unsigned(freeActivation.wanted()),cameraDiagnostics.lookArrivals,cameraDiagnostics.freeArrivals);
        if(stopRequested.exchange(false) || !eligible || (saved && saved->generation!=worldGeneration.load())) {
            if(!restore()) {
                cameraDiagnosticActivationTick("restore_incomplete",true,eligible,eligibilityReason,"not_computed");
                return;
            }
        }
        if(retiring.load()) {
            cameraDiagnosticActivationTick("retiring",true,eligible,eligibilityReason,"not_computed");
            return;
        }
        // Only formal accepted KeyEvents activate. Polls can retire release,
        // never manufacture a down/edge after focus, profile or external cancel.
        if(lookOptions.hold&&lookActivation.wanted()&&!physicalCameraKeyDown(lookOptions.key))lookActivation.cancel();
        if(freeOptions.hold&&freeActivation.wanted()&&!physicalCameraKeyDown(freeOptions.key))freeActivation.cancel();
        bool lookWant=eligible&&lookOptions.enabled&&lookActivation.wanted();
        bool freeWant=eligible&&freeOptions.enabled&&freeActivation.wanted();
        auto desired=freeWant ? DetachedCameraMode::FreeCamera : lookWant ? DetachedCameraMode::Freelook : DetachedCameraMode::None;
        cameraDiagnosticActivationTick("after_hold_and_desired",true,eligible,eligibilityReason,
            desired==DetachedCameraMode::FreeCamera?"FreeCamera":desired==DetachedCameraMode::Freelook?"Freelook":"None");
        if(desired!=DetachedCameraMode::None)cameraDiagnostic(cameraDiagnosticMode(desired),CameraDiagnosticPhase::TickDesired,"desired",
            "stage=tick_desired look=%u free=%u alreadySaved=%u",unsigned(lookWant),unsigned(freeWant),unsigned(bool(saved)));
        if(saved && saved->mode!=desired) { restore(); return; }
        if(!saved && desired!=DetachedCameraMode::None) {
            if(desired==DetachedCameraMode::FreeCamera&&!movementNeutral()){
                cameraDiagnostic(CameraDiagnosticMode::FreeCamera,CameraDiagnosticPhase::MovementNeutral,"movement_not_neutral","stage=begin rejected=movement_not_neutral");
                status="Release movement keys before FreeCamera starts";return;
            }
            if((GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000)
                || !begin(client,player,desired)) { closeInput(); return; }
        }
        if(!saved) return;
        if(saved->mode==DetachedCameraMode::Freelook) {
            if(!currentCameraInputContext()||!keepFreelookHead(player)) {restore();return;}
            auto context=saved->rig.lock();
            if(!context||!finiteFreelookCamera(context->getRegistry(),context->mEntity)){restore();return;}
            // Vanilla owns input conversion, camera constraints and rendering.
            // Never overwrite its current camera angle with private motion.
            pendingDx=pendingDy=pendingWheel=0;mouseAdmission=true;return;
        }
        if(!updateFreeCameraMotion(player)){restore();return;}
        mouseAdmission=true;
    } catch(...) {
        mouseAdmission=false;stopRequested=true;
        // The lock has unwound; retain the existing error log without touching
        // diagnostic counters outside cameraMutex.
        logGuiError("CameraCandidate callback failed; restoration requested");
    }
}
void cameraCandidateBeforeTick(LocalPlayer& player) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        syncCameraOptions();
        (void)retireReleasedCameraInput();
        if(!saved)return;
        if(GetCurrentThreadId()!=cameraThread){closeInput();stopRequested=true;return;}
        auto primary=ll::service::getClientInstance();
        if(!primary || primary->getLocalPlayer()!=&player){closeInput();stopRequested=true;return;}
        auto const& options=saved->mode==DetachedCameraMode::FreeCamera ? freeOptions : lookOptions;
        bool released=options.hold && !physicalCameraKeyDown(options.key);
        auto screen=primary->getTopScreenName();
        if(stopRequested.load() || !inputContext() || released || !player.isAlive()
            ||worldGeneration.load()!=saved->generation||player.isSleeping()||player.getVehicle()
            ||player.getStatusFlag(ActorFlags::Usingitem)
            ||(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000)
            ||primary->getClientInstanceState()!=ClientInstanceState::Playing
            ||!primary->currentInputModeIsMouseAndKeyboard()||!primary->getMouseGrabbed()
            || saved->dimension!=static_cast<int>(player.getDimensionId())
            || (screen!="hud_screen"&&screen!="in_game_play_screen")) restore();
        else if(saved&&saved->mode==DetachedCameraMode::Freelook
            &&(!currentCameraInputContext()||!keepFreelookHead(player)))restore();
        else if(saved&&saved->mode==DetachedCameraMode::FreeCamera
            &&(!currentCameraInputContext()||!keepFreeCameraHead(player)))restore();
    } catch(...) {mouseAdmission=false;stopRequested=true;}
}
void cameraCandidateBeforeRender(LevelRendererPlayer& renderer) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        if(!saved||saved->mode!=DetachedCameraMode::FreeCamera)return;
        auto primary=ll::service::getClientInstance();auto player=primary?primary->getLocalPlayer():nullptr;
        if(primary&&static_cast<IClientInstance*>(&*primary)!=&renderer.mClientInstance)return;
        if(GetCurrentThreadId()!=cameraThread||!currentCameraInputContext()) {
            closeInput();stopRequested=true;return;
        }
        if(!player||static_cast<IClientInstance*>(&*primary)!=&renderer.mClientInstance||!motion.pose()) {
            closeInput();stopRequested=true;return;
        }
        auto eye=player->getEyePos();
        auto const& pose=*motion.pose();
        glm::vec3 delta=saved->anchorEye-glm::vec3{eye.x,eye.y,eye.z}
            +glm::vec3{pose.displacement[0],pose.displacement[1],pose.displacement[2]};
        if(!finiteFreeCameraOffset(delta)
            ||!currentCameraInputContext()){closeInput();stopRequested=true;return;}
        auto context=saved->rig.lock();
        if(!context||!saved->freeOffset||!writeFreeCameraOffset(context->getRegistry(),context->mEntity,
            *saved->freeOffset,saved->freeOffset->original+delta)) {
            closeInput();stopRequested=true;
        }
    }catch(...){mouseAdmission=false;stopRequested=true;}
}
void cameraCandidateRender(LevelRendererPlayer& renderer) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        if(!saved||saved->mode!=DetachedCameraMode::FreeCamera){flightDirection.invalidate();return;}
        auto primary=ll::service::getClientInstance();auto player=primary?primary->getLocalPlayer():nullptr;
        if(primary&&static_cast<IClientInstance*>(&*primary)!=&renderer.mClientInstance)return;
        if(GetCurrentThreadId()!=cameraThread||!currentCameraInputContext()) {
            closeInput();stopRequested=true;return;
        }
        if(!player||static_cast<IClientInstance*>(&*primary)!=&renderer.mClientInstance) {
            closeInput();stopRequested=true;return;
        }
        auto context=saved->rig.lock();auto direct=context?context->getRegistry().try_get<Direct>(context->mEntity):nullptr;
        if(!direct||!observeFreeCameraLook(*direct)){closeInput();stopRequested=true;return;}
        auto const& value=renderer.mCameraForward.get();
        std::array<float,3> copied{value.x,value.y,value.z};
        if(!currentCameraInputContext()){closeInput();stopRequested=true;return;}
        auto now=GetTickCount64();std::array<float,3> forward;
        // Stage displacement from this fresh native render basis. The next
        // before-render callback applies it through SDK Offset. Repeated render
        // callbacks account only elapsed time, never duplicate a tick interval.
        if(!flightDirection.capture(directionContext(),copied,now)
            ||!flightDirection.read(directionContext(),now,forward)
            ||!advanceFreeCameraMotion(forward,now)) {
            closeInput();stopRequested=true;
        }
    }catch(...){mouseAdmission=false;stopRequested=true;}
}
void setFreeCameraMovementBackendAvailable(bool available) noexcept {
    freeCameraMovementReady=available;
    if(!available){mouseAdmission=false;stopRequested=true;}
}
void cameraCandidateConsumeMovement(MoveInputComponent const& input,RawMoveInputComponent& raw) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        if(!saved||saved->mode!=DetachedCameraMode::FreeCamera||!freeCameraMovementReady.load())return;
        if(GetCurrentThreadId()!=cameraThread||!currentCameraInputContext()) {
            closeInput();stopRequested=true;return;
        }
        auto primary=ll::service::getClientInstance();
        if(!primary||ClientMoveInputHandler::getMoveInput(*primary)!=&input)return;
        if(!currentCameraInputContext()){closeInput();stopRequested=true;return;}
        withholdFreeCameraMovement(raw);
    }catch(...){mouseAdmission=false;stopRequested=true;}
}
void cameraCandidateWillChangeDimension(LevelRendererPlayer& renderer) noexcept {
    try {
        std::lock_guard lock(cameraMutex);
        if(!saved||saved->mode!=DetachedCameraMode::FreeCamera)return;
        auto primary=ll::service::getClientInstance();
        if(!primary||static_cast<IClientInstance*>(&*primary)!=&renderer.mClientInstance)return;
        closeInput();stopRequested=true;
        if(GetCurrentThreadId()==cameraThread)restore();
    }catch(...){mouseAdmission=false;stopRequested=true;}
}
void adjustFreelookMouse(short& dx,short& dy,bool cancelled) noexcept {
    if(cancelled||!mouseAdmission.load())return;
    try {
        std::lock_guard lock(cameraMutex);
        if(!saved||!currentCameraInputContext())return;
        // Preserve the existing sensitivity control: default .15 is native 1x.
        // SDK/vanilla still owns the pixels-to-camera-angle conversion.
        auto scale=(saved->mode==DetachedCameraMode::FreeCamera?freeOptions:lookOptions).degreesPerPixel/.15f;
        dx=static_cast<short>(std::clamp(std::lround(dx*scale),-4096l,4096l));
        dy=static_cast<short>(std::clamp(std::lround(dy*scale),-4096l,4096l));
    } catch(...) {mouseAdmission=false;stopRequested=true;}
}
bool cameraCandidateMouse(int action,int data,short dx,short dy,bool cancelled) noexcept {
    if(action>=MouseAction::ActionLeft&&action<=MouseAction::ActionX2&&action!=MouseAction::ActionWheel) {
        auto bit=1u<<action;
        if(data==MouseAction::DataDown)suppressedButtons.fetch_and(~bit);
        if(data==MouseAction::DataUp && (suppressedButtons.fetch_and(~bit)&bit)) return !cancelled;
    }
    if(!mouseAdmission.load()) return false;
    try {std::lock_guard lock(cameraMutex);
    if(!saved || !mouseAdmission.load()) return false;
    if(!currentCameraInputContext()) {closeInput();stopRequested=true;return false;}
    if(cancelled) {
        if(action==MouseAction::ActionWheel&&zoomHeld.load()){pendingWheel=0;return false;}
        closeInput();stopRequested=true;return false;
    }
    // With Link detached, vanilla's native look turns this camera only.
    if(action==MouseAction::ActionMoveRelative)return false;
    if(action==MouseAction::ActionWheel && saved->mode==DetachedCameraMode::FreeCamera) {
        if(zoomHeld.load()) return false;
        // Apply queued wheel flight at the next fresh native render basis.
        pendingWheel=std::clamp(pendingWheel+(data>0?1:data<0?-1:0),-32,32);return true;
    }
    if(action>=MouseAction::ActionLeft&&action<=MouseAction::ActionX2&&action!=MouseAction::ActionWheel
        && data==MouseAction::DataDown) {
            suppressedButtons.fetch_or(1u<<action);
            closeInput();stopRequested=true;return true;
        }
    return false;
    }catch(...){mouseAdmission=false;stopRequested=true;return false;}
}
bool cameraCandidateKey(int key,bool down,bool cancelled) noexcept {
    if(key<0||key>255)return false;
    bool previouslyOwned=false,trackedKey=false;
    unsigned diagnosticKeyModes=0;
    try {
        std::lock_guard lock(cameraMutex);
        syncCameraOptions();
        auto event=inputLedger.observe(key,down,cancelled);
        previouslyOwned=event.wasOwned||event.ownedRelease;
        bool lookKey=lookOptions.key>0&&key==lookOptions.key;
        bool freeKey=freeOptions.key>0&&key==freeOptions.key;
        trackedKey=lookKey||freeKey;
        diagnosticKeyModes=(lookKey?1u:0u)|(freeKey?2u:0u);
        if(trackedKey) {
            refreshCameraDiagnostics();
            if(lookKey){
                if(cameraDiagnostics.lookArrivals<32)++cameraDiagnostics.lookArrivals;
                cameraDiagnostic(CameraDiagnosticMode::Freelook,down?CameraDiagnosticPhase::LookDown:CameraDiagnosticPhase::LookUp,
                    cancelled?"externally_cancelled":down?"down":"up",
                    "stage=key_arrival mode=Freelook down=%u fresh=%u cancelled=%u previouslyOwned=%u",
                    unsigned(down),unsigned(event.freshDown),unsigned(cancelled),unsigned(previouslyOwned));
            }
            if(freeKey){
                if(cameraDiagnostics.freeArrivals<32)++cameraDiagnostics.freeArrivals;
                cameraDiagnostic(CameraDiagnosticMode::FreeCamera,down?CameraDiagnosticPhase::FreeDown:CameraDiagnosticPhase::FreeUp,
                    cancelled?"externally_cancelled":down?"down":"up",
                    "stage=key_arrival mode=FreeCamera down=%u fresh=%u cancelled=%u previouslyOwned=%u",
                    unsigned(down),unsigned(event.freshDown),unsigned(cancelled),unsigned(previouslyOwned));
            }
        }
        auto activation=[&](CameraCandidateOptions const& options,CameraActivation& state){
            if(key!=options.key||!options.key)return false;
            if(!down){
                state.release(options);
                if(cancelled||!currentCameraInputContext())state.cancel();
                return false;
            }
            char const* reason="ok";
            bool allowed=currentCameraInputContext(&reason);
            if(allowed&&cancelled){allowed=false;reason="externally_cancelled";}
            if(allowed&&inputLedger.inhibited(key)){allowed=false;reason="inhibited";}
            if(allowed&&!bindAllowed(key,options.modifiers)){allowed=false;reason="binding_conflict";}
            if(event.freshDown) {
                unsigned modifiers{};
                if(cameraHotkeyModifiers(options.modifiers)!=anyModifiers)
                    modifiers=modifierBits((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,
                        (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,(GetAsyncKeyState(VK_MENU)&0x8000)!=0);
                if(allowed)allowed=currentCameraInputContext(&reason);
                if(allowed&&!cameraModifiersMatch(options.modifiers,modifiers)){allowed=false;reason="modifiers";}
            }
            state.update(options,allowed,down);
            bool lookMode=&state==&lookActivation;
            if(event.freshDown&&allowed&&options.enabled&&state.wanted())cameraDiagnostics.activationTickPending|=lookMode?1u:2u;
            auto phase=allowed&&options.enabled
                ?(lookMode?CameraDiagnosticPhase::LookAccepted:CameraDiagnosticPhase::FreeAccepted)
                :(lookMode?CameraDiagnosticPhase::LookRejected:CameraDiagnosticPhase::FreeRejected);
            auto admissionReason=!allowed?reason:!options.enabled?"disabled":
                !state.wanted()?"accepted_not_wanted":event.freshDown?"fresh_accepted_wanted":"accepted_wanted";
            cameraDiagnostic(lookMode?CameraDiagnosticMode::Freelook:CameraDiagnosticMode::FreeCamera,phase,admissionReason,
                "stage=key_admission mode=%s allowed=%u enabled=%u wanted=%u fresh=%u reason=%s",
                lookMode?"Freelook":"FreeCamera",unsigned(allowed),unsigned(options.enabled),unsigned(state.wanted()),unsigned(event.freshDown),
                allowed&&!options.enabled?"disabled":reason);
            return allowed&&options.enabled;
        };
        bool look=activation(lookOptions,lookActivation),free=activation(freeOptions,freeActivation);
        if(free&&freeActivation.wanted())lookActivation.cancel();
        if(look&&lookActivation.wanted())freeActivation.cancel();
        if(cancelled){if(saved){closeInput();stopRequested=true;}return false;}
        if(event.ownedRelease)return true; // matching owned Up never reaches the body
        if(!down)return false;
        if(event.wasOwned)return true; // repeat is not a fresh acquisition
        if(!currentCameraInputContext()||inputLedger.inhibited(key)){
            if(saved){closeInput();stopRequested=true;}
            inputLedger.passDown(key);return false;
        }
        if(key==VK_F5&&saved){closeInput();stopRequested=true;inputLedger.own(key);return true;}
        if((look||free)&&event.freshDown){inputLedger.own(key);return true;}
        if(saved&&saved->mode==DetachedCameraMode::FreeCamera&&!stopRequested.load()&&cameraMovementKey(key)){
            // Adopt only a fresh Down after a neutral admission. Never erase
            // already-held vanilla movement or synthesize a release/feed event.
            if(event.freshDown){inputLedger.own(key);return true;}
        }
        inputLedger.passDown(key);return false;
    }catch(...){
        mouseAdmission=false;stopRequested=true;
        if(trackedKey)try {
            std::lock_guard lock(cameraMutex);
            if(diagnosticKeyModes&1)cameraDiagnostic(CameraDiagnosticMode::Freelook,CameraDiagnosticPhase::KeyException,"camera_activation_exception","stage=key_admission camera_activation_exception");
            if(diagnosticKeyModes&2)cameraDiagnostic(CameraDiagnosticMode::FreeCamera,CameraDiagnosticPhase::KeyException,"camera_activation_exception","stage=key_admission camera_activation_exception");
        }catch(...){}
        return previouslyOwned&&!cancelled;
    }
}
void cameraCandidateExit(IClientInstance& client) noexcept {
    mouseAdmission=false;stopRequested=true;
    try {
        std::lock_guard lock(cameraMutex);
        cameraDiagnosticSummary(CameraDiagnosticPhase::Exit,"exit");
        auto primary=ll::service::getClientInstance();
        if(primary && static_cast<IClientInstance*>(&*primary)==&client && GetCurrentThreadId()==cameraThread) restore();
    } catch(...) {}
}
bool cameraCandidateCleanup() noexcept {
    retiring=true;mouseAdmission=false;stopRequested=true;
    try {
        std::lock_guard lock(cameraMutex);
        cameraDiagnosticSummary(CameraDiagnosticPhase::Cleanup,"cleanup");
        (void)retireReleasedCameraInput();
        if(saved){
            if(GetCurrentThreadId()==cameraThread){if(!restore())return false;}
            else {
                auto registry=saved->rig.mRegistry.lock();
                if(!registry){saved.reset();motion.reset();}
                else {status="Unload waiting for camera restoration";return false;}
            }
        }
        if(inputLedger.anyOwned()||suppressedButtons.load()){
            status="Unload waiting for owned input releases";return false;
        }
        return true;
    }catch(...){return false;}
}
void requestCameraCandidateReset() noexcept {mouseAdmission=false;stopRequested=true;}
char const* cameraCandidateStatus() noexcept {return status.load();}

}
