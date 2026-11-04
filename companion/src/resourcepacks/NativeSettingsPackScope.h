#pragma once
#include "resourcepacks/WorldPackTransaction.h"
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace praxis::companion::packs {
enum class NativeSettingsExitRoute {OriginReplay,OriginNormal,Consume,Inspect};
// Called before any native exit predicate. A pending owned exit always wins:
// native canExit=false still enters its reload-on-exit branch in 1.26.51.01.
inline NativeSettingsExitRoute nativeSettingsExitRoute(bool ownedRoot,bool exactReplayPermit,
    bool replayOwnerCurrent,bool pendingOrBusy,bool nativeExitEligible) noexcept {
    if(exactReplayPermit)return replayOwnerCurrent?NativeSettingsExitRoute::OriginReplay:NativeSettingsExitRoute::Consume;
    if(!ownedRoot)return NativeSettingsExitRoute::OriginNormal;
    if(pendingOrBusy)return NativeSettingsExitRoute::Consume;
    return nativeExitEligible?NativeSettingsExitRoute::Inspect:NativeSettingsExitRoute::OriginNormal;
}
// The saved native callback may be copied or invoked after its originating
// world has ended. Claiming is irreversible and permits one action only.
struct NativeSettingsApplyOnce {
    std::uint64_t token{},world{};
    std::atomic_bool claimed{};
    bool claim(std::uint64_t currentToken,std::uint64_t currentWorld) noexcept {
        return token&&world&&token==currentToken&&world==currentWorld&&!claimed.exchange(true);
    }
};
enum class NativeSettingsApplyAdmission {Stale,Execute,Duplicate};
inline NativeSettingsApplyAdmission nativeSettingsApplyAdmission(NativeSettingsApplyOnce& once,
    std::uint64_t token,std::uint64_t world,bool contextCurrent) noexcept {
    if(!contextCurrent||!once.token||!once.world||once.token!=token||once.world!=world)
        return NativeSettingsApplyAdmission::Stale;
    return once.claim(token,world)?NativeSettingsApplyAdmission::Execute:NativeSettingsApplyAdmission::Duplicate;
}
// These facts belong to the retained editor, not the discarded exit event or
// its selected target. A changed selection may start a fresh transaction only
// after old owners resumed and the underlying live session remains unchanged.
struct NativeSettingsCancelledRetryView {
    bool nativeSettings{},peersResumed{},peersOpen{},pendingReleased{},notReplayed{},notNavigated{},noCompletion{};
    bool activeOperation{},ownerCurrent{},ownerThread{},sameRoot{},sameModel{},sameController{},sameSource{};
    bool sameTopScene{},settingsScreen{},globalPolicy{},oldGlobalUnchanged{},otherStacksUnchanged{},callbacksIdle{};
};
inline bool nativeSettingsCancelledRetry(WorldPackTransaction& tx,NativeSettingsCancelledRetryView const& v) noexcept {
    bool current=v.nativeSettings&&v.peersOpen&&v.pendingReleased&&v.notReplayed&&v.notNavigated&&v.noCompletion
        &&v.activeOperation&&v.ownerCurrent&&v.ownerThread&&v.sameRoot&&v.sameModel&&v.sameController&&v.sameSource
        &&v.sameTopScene&&v.settingsScreen&&v.globalPolicy&&v.oldGlobalUnchanged&&v.otherStacksUnchanged&&v.callbacksIdle;
    return tx.resetCancelledForRetry(v.peersResumed,current);
}
// Generated MinecraftScreenModel member layout does not match the pinned
// native secondary base. Resolve through primary-interface virtual getters
// and retain the Game-owned shared client; never read model owner members.
template<class Model,class Game>
auto nativeSettingsClient(Model const& model,Game& game,std::uintptr_t observed) {
    using Client=typename decltype(game.getClientInstanceMap())::mapped_type;
    if(!observed)return Client{};
    auto clients=game.getClientInstanceMap();
    if(clients.size()!=1)return Client{};
    auto found=clients.find(model.getSubClientId());
    if(found==clients.end()||!found->second)return Client{};
    auto client=found->second;
    if(reinterpret_cast<std::uintptr_t>(client.get())!=observed
        ||&client->getMinecraftGame_DEPRECATED()!=&game
        ||&model.getResourcePackManager()!=&client->getResourcePackManager())return Client{};
    return client;
}
// Native Settings owns selection, entitlement and apply. This scope changes
// only the same client's preGame query in construction/setter scopes or the
// independently verified live Global Settings editor below.
struct NativeSettingsPackContext {
    std::uintptr_t controller{}, client{}, manager{};
    std::uint64_t world{};
    bool includeGlobal{};
    bool allowPreGame{true};
    bool valid() const noexcept { return controller && client && manager && world; }
};
inline thread_local NativeSettingsPackContext nativeSettingsPackContext;
struct NativeSettingsConstructorFlags {bool global{},reloadOnExit{};};
inline NativeSettingsConstructorFlags nativeSettingsConstructorFlags(bool global,bool reloadOnExit,bool scoped) noexcept {
    return {global||scoped,reloadOnExit||scoped};
}
// Root global/navigation and Root reload are independent native policies.
// Called only at the normal MCAPI _init return with a retained model/client
// and the General that was created in that exact synchronous initialization.
struct NativeSettingsRootReloadView {
    bool initializationOwned{},sameModel{},sameGeneral{},ownerCurrent{},settingsMode{},
        globalPolicy{},childGlobal{},childReload{},sourcePresent{};
};
inline bool nativeSettingsRootReload(bool original,NativeSettingsRootReloadView const& v) noexcept {
    return original||(v.initializationOwned&&v.sameModel&&v.sameGeneral&&v.ownerCurrent&&v.settingsMode
        &&v.globalPolicy&&v.childGlobal&&v.childReload&&v.sourcePresent);
}
enum class NativeSettingsRootExitReason {
    Hold,EventMismatch,WrongThread,Pending,RootReloadDisabled,GeneralMissing,CanExitDenied,ImportsPending,
    EventExpired,ModelMissing,GeneralMismatch,SourceMissing,ChildGlobalDisabled,ChildReloadDisabled,
    SettingsMode,OwnerChanged,RevisionInvalid,SceneChanged,GlobalChanged,OtherStacksChanged,
    NoChange,CopyExpired,BeginRejected,Exception,ReplayOwnerChanged,Replay
};
struct NativeSettingsRootExitView {
    bool exitEvent{},ownerThread{},pending{},rootReload{},generalPresent{},canExit{},importsIdle{},eventAlive{},
        modelPresent{},sameGeneral{},sourcePresent{},childGlobal{},childReload{},settingsMode{},ownerCurrent{},
        revisionValid{},sameScene{},globalUnchanged{},otherStacksUnchanged{},selectionUnchanged{};
};
struct NativeSettingsRootExitDecision {NativeSettingsRootExitReason reason;NativeSettingsExitRoute route;};
inline NativeSettingsRootExitDecision nativeSettingsRootExit(NativeSettingsRootExitView const& v) noexcept {
    using Reason=NativeSettingsRootExitReason;using Route=NativeSettingsExitRoute;
    if(!v.exitEvent)return {Reason::EventMismatch,Route::OriginNormal};
    if(!v.ownerThread)return {Reason::WrongThread,Route::Consume};
    if(v.pending)return {Reason::Pending,Route::Consume};
    if(!v.rootReload)return {Reason::RootReloadDisabled,Route::OriginNormal};
    // Native reload-on-exit is independent of canExit/global. Once enabled,
    // an unverifiable owned exit must not fall through to an unpaused save.
    for(auto [ok,reason]:{
        std::pair{v.generalPresent,Reason::GeneralMissing},std::pair{v.canExit,Reason::CanExitDenied},
        std::pair{v.importsIdle,Reason::ImportsPending},std::pair{v.eventAlive,Reason::EventExpired},
        std::pair{v.modelPresent,Reason::ModelMissing},std::pair{v.sameGeneral,Reason::GeneralMismatch},
        std::pair{v.sourcePresent,Reason::SourceMissing},std::pair{v.childGlobal,Reason::ChildGlobalDisabled},
        std::pair{v.childReload,Reason::ChildReloadDisabled},std::pair{v.settingsMode,Reason::SettingsMode},
        std::pair{v.ownerCurrent,Reason::OwnerChanged},std::pair{v.revisionValid,Reason::RevisionInvalid},
        std::pair{v.sameScene,Reason::SceneChanged},std::pair{v.globalUnchanged,Reason::GlobalChanged},
        std::pair{v.otherStacksUnchanged,Reason::OtherStacksChanged}})
        if(!ok)return {reason,Route::Consume};
    return v.selectionUnchanged?NativeSettingsRootExitDecision{Reason::NoChange,Route::OriginNormal}
        :NativeSettingsRootExitDecision{Reason::Hold,Route::Inspect};
}
class NativeSettingsPackScope {
    NativeSettingsPackContext previous;
public:
    explicit NativeSettingsPackScope(NativeSettingsPackContext current) noexcept
        : previous(nativeSettingsPackContext) { nativeSettingsPackContext=current; }
    ~NativeSettingsPackScope() { nativeSettingsPackContext=previous; }
    NativeSettingsPackScope(NativeSettingsPackScope const&)=delete;
};
inline bool nativeSettingsPreGame(bool original, std::uintptr_t client, std::uint64_t world) noexcept {
    auto const& c=nativeSettingsPackContext;
    return original || (c.valid() && c.allowPreGame && c.client==client && c.world==world);
}
struct NativeSettingsEditorView {
    std::uintptr_t client{};
    std::uint64_t world{};
    bool ownerThread{},sessionOpen{},rootAlive{},sameController{},sameTopScene{},globalTab{},settingsScreen{},ownerCurrent{};
};
inline thread_local bool nativeSettingsEditorSuppressed;
class NativeSettingsEditorPause {
    bool previous;
public:
    NativeSettingsEditorPause() noexcept:previous(nativeSettingsEditorSuppressed){nativeSettingsEditorSuppressed=true;}
    ~NativeSettingsEditorPause(){nativeSettingsEditorSuppressed=previous;}
    NativeSettingsEditorPause(NativeSettingsEditorPause const&)=delete;
};
inline bool nativeSettingsEditorPreGame(bool original, NativeSettingsPackContext const& c,
    NativeSettingsEditorView const& v) noexcept {
    return original || (!nativeSettingsEditorSuppressed && c.valid() && c.includeGlobal && c.client==v.client && c.world==v.world
        && v.ownerThread && v.sessionOpen && v.rootAlive && v.sameController
        && v.sameTopScene && v.globalTab && v.settingsScreen && v.ownerCurrent);
}
// A native false Global policy remains binding with world/server resources.
// Only an ordinary local world without a Level resource stack can opt in.
// Extra grant is scoped to the exact audited *offline duplicate world*;
    // a normal Level RP/mandatory server stack must not gain Global access.
inline bool nativeSettingsMayIncludeGlobal(
    bool original,bool multiplayer,bool levelEmpty,bool verifiedClone=false) noexcept {
    return original || (!multiplayer&&(levelEmpty||verifiedClone));
}
template<class Origin>
decltype(auto) withNativeSettingsGlobal(bool& flag, std::uintptr_t manager,
    std::uint64_t world, Origin&& origin) {
    auto const& c=nativeSettingsPackContext;
    bool scoped=c.valid() && c.manager==manager && c.world==world && c.includeGlobal;
    struct Restore { bool& flag; bool old; bool changed; ~Restore(){if(changed)flag=old;} } restore{flag,flag,scoped};
    if(scoped)flag=true;
    return std::forward<Origin>(origin)();
}
}
