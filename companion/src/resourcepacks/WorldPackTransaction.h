#pragma once
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/WorldPackReloadApi.h"

namespace praxis::companion::packs {
struct WorldPackOwner {
    std::uintptr_t game{},manager{},client{},renderer{};
    std::uint64_t world{},thread{};
    bool present{},ready{},pending{};
    bool same(WorldPackOwner const& b) const noexcept {
        return game&&manager&&client&&renderer&&world&&thread&&game==b.game&&manager==b.manager
            &&client==b.client&&renderer==b.renderer&&world==b.world&&thread==b.thread;
    }
    bool usable() const noexcept {return game&&manager&&client&&renderer&&world&&thread&&present&&ready&&!pending;}
};
enum class WorldSwitchPhase {Idle,Preparing,Pausing,AwaitApply,Reloading,Reacquiring,Complete,Restored,Cancelled,Recovery};
enum class WorldSwitchAction {None,NativeSettled};
class WorldPackTransaction {
public:
    WorldSwitchPhase phase{WorldSwitchPhase::Idle};
    WorldPackOwner owner;
    std::uint64_t token{},attempt{},deadline{};
    WorldPackReloadRequest participant;
    std::vector<std::string> oldGlobal,targetGlobal,expectedBefore,lastStable;
    bool started{},startReturned{},finished{},fullVanilla{},setterRejected{},restoring{},needRollback{},changed{},geometryRequested{},awaitingSelection{};
    unsigned stable{};
    static constexpr std::uint64_t PrepareBudget=30000,ReloadBudget=180000;
    bool busy() const noexcept {return phase>=WorldSwitchPhase::Preparing&&phase<=WorldSwitchPhase::Reacquiring;}
    bool beforeNative() const noexcept {return phase==WorldSwitchPhase::Preparing||phase==WorldSwitchPhase::Pausing||phase==WorldSwitchPhase::AwaitApply;}
    bool begin(std::uint64_t id,WorldPackOwner const& context,std::uint64_t now) noexcept {
        if(busy()||!id||!context.usable())return false;
        token=id;owner=context;awaitingSelection=false;phase=WorldSwitchPhase::Preparing;deadline=now+PrepareBudget;return true;
    }
    void prepared(std::vector<std::string> old,std::vector<std::string> target){
        oldGlobal=std::move(old);targetGlobal=std::move(target);expectedBefore=oldGlobal;phase=WorldSwitchPhase::Pausing;
    }
    void awaiting(WorldPackReloadRequest r,std::uint64_t now,bool userSelection=false) noexcept {
        // Human selection has no processing deadline. World/owner checks still
        // run each tick; preparation and a queued native callback stay bounded.
        participant=r;phase=WorldSwitchPhase::AwaitApply;awaitingSelection=userSelection;
        deadline=userSelection?0:now+PrepareBudget;
    }
    void queueCallback(std::uint64_t now) noexcept {
        if(phase!=WorldSwitchPhase::AwaitApply||!awaitingSelection){seal();return;}
        awaitingSelection=false;deadline=now+ReloadBudget;
    }
    bool callback(WorldPackOwner const& context,std::uint64_t now) noexcept {
        if(phase!=WorldSwitchPhase::AwaitApply)return false;
        if(awaitingSelection||now>deadline||!owner.same(context)||!context.present||context.pending){seal();return false;}
        ++attempt;started=startReturned=finished=setterRejected=geometryRequested=false;stable=0;lastStable.clear();
        phase=WorldSwitchPhase::Reloading;deadline=now+ReloadBudget;return true;
    }
    bool nativeStart(WorldPackOwner const& actual,std::uint64_t seedAttempt,bool full) noexcept {
        if(phase!=WorldSwitchPhase::Reloading||started||seedAttempt!=attempt||!owner.same(actual)||!actual.present){seal();return false;}
        started=true;fullVanilla=full;return true;
    }
    void startResult(std::uint64_t actualAttempt,bool normal) noexcept {
        if(actualAttempt!=attempt||!started||!normal){seal();return;}startReturned=true;
    }
    void finish(std::uint64_t actualAttempt,bool normal) noexcept {
        if(phase!=WorldSwitchPhase::Reloading||actualAttempt!=attempt||!started||finished||!normal){seal();return;}finished=true;
    }
    // The API route joins selection, actual native start/finish with callable
    // provenance, UI reload, later loading query and current-renderer draw.
    // Stack equality validates selection; it is never a completion signal.
    bool settleExecuted(WorldPackOwner const& current,bool frameReceipt,bool otherUnchanged,
        std::vector<std::string> const& global,std::uint64_t now) noexcept {
        if(phase!=WorldSwitchPhase::Reloading||!frameReceipt||!started||!startReturned||!finished)return false;
        if(now>deadline||!current.usable()||owner.game!=current.game||owner.manager!=current.manager
            ||owner.client!=current.client||owner.world!=current.world||owner.thread!=current.thread
            ||!otherUnchanged||global!=targetGlobal){seal();return false;}
        try{lastStable=global;}catch(...){seal();return false;}
        owner.renderer=current.renderer;
        participant.renderer=current.renderer;
        stable=0;needRollback=false;geometryRequested=false;
        phase=WorldSwitchPhase::Reacquiring;return true;
    }
    WorldSwitchAction poll(WorldPackOwner const& current,bool callbacksIdle,bool normalScreen,
        bool otherUnchanged,std::vector<std::string> const& global,std::uint64_t now) noexcept {
        if(!busy())return WorldSwitchAction::None;
        if(!owner.same(current)||!current.present){seal();return WorldSwitchAction::None;}
        if(!awaitingSelection&&now>deadline){if(beforeNative())cancel();else seal();return WorldSwitchAction::None;}
        if(phase!=WorldSwitchPhase::Reloading)return WorldSwitchAction::None;
        if(!started||!startReturned||!finished||!callbacksIdle||!normalScreen||!current.usable()){
            stable=0;lastStable.clear();return WorldSwitchAction::None;
        }
        if(!otherUnchanged){seal();return WorldSwitchAction::None;}
        if(lastStable!=global){stable=0;try{lastStable=global;}catch(...){seal();return WorldSwitchAction::None;}}
        if(++stable<2)return WorldSwitchAction::None;
        needRollback=global!=oldGlobal&&(setterRejected||global!=targetGlobal);
        if(restoring&&global!=oldGlobal){seal();return WorldSwitchAction::None;}
        if(global==oldGlobal&&global!=targetGlobal)restoring=true;
        phase=WorldSwitchPhase::Reacquiring;return WorldSwitchAction::NativeSettled;
    }
    void completeParticipants() noexcept {phase=restoring?WorldSwitchPhase::Restored:WorldSwitchPhase::Complete;}
    void rollback(std::vector<std::string> actual,std::uint64_t now){
        restoring=true;needRollback=false;expectedBefore=std::move(actual);
        phase=WorldSwitchPhase::Pausing;deadline=now+PrepareBudget;
    }
    void cancel() noexcept {if(beforeNative())phase=WorldSwitchPhase::Cancelled;}
    // Only the caller that verified every retained peer's old-epoch resume and
    // the live editor session may clear a pre-native attempt for another exit.
    bool resetCancelledForRetry(bool peersResumed,bool sessionCurrent) noexcept {
        if(phase!=WorldSwitchPhase::Cancelled||!peersResumed||!sessionCurrent||!token
            ||attempt||started||startReturned||finished||restoring||needRollback||changed||geometryRequested)return false;
        *this=WorldPackTransaction{};return true;
    }
    void seal() noexcept {phase=WorldSwitchPhase::Recovery;}
};
// This runs on the verified game update owner after the corresponding normal
// reload has settled. The SDK method marks existing render chunks dirty; the
// engine still owns incremental tessellation, old meshes and GPU submission.
// A request receipt is not a mesh-completion or framebuffer receipt.
template<class Rebuild> bool requestWorldGeometryRebuild(WorldPackTransaction& tx,
    WorldPackOwner const& current, bool normalScreen, Rebuild&& rebuild) noexcept {
    if(tx.phase!=WorldSwitchPhase::Reacquiring)return false;
    if(tx.geometryRequested)return true;
    if(!tx.owner.same(current)||!current.usable()||!normalScreen){tx.seal();return false;}
    try{std::forward<Rebuild>(rebuild)();tx.geometryRequested=true;return true;}
    catch(...){tx.seal();return false;}
}
// Ready may have retired old owners; private new GPU resources must complete,
// while native service handles use the explicit ABI4 lifetime policy. Neither
// is a first-frame framebuffer receipt. Cancelled keeps old owners/epoch; no resource
// mutation occurred, so resuming those owners does not require retiring old GPU.
inline bool participantReceipt(WorldPackReloadRequest const& r,WorldPackReloadReply const& p,
    WorldPackReloadPhase phase,std::uint64_t epoch) noexcept {
    return p.bytes==sizeof(p)&&p.abi==WorldPackReloadAbi&&p.token==r.token&&p.world==r.world
        &&p.client==r.client&&p.renderer==r.renderer&&p.epoch==epoch&&p.phase==phase&&!p.error
        &&p.admissionClosed&&(phase==WorldPackReloadPhase::Ready||p.oldOwnersRetained)
        &&!p.cpuReaders&&!p.asyncJobs&&!p.nativeBorrowers
        &&(phase==WorldPackReloadPhase::Cancelled||p.ownedGpu==OwnedGpuFence::Complete||p.ownedGpu==OwnedGpuFence::NoOwnedSubmission || p.ownedGpu==OwnedGpuFence::NativeManaged);
}
}
