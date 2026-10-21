#pragma once
#include "resourcepacks/WorldPackTransaction.h"
#include <array>

namespace praxis::companion::packs {
enum class WorldPeerAction {None,Navigate,Rollback};
// Engine-managed geometry and a human Settings selection are independent.
// The custom Apply/Reload route owns geometry through the native engine, but
// has no Settings selection event to release an indefinite wait.
struct WorldPeerStepPolicy {
    bool engineManagesGeometry{};
    bool waitForSettingsSelection{};
};
inline constexpr WorldPeerStepPolicy worldPeerStepPolicy(bool apiReload,bool nativeSettings) noexcept {
    return {apiReload,nativeSettings};
}
// Production coordinator and the joined fixtures share these exact C ABI
// calls/receipts/transitions. Query is polling, never a native drain operation.
struct WorldPackPeers {
    std::array<WorldPackReloadParticipant,2> peers{};
    unsigned count{1},mask{};
    bool settledSent{},cancelVerified{},pauseComplete{},recoveryStopIssued{};
    std::array<PackPeerObservation,2> observations;
    explicit WorldPackPeers(WorldPackReloadParticipant praxis){peers[0]=praxis;}
    bool call(WorldPackTransaction& tx,unsigned i,WorldPackReloadCommand command,WorldPackReloadReply& reply){
        reply={};bool accepted=peers[i]&&peers[i](command,&tx.participant,&reply)==1;
        // Observe the existing call once; do not issue a diagnostic Query or
        // substitute this copied summary for participantReceipt().
        observations[i]={true,accepted,command,reply.epoch,reply.phase,reply.error,reply.cpuReaders,reply.asyncJobs,reply.nativeBorrowers,reply.ownedGpu,false};
        return accepted;
    }
    // Recovery is terminal. One Stop command per peer is enough to close
    // admission; repeated per-frame Stop calls can re-enter invalidated GPU
    // owners and spam callbacks while the game continues rendering.
    void stop(WorldPackTransaction& tx){
        if(recoveryStopIssued)return;
        recoveryStopIssued=true;
        for(unsigned i=0;i<count;++i)if(mask&(1u<<i)){
            WorldPackReloadReply reply;
            call(tx,i,WorldPackReloadCommand::Stop,reply);
        }
    }
    bool transition(WorldPackTransaction& tx,WorldPackReloadCommand command){
        bool accepted=true;for(unsigned i=0;i<count;++i){WorldPackReloadReply reply;if(!call(tx,i,command,reply))accepted=false;}return accepted;
    }
    // Native atlas/pack mutation executes *inside* the engine callback.
    // NativeActive permits outstanding owned CPU work, unlike Prepared,
    // whose original zero-borrower/GPU-fence proof was checked before
    // the actual native mutation was started. Neither status forgives an
    // invalidated peer, a foreign owner or a stale epoch.
    static bool nativeActiveReceipt(WorldPackReloadRequest const& r,
                                    WorldPackReloadReply const& p) noexcept {
        return p.bytes==sizeof(p)&&p.abi==WorldPackReloadAbi
            &&p.token==r.token&&p.world==r.world
            &&p.client==r.client&&p.renderer==r.renderer
            &&p.epoch==r.oldEpoch
            &&p.phase==WorldPackReloadPhase::NativeActive
            &&!p.error&&p.admissionClosed&&p.oldOwnersRetained;
    }
    bool nativeActive(WorldPackTransaction& tx){
        for(unsigned i=0;i<count;++i){
            WorldPackReloadReply reply;
            bool accepted=call(tx,i,WorldPackReloadCommand::Query,reply);
            bool matched=accepted&&nativeActiveReceipt(tx.participant,reply);
            observations[i].receiptMatched=matched;
            if(!matched)return false;
        }
        return true;
    }
    bool ready(WorldPackTransaction& tx,WorldPackReloadPhase phase,std::uint64_t epoch){
        for(unsigned i=0;i<count;++i){WorldPackReloadReply reply;
            bool accepted=call(tx,i,WorldPackReloadCommand::Query,reply);
            bool matched=accepted&&participantReceipt(tx.participant,reply,phase,epoch);
            observations[i].receiptMatched=matched;if(!matched)return false;}
        return true;
    }
    static bool resumedOldReceipt(WorldPackReloadRequest const& r,WorldPackReloadReply const& p) noexcept {
        return p.bytes==sizeof(p)&&p.abi==WorldPackReloadAbi&&p.token==r.token&&p.world==r.world
            &&p.client==r.client&&p.renderer==r.renderer&&p.epoch==r.oldEpoch
            &&p.phase==WorldPackReloadPhase::Idle&&!p.error&&!p.admissionClosed
            &&!p.cpuReaders&&!p.asyncJobs&&!p.nativeBorrowers;
    }
    static bool resumedNewReceipt(WorldPackReloadRequest const& r,WorldPackReloadReply const& p) noexcept {
        return p.bytes==sizeof(p)&&p.abi==WorldPackReloadAbi&&p.token==r.token&&p.world==r.world
            &&p.client==r.client&&p.renderer==r.renderer&&p.epoch==r.nextEpoch
            &&p.phase==WorldPackReloadPhase::Idle&&!p.error&&!p.admissionClosed
            &&!p.cpuReaders&&!p.asyncJobs&&!p.nativeBorrowers;
    }
    // The success return from a foreign peer is not enough: its same-call
    // ABI reply must attest an open Idle owner at the new epoch. Never use a
    // speculative Query after resume to substitute for the command receipt.
    bool resumeReady(WorldPackTransaction& tx){
        bool accepted=true;
        for(unsigned i=0;i<count;++i)if(mask&(1u<<i)){
            WorldPackReloadReply reply;
            bool matched=call(tx,i,WorldPackReloadCommand::Resume,reply)
                &&resumedNewReceipt(tx.participant,reply);
            observations[i].receiptMatched=matched;
            if(!matched)accepted=false;
        }
        return accepted;
    }
    void pause(WorldPackTransaction& tx,std::uint64_t peerToken,std::uint64_t epoch,std::uint64_t now){
        tx.participant={};auto& r=tx.participant;r.token=peerToken;r.world=tx.owner.world;r.client=tx.owner.client;
        r.renderer=tx.owner.renderer;r.oldEpoch=epoch;r.nextEpoch=epoch+1;
        mask=0;settledSent=cancelVerified=pauseComplete=recoveryStopIssued=false;
        tx.deadline=now+WorldPackTransaction::PrepareBudget;
        for(unsigned i=0;i<count;++i){WorldPackReloadReply reply;
            if(!call(tx,i,WorldPackReloadCommand::Pause,reply)){tx.phase=WorldSwitchPhase::Cancelled;return;}
            mask|=1u<<i;
        }
        pauseComplete=true;
    }
    bool cancel(WorldPackTransaction& tx){
        for(unsigned i=0;i<count;++i)if(mask&(1u<<i)){
            WorldPackReloadReply reply;call(tx,i,WorldPackReloadCommand::CancelBeforeNative,reply);
            if(!call(tx,i,WorldPackReloadCommand::Query,reply)||!participantReceipt(tx.participant,reply,WorldPackReloadPhase::Cancelled,tx.participant.oldEpoch))return false;
        }
        for(unsigned i=0;i<count;++i)if(mask&(1u<<i)){
            WorldPackReloadReply reply;
            if(!call(tx,i,WorldPackReloadCommand::ResumeCancelled,reply)
                ||!resumedOldReceipt(tx.participant,reply)){
                // Peers resume independently. An earlier one may already be
                // Idle; never poll it for Cancelled again or leave GUI busy.
                // The existing step() Recovery path closes every retained peer.
                tx.seal();return false;
            }
        }
        mask=0;cancelVerified=pauseComplete;return true;
    }
    WorldPeerAction step(WorldPackTransaction& tx,std::uint64_t now,WorldPeerStepPolicy policy={}){
        if(tx.phase==WorldSwitchPhase::Pausing){
            for(unsigned i=0;i<count;++i){WorldPackReloadReply reply;call(tx,i,WorldPackReloadCommand::Prepare,reply);}
            if(ready(tx,WorldPackReloadPhase::Prepared,tx.participant.oldEpoch)){
                tx.awaiting(tx.participant,now,policy.waitForSettingsSelection);return WorldPeerAction::Navigate;
            }
        }
        // Native Settings delegates terrain reload to the original engine.
        // Omit only the mod dirty request; retain all peer readiness checks.
        if(tx.phase==WorldSwitchPhase::Reacquiring&&(tx.geometryRequested||policy.engineManagesGeometry)){
            if(!settledSent){settledSent=transition(tx,WorldPackReloadCommand::NativeSettled);if(!settledSent)tx.seal();}
            else if(ready(tx,WorldPackReloadPhase::Ready,tx.participant.nextEpoch)){
                if(!resumeReady(tx))tx.seal();
                else if(tx.needRollback)return WorldPeerAction::Rollback;
                else {mask=0;tx.completeParticipants();}
            }
        }
        if(tx.phase==WorldSwitchPhase::Cancelled&&mask)cancel(tx);
        if(tx.phase==WorldSwitchPhase::Recovery)stop(tx);
        return WorldPeerAction::None;
    }
};
}
