#include "resourcepacks/WorldPackReloadState.h"
#include "resourcepacks/WorldPackParticipant.h"
#include "resourcepacks/WorldPackPeers.h"
#include "resourcepacks/NativeSettingsPackScope.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <process.h>
#include <string_view>
using namespace praxis::companion::packs;
using namespace lholo::resourcepacks;
extern "C" int praxis_resource_reload_v4(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
extern "C" int lholo_resource_reload_v4(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
extern "C" int praxis_resource_reload_v3(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
extern "C" int lholo_resource_reload_v3(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
namespace {
unsigned checks{},resumes[2]{};int rejectResume=-1;
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int bridge(unsigned index,WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept {
    auto fn=index?lholo_resource_reload_v4:praxis_resource_reload_v4;
    if(c==WorldPackReloadCommand::ResumeCancelled){++resumes[index];
        if(int(index)==rejectResume){fn(WorldPackReloadCommand::Query,r,p);p->error=1;return 0;}}
    return fn(c,r,p);
}
int praxisPeer(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept{return bridge(0,c,r,p);}
int lholoPeer(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept{return bridge(1,c,r,p);}
struct Fixture {
    std::shared_ptr<int> module=std::make_shared<int>(1);
    State& b=state();WorldPackParticipant& a=praxisWorldParticipant();
    WorldPackTransaction tx;WorldPackPeers group{praxisPeer};
    WorldPackOwner owner{11,22,33,55,7,44,true,true,false};
    Fixture(){check(b.enable(module),"actual LHolo owner enabled");b.observe({11,33,55,22});
        group.count=2;group.peers[1]=lholoPeer;check(tx.begin(1,owner,10),"actual Praxis transaction admitted");tx.prepared({"old"},{"new"});}
    void pause(){group.pause(tx,1,1,11);check(group.mask==3,"both actual C exports pause");
        a.ownerServiced(true,1,true,false);a.ownerServiced(false,1,true,false);}
    void nativeSettled(){check(group.step(tx,12)==WorldPeerAction::Navigate,"actual pair prepared before navigation");
        check(group.transition(tx,WorldPackReloadCommand::BeginNative),"actual exports admit captured native phase");
        // Normal native settlement and owner services are explicit protocol
        // doubles. They are not engine, upload, GPU or framebuffer evidence.
        tx.phase=WorldSwitchPhase::Reacquiring;tx.geometryRequested=true;group.step(tx,14);
        check(a.resourceEpoch()==2&&b.snapshot()->reply.epoch==2,"actual providers advance epochs");}
    void rebuilt(){auto serial=b.snapshot()->serial;
        b.retiredOwner(Domain::Projection,serial);b.rebuilt(Domain::Projection,serial,true);
        b.retiredOwner(Domain::Icons,serial);b.rebuilt(Domain::Icons,serial,true);
        a.ownerServiced(true,2,true,true);a.ownerServiced(false,2,true,true);}
};
void run(std::string_view name){bool reproduce=name.starts_with("reproduce-");
    if(name=="cancel-retry-expired"||name=="cancel-retry-selection"||name=="cancel-retry-timeout"){
        Fixture f;
        std::optional<State::Lease> borrow;
        if(name=="cancel-retry-timeout")borrow.emplace(f.b.enter(Role::MeshJob));
        f.pause();if(!borrow)check(f.group.step(f.tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate,"actual providers prepare before expired/changed selection");
        if(borrow)f.tx.poll(f.owner,true,false,true,{"old"},30012);else f.tx.cancel();
        f.group.step(f.tx,30012,worldPeerStepPolicy(true,true));
        auto v=NativeSettingsCancelledRetryView{true,f.group.cancelVerified,!f.group.mask,true,true,true,true,true,true,true,
            true,true,true,true,true,true,true,true,true,true};
        if(borrow){
            check(!nativeSettingsCancelledRetry(f.tx,v)&&f.a.paused()&&f.b.snapshot()->closed,
                "actual LHolo retained job prevents same-root timeout reset and re-exit");
            borrow.reset();f.group.step(f.tx,30013,worldPeerStepPolicy(true,true));v.peersResumed=f.group.cancelVerified;v.peersOpen=!f.group.mask;
        }
        check(f.group.cancelVerified&&!f.a.paused()&&!f.b.snapshot()->closed&&f.b.snapshot()->reply.phase==WorldPackReloadPhase::Idle,
            "actual ABI4 pair supplies verified old-epoch Idle/open resume receipts");
        check(nativeSettingsCancelledRetry(f.tx,v)&&f.tx.phase==WorldSwitchPhase::Idle,
            "production retry helper reopens the same live Settings root after each actual peer cancellation");
        NativeSettingsPackContext context{66,33,22,7,true};NativeSettingsEditorView editor{33,7,true,true,true,true,true,true,true,true};
        check(nativeSettingsEditorPreGame(false,context,editor)
            &&nativeSettingsExitRoute(true,false,false,f.tx.phase!=WorldSwitchPhase::Idle,true)==NativeSettingsExitRoute::Inspect,
            "same-root edit and Back routes admit the fresh selection after actual peer resume");
        check(f.tx.begin(2,f.owner,30014),"same-root re-exit starts a fresh transaction token");f.tx.prepared({"old"},{"retry"});
        f.group.pause(f.tx,2,1,30015);f.a.ownerServiced(true,1,true,false);f.a.ownerServiced(false,1,true,false);
        check(f.group.step(f.tx,30016,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate&&f.group.mask==3&&!f.group.cancelVerified,
            "same-root retry reaches fresh Prepared on both actual participant exports");
        auto old=f.tx.participant;old.token=1;WorldPackReloadReply reply;
        check(!lholo_resource_reload_v4(WorldPackReloadCommand::BeginNative,&old,&reply)
            &&!praxis_resource_reload_v4(WorldPackReloadCommand::BeginNative,&old,&reply),
            "stale old peer token cannot mutate a fresh retry on either actual export");
        check(f.group.ready(f.tx,WorldPackReloadPhase::Prepared,1),"old token refusal preserves fresh Prepared identities");
    }else if(name=="ready"||name=="reproduce-ready"){
        Fixture f;f.pause();f.nativeSettled();f.rebuilt();auto p=f.b.snapshot()->reply;
        check(p.phase==WorldPackReloadPhase::Ready&&!p.oldOwnersRetained,"actual LHolo Ready follows old owner retirement");
        check(participantReceipt(f.tx.participant,p,WorldPackReloadPhase::Ready,2)!=reproduce,"Ready receipt uses its own retention contract");
        f.group.step(f.tx,15);
        if(reproduce)check(f.tx.phase==WorldSwitchPhase::Reacquiring&&f.a.paused()&&f.b.snapshot()->closed,"REPRO Ready remains closed with old predicate");
        else check(f.tx.phase==WorldSwitchPhase::Complete&&!f.a.paused()&&!f.b.snapshot()->closed&&!f.group.mask,"actual pair resumes after retired owners");
    }else if(name=="cancel-unknown"||name=="cancel-pending"||name=="reproduce-cancel"){
        Fixture f;f.b.submitted(Domain::Projection);f.pause();
        if(name=="cancel-pending")check(f.b.gpuReceipt(Domain::Projection,f.b.snapshot()->serial,OwnedGpuFence::Pending),"pending old GPU receipt remains pending");
        check(f.group.step(f.tx,12)==WorldPeerAction::None,"actual Unknown/Pending GPU blocks prepare");
        f.tx.cancel();check(f.group.transition(f.tx,WorldPackReloadCommand::CancelBeforeNative),"actual providers cancel retaining old epoch");
        auto p=f.b.snapshot()->reply;check(p.phase==WorldPackReloadPhase::Cancelled&&p.oldOwnersRetained&&p.admissionClosed,"actual Cancelled retains old owners");
        check(p.ownedGpu==OwnedGpuFence::Unknown||p.ownedGpu==OwnedGpuFence::Pending,"no fake GPU completion at cancellation");
        check(participantReceipt(f.tx.participant,p,WorldPackReloadPhase::Cancelled,1)!=reproduce,"Cancelled receipt uses retained same owners");
        f.group.step(f.tx,13);
        if(reproduce)check(f.group.mask==3&&f.a.paused()&&resumes[0]==0&&resumes[1]==0,"REPRO old GPU predicate prevents both resumptions");
        else check(!f.group.mask&&!f.a.paused()&&!f.b.snapshot()->closed&&f.a.resourceEpoch()==1&&f.b.snapshot()->reply.epoch==1,"both actual exports resume unchanged old epoch");
    }else if(name=="partial-cancel"||name=="first-cancel-reject"||name=="reproduce-partial"){
        Fixture f;f.pause();rejectResume=name=="first-cancel-reject"?0:1;f.tx.cancel();f.group.step(f.tx,13);
        if(reproduce){check(!f.a.paused()&&f.b.snapshot()->closed&&f.group.mask==3,"REPRO first peer resumes and second rejects");
            f.group.step(f.tx,14);check(f.tx.phase==WorldSwitchPhase::Cancelled&&resumes[1]==1&&f.group.mask==3,"REPRO resumed Idle peer prevents retry reaching second peer");
            check(f.tx.busy()||(f.group.mask&&f.tx.phase==WorldSwitchPhase::Cancelled),"REPRO GUI remains busy");
        }else{check(f.tx.phase==WorldSwitchPhase::Recovery&&f.a.paused()&&f.b.snapshot()->closed,"partial/first resume refusal immediately seals both actual providers");
            check(f.a.state()==WorldPackReloadPhase::Invalidated&&f.b.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"existing Stop closes and retains both peers");
            auto before=resumes[1];f.group.step(f.tx,14);check(resumes[1]==before&&f.group.mask==3,"Recovery never retries a partially completed cancel resume");
            check(!f.tx.busy()&&f.tx.phase!=WorldSwitchPhase::Cancelled,"GUI exits cancellation busy into explicit Recovery");}
    }else if(name=="borrow-drain"){
        Fixture f;{
            auto lease=f.b.enter(Role::MeshJob);check(bool(lease),"actual owned asynchronous/native borrower admitted");f.pause();f.tx.cancel();f.group.step(f.tx,13);
            check(f.group.mask==3&&f.a.paused()&&f.b.snapshot()->closed&&resumes[0]==0,"actual retained job prevents premature cancellation resume");
            check(!f.b.mayPublish(lease),"actual pause denies stale job publication");}
        f.group.step(f.tx,14);check(!f.group.mask&&!f.a.paused()&&!f.b.snapshot()->closed,"actual job destructor drains before both old-owner resumptions");
    }else if(name=="unknown-ready"){
        Fixture f;f.pause();f.nativeSettled();f.b.submitted(Domain::Projection);f.rebuilt();f.group.step(f.tx,15);
        check(f.tx.phase==WorldSwitchPhase::Reacquiring&&f.a.paused()&&f.b.snapshot()->closed,"actual production Unknown GPU still blocks new owner Ready");
        check(f.b.snapshot()->reply.ownedGpu==OwnedGpuFence::Unknown,"chunk/CPU/retirement never substitute for native GPU completion");
    }else if(name=="native-ready"||name=="native-borrow-drain"){
        Fixture f;check(f.b.nativeResourcesObserved({11,33,55,22}),"actual normal physical owner registers native lifetime");
        f.b.submitted(Domain::Projection);
        std::optional<State::Lease> borrow;
        if(name=="native-borrow-drain")borrow.emplace(f.b.enter(Role::MeshJob));
        f.pause();
        if(borrow){check(f.group.step(f.tx,12)==WorldPeerAction::None&&f.a.paused(),"native policy waits actual peer borrower drain");borrow.reset();}
        f.nativeSettled();f.rebuilt();auto p=f.b.snapshot()->reply;
        check(p.phase==WorldPackReloadPhase::Ready && p.ownedGpu==OwnedGpuFence::NativeManaged,"actual production State reports new native owners without a false fence receipt");
        check(participantReceipt(f.tx.participant,p,WorldPackReloadPhase::Ready,2),"v4 coordinator accepts explicit native service ownership");
        f.group.step(f.tx,15);check(f.tx.phase==WorldSwitchPhase::Complete&&!f.a.paused()&&!f.b.snapshot()->closed,"actual two C exports resume the world switch with native resources");
    }else if(name=="native-icons-unknown"||name=="native-icons-pending"){
        Fixture f;check(f.b.nativeResourcesObserved({11,33,55,22}),"native service classification before private icon submission");
        f.b.submitted(Domain::Projection);f.b.submitted(Domain::Icons);f.pause();
        if(name=="native-icons-pending")check(f.b.gpuReceipt(Domain::Icons,f.b.snapshot()->serial,OwnedGpuFence::Pending),"private pending GPU receipt");
        check(f.group.step(f.tx,12)==WorldPeerAction::None&&f.tx.phase==WorldSwitchPhase::Pausing,"native lifetime cannot substitute actual private icon fence");
        check(f.b.snapshot()->reply.ownedGpu!=OwnedGpuFence::NativeManaged,"private incomplete receipts dominate native policy");
        f.tx.cancel();f.group.step(f.tx,13);check(!f.group.mask&&!f.a.paused()&&!f.b.snapshot()->closed&&f.b.snapshot()->reply.epoch==1,"cancel incomplete private GPU retains same native and private owners");
    }else if(name=="renderer-retarget"){
        Fixture f;check(f.b.nativeResourcesObserved({11,33,55,22}),"native policy established before replacement");
        f.pause();check(f.group.step(f.tx,12)==WorldPeerAction::Navigate,"both peers prepared before replacement");
        check(f.group.transition(f.tx,WorldPackReloadCommand::BeginNative),"both peers entered native phase");
        f.b.observe({11,33,56,22});
        check(f.tx.callback(f.owner,13),"specific native callback claimed");
        check(f.tx.nativeStart(f.owner,f.tx.attempt,true),"native start belongs to this attempt");
        f.tx.startResult(f.tx.attempt,true);f.tx.finish(f.tx.attempt,true);
        auto replacement=f.owner;replacement.renderer=56;
        check(f.tx.settleExecuted(replacement,true,true,{"new"},14),"explicit frame receipt retargets coordinator");
        f.group.step(f.tx,15,worldPeerStepPolicy(true,false));
        auto p=f.b.snapshot()->reply;
        check(f.tx.phase==WorldSwitchPhase::Reacquiring&&f.tx.participant.renderer==56
            &&f.a.resourceEpoch()==2&&p.epoch==2&&p.renderer==56&&p.ownedGpu==OwnedGpuFence::NativeManaged,
            "both production peers accept exact NativeSettled renderer and preserve native policy");
        f.rebuilt();check(f.group.ready(f.tx,WorldPackReloadPhase::Ready,2),"both new renderer Ready receipts match");
        f.group.step(f.tx,16,worldPeerStepPolicy(true,false));
        check(f.tx.phase==WorldSwitchPhase::Complete&&!f.a.paused()&&!f.b.snapshot()->closed&&!f.group.mask,
            "both updated production peers resume only with current renderer");
    }else if(name=="renderer-old-peer-failclosed"){
        Fixture f;f.pause();check(f.group.step(f.tx,12)==WorldPeerAction::Navigate,"both peers prepared");
        check(f.group.transition(f.tx,WorldPackReloadCommand::BeginNative),"both peers native active");
        f.b.observe({11,33,56,22});
        f.tx.phase=WorldSwitchPhase::Reacquiring;f.tx.geometryRequested=true;
        f.group.step(f.tx,14,worldPeerStepPolicy(true,false));
        check(f.tx.phase==WorldSwitchPhase::Recovery&&f.a.paused()&&f.b.snapshot()->closed,
            "old renderer NativeSettled cannot complete mixed peer pair");
        f.group.step(f.tx,15,worldPeerStepPolicy(true,false));
        check(f.a.state()==WorldPackReloadPhase::Invalidated
            &&f.b.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated
            &&resumes[0]==0&&resumes[1]==0,"both peers remain sealed without a mixed-version resume");
    }else if(name=="legacy-v3"){
        WorldPackReloadRequest r;r.abi=3;WorldPackReloadReply p;p.abi=3;
        check(!praxis_resource_reload_v3(WorldPackReloadCommand::Pause,&r,&p)&&p.error==1,"actual Praxis v3 export refuses new ownership semantics");
        p={};p.abi=3;check(!lholo_resource_reload_v3(WorldPackReloadCommand::Pause,&r,&p)&&p.error==1,"actual LHolo v3 export refuses new ownership semantics");
    }else if(name=="negative-receipts"){
        WorldPackReloadRequest r;r.token=1;r.world=7;r.client=33;r.renderer=55;r.oldEpoch=1;r.nextEpoch=2;
        for(auto phase:{WorldPackReloadPhase::Prepared,WorldPackReloadPhase::Ready,WorldPackReloadPhase::Cancelled}){
            WorldPackReloadReply p;p.token=1;p.world=7;p.client=33;p.renderer=55;p.epoch=phase==WorldPackReloadPhase::Ready?2:1;
            p.phase=phase;p.admissionClosed=1;p.oldOwnersRetained=1;p.ownedGpu=OwnedGpuFence::Complete;
            check(participantReceipt(r,p,phase,p.epoch),"valid exact phase receipt");
            auto test=[&](auto change){auto wrong=p;change(wrong);check(!participantReceipt(r,wrong,phase,p.epoch),"phase-specific exceptions preserve strict common receipt validation");};
            test([](auto& v){v.bytes=0;});test([](auto& v){++v.abi;});test([](auto& v){++v.token;});test([](auto& v){++v.world;});
            test([](auto& v){++v.client;});test([](auto& v){++v.renderer;});test([](auto& v){++v.epoch;});test([](auto& v){v.phase=WorldPackReloadPhase::Unknown;});
            test([](auto& v){v.error=1;});test([](auto& v){v.admissionClosed=0;});test([](auto& v){v.cpuReaders=1;});test([](auto& v){v.asyncJobs=1;});test([](auto& v){v.nativeBorrowers=1;});
            p.oldOwnersRetained=0;check(participantReceipt(r,p,phase,p.epoch)==(phase==WorldPackReloadPhase::Ready),"only Ready allows retired old owners");
            p.oldOwnersRetained=1;
            for(auto gpu:{OwnedGpuFence::Unknown,OwnedGpuFence::Pending}){p.ownedGpu=gpu;check(participantReceipt(r,p,phase,p.epoch)==(phase==WorldPackReloadPhase::Cancelled),"only retained pre-native Cancelled allows incomplete old GPU");}
            p.ownedGpu=OwnedGpuFence::NativeManaged;check(participantReceipt(r,p,phase,p.epoch),"v4 permits explicit native ownership with every exact owner/phase/epoch predicate intact");
        }
    }else check(false,"known fixture case");
    std::printf("Actual peer %.*s: %u checks PASS; native/GPU/loader NOT_RUN\n",int(name.size()),name.data(),checks);
}
}
int main(int argc,char** argv){
    if(argc==2){run(argv[1]);return 0;}
    check(argc==1,"fresh child case isolation");
    for(auto name:{"ready","cancel-unknown","cancel-pending","partial-cancel","first-cancel-reject","borrow-drain","unknown-ready","negative-receipts","native-ready","native-borrow-drain","native-icons-unknown","native-icons-pending","renderer-retarget","renderer-old-peer-failclosed","legacy-v3","cancel-retry-expired","cancel-retry-selection","cancel-retry-timeout"}){
        char const* args[]{argv[0],name,nullptr};check(_spawnv(_P_WAIT,argv[0],args)==0,"each actual singleton provider case passes in a fresh process");}
    std::printf("Actual peer integration supervisor: %u checks PASS; 18 cases\n",checks);return 0;
}
