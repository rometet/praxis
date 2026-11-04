#include "resourcepacks/R25NativeEntryAudit.h"
#include "resourcepacks/WorldPackPeers.h"
#include <array>
#include <cstdio>
#include <cstdlib>

using namespace praxis::companion::packs;
namespace {
unsigned checks{};
unsigned stops{},pauses{};
void check(bool b,char const* title){
    ++checks;
    if(!b){std::fprintf(stderr,"R25 FAIL: %s\n",title);std::exit(1);}
}
int fakePeer(WorldPackReloadCommand command,WorldPackReloadRequest const* request,
             WorldPackReloadReply* reply) noexcept {
    if(command==WorldPackReloadCommand::Stop)++stops;
    if(command==WorldPackReloadCommand::Pause)++pauses;
    if(reply){
        *reply={};
        if(request){
            reply->token=request->token;reply->world=request->world;
            reply->client=request->client;reply->renderer=request->renderer;
            reply->epoch=request->oldEpoch;
        }
    }
    return 1;
}
}
int main(){
    R25NativeEntryAudit complete{};
    complete.seeded=complete.noCycle=complete.tokenMatches=complete.attemptMatches=
       complete.ownerMatches=complete.engineOwner=complete.fullMatches=
       complete.peersPrepared=complete.receiptEntered=complete.transactionStarted=
       complete.peersBegan=true;
    check(r25NativeEntryCompleted(complete),"complete receipt");
    constexpr std::array<bool R25NativeEntryAudit::*,11> gates{
        &R25NativeEntryAudit::seeded,
        &R25NativeEntryAudit::noCycle,
        &R25NativeEntryAudit::tokenMatches,
        &R25NativeEntryAudit::attemptMatches,
        &R25NativeEntryAudit::ownerMatches,
        &R25NativeEntryAudit::engineOwner,
        &R25NativeEntryAudit::fullMatches,
        &R25NativeEntryAudit::peersPrepared,
        &R25NativeEntryAudit::receiptEntered,
        &R25NativeEntryAudit::transactionStarted,
        &R25NativeEntryAudit::peersBegan
    };
    for(std::size_t i=0;i<gates.size();++i){
        auto sample=complete;
        sample.*gates[i]=false;
        check(r25NativeEntryMissing(sample)==(1u<<i),"exact failure bit");
        check(!r25NativeEntryCompleted(sample),"failed gate not admitted");
    }
    WorldPackTransaction transaction{};
    transaction.phase=WorldSwitchPhase::Recovery;
    transaction.owner={1,2,3,4,5,6,true,true,false};
    WorldPackPeers peers(fakePeer);
    peers.peers[1]=fakePeer;peers.count=2;peers.mask=3;
    peers.step(transaction,1);
    check(stops==2,"one Stop to each peer");
    peers.step(transaction,2);
    peers.step(transaction,3);
    check(stops==2,"Recovery must not issue per-frame Stop");
    peers.pause(transaction,1,1,4);
    check(pauses==2,"next transaction pauses each peer");
    transaction.phase=WorldSwitchPhase::Recovery;
    peers.step(transaction,5);
    check(stops==4,"new transaction can stop once again");
    peers.step(transaction,6);
    check(stops==4,"new terminal phase stays idempotent");
    std::printf("R25 NativeEntry + RecoveryStop PASS (%u checks)\n",checks);
}
