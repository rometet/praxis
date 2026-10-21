#include "resourcepacks/WorldPackParticipant.h"
#include "resourcepacks/WorldPackNativeAdapter.h"
#include "WorldNativeApplicationReceiptCases.h"
#include "WorldEngineReloadCases.h"
#include "resourcepacks/WorldPackPeers.h"
#include "resourcepacks/NativeResourceIdentityGate.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include "resourcepacks/LocalOverlayStage.h"
#include "resourcepacks/LocalTerrainAtlasProof.h"
#include "resourcepacks/LocalAtlasColorPatch.h"
#include "resourcepacks/LocalNativeAtlasApplyControl.h"
#include "resourcepacks/ServerResourcePackAudit.h"
#include "resourcepacks/WorldPackPreflightDiagnostics.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <latch>
#include <thread>
#include <stdexcept>

using namespace praxis::companion;
using namespace praxis::companion::packs;
extern "C" int praxis_resource_reload_v4(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
WorldPackParticipant* providerA{};WorldPackParticipant* providerB{};
unsigned peerCallsA{},peerCallsB{};
OwnedGpuFence fakeGpu{OwnedGpuFence::Complete};bool corruptReceipt{},rejectPauseA{},rejectPauseB{};
unsigned resumeCorruption{};
void corruptResume(WorldPackReloadReply& p){switch(resumeCorruption){
    case 1:++p.token;break;case 2:++p.world;break;case 3:++p.client;break;case 4:++p.renderer;break;
    case 5:++p.epoch;break;case 6:p.phase=WorldPackReloadPhase::Cancelled;break;case 7:p.error=1;break;
    case 8:p.admissionClosed=1;break;case 9:p.cpuReaders=1;break;case 10:p.asyncJobs=1;break;
    case 11:p.nativeBorrowers=1;break;case 12:p.bytes=0;break;case 13:++p.abi;break;
}}
int peerA(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept {++peerCallsA;if(c==WorldPackReloadCommand::Pause&&rejectPauseA)return 0;return providerA->command(c,r,p);}
int peerB(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept {
    ++peerCallsB;if(c==WorldPackReloadCommand::Pause&&rejectPauseB)return 0;auto accepted=providerB->command(c,r,p);
    if(c==WorldPackReloadCommand::ResumeCancelled||c==WorldPackReloadCommand::Resume)corruptResume(*p);p->ownedGpu=fakeGpu;if(corruptReceipt)++p->renderer;return accepted;
}
WorldPackOwner owner(){return {11,22,33,55,7,44,true,true,false};}
struct Fixture {
    NativeHookLifetime lifetime;std::atomic<unsigned> callbacks{},queued{};TitlePackControl control;std::mutex mutex;
    WorldPackTransaction tx;WorldPackParticipant a,b;WorldPackPeers peers{peerA};WorldNativeContext native;
    std::function<void()> pending;std::uint64_t cycles{},nextPeerToken{1};unsigned setters{},origins{},rebuilds{};
    std::unique_ptr<WorldPackParticipant::Lease> heldBorrow;
    std::unique_ptr<WorldPackParticipant::AsyncLease> heldJob;
    explicit Fixture(unsigned hold=0){
        providerA=&a;providerB=&b;fakeGpu=OwnedGpuFence::Complete;corruptReceipt=rejectPauseA=rejectPauseB=false;resumeCorruption=0;peers.count=2;peers.peers[1]=peerB;
        check(lifetime.prepare([]{return true;}),"module pin");lifetime.registrationStarted();check(lifetime.open(),"native lifetime opens");
        TitlePackView view;view.available=true;view.canReload=true;view.inWorld=true;view.current.known=true;view.until=1000;control.publish(view);
        auto catalog=std::make_shared<Catalog>();catalog->complete=true;catalog->generation=1;catalog->packs.push_back({"a","a","uuid","1.0.0","","texture",true});
        Selection selection;selection.generation=1;selection.order={"a"};check(control.request(catalog,selection,10,true),"real GUI queue permits world reload");
        auto request=control.take();check(request&&request->reload,"same-stack reload request survives dequeue");
        check(tx.begin(request->token,owner(),10),"real world identity admitted");tx.prepared({"old"},{"new"});
        if(hold==1)heldBorrow=std::make_unique<WorldPackParticipant::Lease>(a,true);
        if(hold==2)heldJob=std::make_unique<WorldPackParticipant::AsyncLease>(a,1);
        peers.pause(tx,nextPeerToken,1,11);
    }
    void service(bool ready=false){auto epoch=a.resourceEpoch();a.ownerServiced(true,epoch,true,ready);a.ownerServiced(false,epoch,true,ready);
        epoch=b.resourceEpoch();b.ownerServiced(true,epoch,true,ready);b.ownerServiced(false,epoch,true,ready);}
    void prepared(){service();check(peers.step(tx,12)==WorldPeerAction::Navigate,"both actual providers prepare before normal navigation");}
    template<class Validate>void apply(Validate&& validate,std::uint64_t token=0){
        runWorldApply(lifetime,callbacks,control,mutex,tx,token?token:tx.participant.token,[]{return owner();},
            std::forward<Validate>(validate),[&]{return peers.ready(tx,WorldPackReloadPhase::Prepared,tx.participant.oldEpoch);},[&]{
                ++setters;{std::lock_guard lock(mutex);native=beginWorldCycle(tx,worldNativeContext,owner(),true,++cycles);
                    check(native.receipt(),"actual setter seed becomes this world native cycle");check(peers.transition(tx,WorldPackReloadCommand::BeginNative),"BeginNative belongs to the real Active entry");tx.startResult(native.attempt,true);}
                WorldNativeScope scope(native);bool lost{};
                wrapWorldCallable<void>(worldNativeContext,lifetime,callbacks,std::function<void()>([&]{
                    observeWorldFinish(mutex,tx,native,worldNativeContext,11,22,7,true,[&]{++origins;});
                }),[&](auto task){pending=std::move(task);return 1;},lost,TitleNativeAllocation{},&queued);
                check(!lost,"actual created finish callable inherits provenance");return TitleSetterResult{true,false};},13);
    }
    void apply(){apply([]{return true;});}
    void finish(){check(bool(pending)&&queued.load()==1,"engine-created queued callable retained and counted");std::thread nativeThread([&]{pending();});nativeThread.join();check(origins==1&&queued.load()==0,"finish origin once and queued count retires after async native body");}
    void settle(std::vector<std::string> const& global){check(tx.poll(owner(),true,true,true,global,20)==WorldSwitchAction::None,"first exact ready identity is insufficient");
        check(tx.poll(owner(),true,true,true,global,21)==WorldSwitchAction::NativeSettled,"second matching native settlement begins reacquisition");
        check(peers.step(tx,21)==WorldPeerAction::None&&a.resourceEpoch()==tx.participant.oldEpoch,"owner rebuild request precedes provider epoch transition");
        check(requestWorldGeometryRebuild(tx,owner(),true,[&]{++rebuilds;}),"normal world render owner requests incremental rebuild");
        check(requestWorldGeometryRebuild(tx,owner(),true,[&]{++rebuilds;})&&rebuilds==tx.attempt,"duplicate owner service does not duplicate rebuild");
        peers.step(tx,22);check(a.resourceEpoch()==tx.participant.nextEpoch&&b.resourceEpoch()==tx.participant.nextEpoch,"both provider epochs advance after real finish");}
    void clean(){check(callbacks.load()==0&&lifetime.readers()==0,"callback and module readers balanced");}
};
}
int main(){
    {
        LocalTerrainAtlasProof p{};
        p.stagedToken=7;p.world=42;
        p.candidateW=p.candidateH=p.uvSourceW=p.uvSourceH=16;
        p.u0=.25f;p.v0=.25f;p.u1=.265625f;p.v1=.265625f;
        p.gameAtlas=p.terrainManager=p.sameAtlasManager=p.textureGroup
            =p.sourcePathPowder=p.uvFinite=p.uvInsideAtlas
            =p.sourceDimensionsMatch=p.uvAreaNonzero=true;
        LocalNativeAtlasApplyControl controller;
        check(!controller.request(true,p,8),"wrong source stage token cannot enter atlas rebuild");
        check(controller.request(true,p,7),"complete exact stage/owner proof queues local-on once");
        check(!controller.request(true,p,7),"repeated local-on while pending cannot start");
        auto on=controller.take();
        check(on&&on->enable&&on->token==1&&!controller.take(),
            "native local-on work is one-shot and belongs to requested token");
        controller.begun(on->token,true);
        controller.statusCallback(on->token);
        controller.finished(on->token);
        controller.frame(on->token,1);
        auto result=controller.view();
        check(result.phase==NativeAtlasApplyPhase::Enabled
            &&result.statusCallbacks==1&&result.finished==1&&result.sourceHits==1
            &&result.renderVerified,"native finish + texture source hit + renderer frame gate local-on");
        check(controller.request(false,p,7),"local-off is permitted only after completed local-on");
        auto off=controller.take();
        check(off&&!off->enable&&off->token==2,"rollback obtains a distinct exact native token");
        controller.begun(off->token,true);
        controller.finished(off->token);
        controller.frame(off->token,0);
        check(controller.view().phase==NativeAtlasApplyPhase::Restored,
            "original source reload and frame complete restores local overlay state");
        check(controller.request(true,p,7),"after exact restore on can be selected anew");
    }
    {
        LocalTerrainAtlasProof p{};
        p.candidateW=p.candidateH=p.uvSourceW=p.uvSourceH=4;
        p.uvTexW=p.uvTexH=64;
        p.u0=.25f;p.v0=.5f;p.u1=.3125f;p.v1=.5625f;
        p.gameAtlas=p.terrainManager=p.sameAtlasManager=p.textureGroup
            =p.sourcePathPowder=p.uvFinite=p.uvInsideAtlas
            =p.sourceDimensionsMatch=p.uvAreaNonzero=true;
        auto plan=planLocalAtlasColorPatch(p);
        check(plan&&plan->x==16&&plan->y==32&&plan->width==4&&plan->height==4,
            "exact Native terrain UV maps to bounded pixel rectangle");
        std::vector<std::uint8_t> original(64*64*4,31),out=original;
        std::vector<std::uint8_t> local(4*4*4,247);
        check(applyLocalAtlasColorPatch(out,local,*plan),
            "RGBA-only local tile composited into detached native atlas CPU copy");
        check(out[(32*64+16)*4]==247&&out[(35*64+19)*4]==247
                &&out[(32*64+15)*4]==31&&out[(36*64+16)*4]==31,
            "only selected 4x4 tile changes, neighbors are preserved");
        check(!applyLocalAtlasColorPatch(out,{},{*plan}),
            "missing source RGBA rejected without touching atlas");
        p.u1=.32f;
        check(!planLocalAtlasColorPatch(p),"fractional/out-of-bounds mapping never rounds into neighbor");
        p.u1=.3125f;p.sameAtlasManager=false;
        check(!planLocalAtlasColorPatch(p),"inconsistent Game/Block atlas owner forbids CPU publication");
        check(original[0]==31&&out[0]==31,
            "detached copy allows rollback without mutating old resource image");
    }
    {
        LocalTerrainAtlasProof a{};
        a.candidateW=a.candidateH=a.uvSourceW=a.uvSourceH=16;
        a.u0=.25f;a.v0=.5f;a.u1=.26f;a.v1=.51f;
        check(terrainUvGeometryValid(a),"atlas proof admits finite normalized matching source UV geometry");
        a.uvSourceW=17;
        check(!terrainUvGeometryValid(a),"atlas proof rejects candidate/source dimension mismatch");
        a.uvSourceW=16;a.u1=2.f;
        check(!terrainUvGeometryValid(a),"atlas proof rejects out-of-bounds native atlas UV");
        a.u1=.26f;a.u0=.27f;
        check(!terrainUvGeometryValid(a),"atlas proof rejects inverted atlas UV");
        a.u0=.25f;a.u1=.26f;a.gameAtlas=a.terrainManager=a.sameAtlasManager
          =a.textureGroup=a.sourcePathPowder=a.uvFinite=a.uvInsideAtlas
          =a.sourceDimensionsMatch=a.uvAreaNonzero=true;
        check(a.mappingObserved(),"atlas ownership proof requires a complete native mapping");
        a.sameAtlasManager=false;
        check(!a.mappingObserved(),"atlas ownership mismatch refuses a renderer publication");
    }
    {
        std::string png(40,'\0');
        constexpr unsigned char signature[]{137,80,78,71,13,10,26,10};
        for(unsigned i=0;i<8;++i)png[i]=char(signature[i]);
        png[8]=0;png[9]=0;png[10]=0;png[11]=13;
        png.replace(12,4,"IHDR",4);
        png[16]=0;png[17]=0;png[18]=0;png[19]=16;
        png[20]=0;png[21]=0;png[22]=0;png[23]=16;
        png[24]=8;png[25]=6;png[26]=0;png[27]=0;
        auto m=localOverlayPngMetadata(png);
        check(m.valid&&m.width==16&&m.height==16&&m.bitDepth==8,
            "well-formed bounded PNG IHDR parsed independently of active RP");
        png[19]=0;
        check(!localOverlayPngMetadata(png).valid,"zero-width png rejected");
        png[19]=16;png[25]=5;
        check(!localOverlayPngMetadata(png).valid,"unsupported png color type rejected");
        png[25]=6;png.resize(8);
        check(!localOverlayPngMetadata(png).valid,"truncated png rejected");
        LocalOverlayStageControl c;
        auto cat=std::make_shared<Catalog>();
        cat->complete=true;cat->generation=7;cat->packs.push_back({"powder","Powder Snow","uuid","0.0.1","","texture",true});
        Selection s;s.generation=7;s.order={"powder"};
        check(!c.request(cat,s,false),"cannot stage from title instead of owner world");
        check(c.request(cat,s,true),"only one registered local selected PNG can be staged");
        check(!c.request(cat,s,true),"pending image job is single-flight");
        auto req=c.take();
        check(req&&req->token==1&&req->key=="powder","native stage request retains exact selected key");
        check(!c.take(),"single-flight stage dequeues once");
        c.complete(req->token,{}, {},{},false,"bad");
        check(c.view().phase==LocalOverlayStagePhase::Rejected,
            "failed native decode does not claim applied or GPU success");
        s.order={"nonexistent"};
        check(!c.request(cat,s,true),"unknown pack selection rejected before engine");
    }
    {
        ServerResourcePackSnapshot packless{false,true,false,0,3,0,2,5};
        check(!packless.serverOrWorldResourceContext()&&packless.safeToAttemptGlobalComposerInWorld(),
            "only non-server no-Level-RP context may attempt legacy Full composer");
        ServerResourcePackSnapshot server{true,false,false,4,3,1,2,10};
        check(server.serverOrWorldResourceContext()&&!server.safeToAttemptGlobalComposerInWorld(),
            "server Level RPs block legacy Full composer without preventing introspection");
        ServerResourcePackSnapshot levelLocal{false,true,false,2,3,0,2,7};
        check(levelLocal.serverOrWorldResourceContext()&&!levelLocal.safeToAttemptGlobalComposerInWorld(),
            "world-only resource packs also preserve original native full-stack boundary");
        auto level=std::vector<std::string>{"server-a","server-b"};
        auto global=std::vector<std::string>{"local"};
        auto base=std::vector<std::string>{"vanilla"};
        auto empty=std::vector<std::string>{};
        auto full=std::vector<std::string>{"server-a","local","vanilla"};
        check(serverResourceAssetLayer(full,level,global,empty,base,0)==ServerAssetLayer::Level,
            "asset origin reports server/Level membership without mutating IDs");
        check(serverResourceAssetLayer(full,level,global,empty,base,1)==ServerAssetLayer::Global,
            "asset origin reports global/local membership");
        check(serverResourceAssetLayer(full,level,global,empty,base,2)==ServerAssetLayer::Base,
            "asset origin reports base membership");
        check(serverResourceAssetLayer(full,level,global,empty,base,3)==ServerAssetLayer::Unknown,
            "out of range source position cannot fabricate origin");
        check(serverResourceAssetLayer(full,level,global,empty,base,-1)==ServerAssetLayer::Unknown,
            "negative native source position cannot fabricate origin");
        auto duplicate=std::vector<std::string>{"server-a"};
        check(serverResourceAssetLayer(full,level,duplicate,empty,base,0)==ServerAssetLayer::Multiple,
            "duplicate server/local pack IDs must report ambiguity");
    }
    {
        WorldNativeCompatibilityAdmission a{};
        check(!admitWorldNativeCompatibility(a),"world native compatibility defaults denied");
        a={true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
        check(admitWorldNativeCompatibility(a),"only fully owned prepared one-append local pack may enter native compatibility window");
        check(worldNativeCompatibilityFailures(a)==0,"successful native compatibility has zero missing predicates");
        check(worldNativeCompatibilityFailures({})==0x1ffff,"empty admission has all 17 missing predicates");
        {auto b=a;b.levelEmpty=false;check(worldNativeCompatibilityFailures(b)==(1u<<15),
            "world Level resource packs reject the experimental full-stack compatibility route");}
        {auto b=a;b.notMultiplayer=false;check(worldNativeCompatibilityFailures(b)==(1u<<14),
            "multiplayer worlds reject experimental full-stack route");}
        {auto b=a;b.identity=false;check(worldNativeCompatibilityFailures(b)==(1u<<5),
            "missing exact engine transaction provenance identifies matching gate bit");}
        auto test=[&](bool WorldNativeCompatibilityAdmission::*member){
            auto b=a;b.*member=false;
            check(!admitWorldNativeCompatibility(b),"each absent safety gate denies scoped inGame mask");
        };
        test(&WorldNativeCompatibilityAdmission::sameWorld);
        test(&WorldNativeCompatibilityAdmission::sameManager);
        test(&WorldNativeCompatibilityAdmission::identity);
        test(&WorldNativeCompatibilityAdmission::singleLocalAppend);
        test(&WorldNativeCompatibilityAdmission::prepared);
        test(&WorldNativeCompatibilityAdmission::loadReady);
        test(&WorldNativeCompatibilityAdmission::notMultiplayer);
        test(&WorldNativeCompatibilityAdmission::noNativeTask);
    }
    check(nativeRegisteredResourceIdentity(6,6,6,0,true,true),"ordinary native resource identity admitted");
    check(nativeRegisteredResourceIdentity(6,0,6,0,true,true),"native identity Invalid is supported only when separate manifest explicitly says Resources");
    check(!nativeRegisteredResourceIdentity(0,0,6,0,true,true)
        &&!nativeRegisteredResourceIdentity(4,0,6,0,true,true)
        &&!nativeRegisteredResourceIdentity(6,4,6,0,true,true)
        &&!nativeRegisteredResourceIdentity(6,0,6,0,false,true)
        &&!nativeRegisteredResourceIdentity(6,0,6,0,true,false),
        "malformed, behavior, foreign-type, wrong UUID or version are always rejected");
    worldEngineReloadCases(check);
    worldNativeApplicationReceiptCases(check);
    // Regression for the production group.step -> navigate -> selection callback
    // ordering. Both routes use the exact worldPeerStepPolicy used in tick().
    {
        auto custom=worldPeerStepPolicy(true,false);
        auto settings=worldPeerStepPolicy(true,true);
        check(custom.engineManagesGeometry&&!custom.waitForSettingsSelection,
            "custom Apply/Reload cannot wait for a Settings user selection event");
        check(settings.engineManagesGeometry&&settings.waitForSettingsSelection,
            "native Settings retains selection wait independently of engine geometry");
        for(bool reload:{false,true}){
            Fixture f;f.service();
            check(f.peers.step(f.tx,12,custom)==WorldPeerAction::Navigate,
                "custom Apply and current-active Reload navigate after both peers prepared");
            check(f.tx.phase==WorldSwitchPhase::AwaitApply&&!f.tx.awaitingSelection
                &&f.tx.deadline==12+WorldPackTransaction::PrepareBudget,
                "custom navigation arms bounded engine callback without indefinite human wait");
            unsigned writes{};
            check(f.tx.callback(owner(),13),"actual custom application scene callback is admitted");
            if(!reload)++writes; // Reload is for already-active resources, not edited candidates.
            check(f.tx.phase==WorldSwitchPhase::Reloading&&f.tx.attempt==1
                &&writes==unsigned(!reload),"selection callback owns one attempt and correct setter policy");
            check(f.peers.transition(f.tx,WorldPackReloadCommand::BeginNative),
                "custom engine reload begins only after both peers were prepared");
            check(f.tx.nativeStart(owner(),f.tx.attempt,true),"actual engine Active event admits current attempt");
            f.tx.startResult(f.tx.attempt,true);f.tx.finish(f.tx.attempt,true);
            check(f.tx.settleExecuted(owner(),true,true,f.tx.targetGlobal,17),
                "native finish and current-frame receipt settle custom Apply/Reload");
            check(!f.tx.geometryRequested&&f.peers.step(f.tx,18,custom)==WorldPeerAction::None,
                "custom route uses original engine geometry without a second mod rebuild");
            f.service(true);
            check(f.peers.step(f.tx,19,custom)==WorldPeerAction::None
                &&f.tx.phase==WorldSwitchPhase::Complete&&!f.peers.mask,
                "both mod peers resume after custom native engine reload");
            f.clean();
        }
        // A positive Resume result with a false/foreign reply must not open
        // old resources or report Complete. Keep both providers fail-closed.
        for(unsigned bad=1;bad<=13;++bad){
            Fixture f;f.service();
            check(f.peers.step(f.tx,12,custom)==WorldPeerAction::Navigate,
                "malformed Resume probe reached Prepared");
            check(f.tx.callback(owner(),13),"malformed Resume probe admitted application callback");
            check(f.peers.transition(f.tx,WorldPackReloadCommand::BeginNative),
                "malformed Resume probe advanced both providers to NativeActive");
            check(f.tx.nativeStart(owner(),f.tx.attempt,true),"malformed Resume probe real epoch active");
            f.tx.startResult(f.tx.attempt,true);f.tx.finish(f.tx.attempt,true);
            check(f.tx.settleExecuted(owner(),true,true,f.tx.targetGlobal,17),
                "malformed Resume probe settled only an owned native completion");
            f.peers.step(f.tx,18,custom);f.service(true);
            resumeCorruption=bad;f.peers.step(f.tx,19,custom);
            check(f.tx.phase==WorldSwitchPhase::Recovery&&f.peers.mask==3
                &&f.a.state()==WorldPackReloadPhase::Invalidated
                &&f.b.state()==WorldPackReloadPhase::Invalidated,
                "malformed affirmative peer Resume reply seals both providers");
            f.clean();
        }
        {
            Fixture f;f.service();
            check(f.peers.step(f.tx,12,settings)==WorldPeerAction::Navigate
                &&f.tx.awaitingSelection&&f.tx.deadline==0,
                "native Settings navigation retains user-selection wait without a deadline");
            f.tx.poll(owner(),true,true,true,{"old"},50000);
            check(f.tx.phase==WorldSwitchPhase::AwaitApply&&f.tx.awaitingSelection,
                "open Settings editor cannot time out before selection is queued");
            f.tx.queueCallback(50001);
            check(!f.tx.awaitingSelection&&f.tx.deadline==50001+WorldPackTransaction::ReloadBudget
                &&f.tx.callback(owner(),50002)&&f.tx.phase==WorldSwitchPhase::Reloading,
                "native Settings queueCallback releases selection and admits the actual callback");
            f.clean();
        }
        {
            Fixture f;f.service();f.peers.step(f.tx,12,settings);
            check(!f.tx.callback(owner(),13)&&f.tx.phase==WorldSwitchPhase::Recovery,
                "premature native Settings callback stays fail-closed without queued selection");
            f.clean();
        }
    }
    {
        auto drain=[](PackDiagnosticQueue& queue){std::vector<std::string> lines;
            while(queue.drain([&](char const* text){lines.emplace_back(text);})){}return lines;};
        constexpr std::uintptr_t game=0x100000000ull,praxis=0x600000000ull,lholo=0x700000000ull;
        auto local=worldDiagnosticAddress(game+0x139e70,game,0x8000000,false,0,false,praxis,lholo);
        check(local.module==WorldDiagnosticModule::Game&&local.offset==0x139e70&&local.resolution==1,
            "validated Game identity needs only its existing base and image-relative offset");
        auto foreign=worldDiagnosticAddress(praxis+0x23450,game,0x8000000,true,praxis,true,praxis,lholo);
        check(foreign.module==WorldDiagnosticModule::Praxis&&foreign.offset==0x23450&&foreign.resolution==2,
            "foreign formal thunk uses copied AllocationBase metadata and known Praxis pin handle");
        auto holo=worldDiagnosticAddress(lholo+64,game,0x8000000,true,lholo,true,praxis,lholo);
        check(holo.module==WorldDiagnosticModule::LHolo&&holo.offset==64,"existing LHolo module handle classifies metadata without lookup or header dereference");
        auto unknown=worldDiagnosticAddress(0x900000020ull,game,0x8000000,true,0x900000000ull,true,praxis,lholo);
        check(unknown.module==WorldDiagnosticModule::Unknown&&unknown.offset==32&&unknown.resolution==2,
            "unidentified image emits only allocation-relative offset and unknown module identity");
        check(worldDiagnosticAddress(0,game,1,false,0,false,praxis,lholo).resolution==0,
            "null formal identity remains explicitly unresolved");
        check(worldDiagnosticAddress(17,game,1,false,0,false,praxis,lholo).resolution==4,
            "metadata unavailable never claims an offset, module identity or resolution");
        auto nonimage=worldDiagnosticAddress(65,game,1,true,64,false,praxis,lholo);
        check(nonimage.resolution==3&&nonimage.offset==1,"non-image mapped address stays distinct from an image resolution");
        auto addressOwner=std::make_unique<PackDiagnosticQueue>();auto& addressQueue=*addressOwner;WorldPackPreflightDiagnostics addressTrace(addressQueue);
        check(!addressTrace.formal(11,false,0x139e70,foreign)&&addressTrace.failure()==11,
            "copied metadata never repairs a failed original formal-address predicate");addressTrace.finish(false,true);
        auto addressLines=drain(addressQueue);
        check(addressLines.size()==2&&addressLines[0].find("actual_module=Praxis")!=std::string::npos
            &&addressLines[0].find("actual_offset=144464")!=std::string::npos
            &&addressLines[0].find(std::to_string(praxis))==std::string::npos,
            "formal trace contains module-relative offset and no absolute module address");
        auto allowedOwner=std::make_unique<PackDiagnosticQueue>();auto& allowedQueue=*allowedOwner;
        WorldPackPreflightDiagnostics allowed(allowedQueue);
        check(allowed.image(1,true)&&!allowed.image(1,false),"image diagnostic preserves the existing verification result without extra hash/native work");
        for(auto value:{0x20u,0x40u,0x80u})check(allowed.protection(28,value,0x20,0x40,0x80),
            "each original executable protection value remains allowed and records the whole expected set");
        for(auto value:{0u,1u,2u,4u,8u,0x10u})check(!allowed.protection(28,value,0x20,0x40,0x80),
            "every non-executable protection in the fixture retains original rejection");
        check(allowed.region(29,true,16,48,32)&&!allowed.region(29,false,17,48,32),
            "region diagnostics preserve the original bounds boolean and report explicit byte units");
        allowed.finish(false,true);auto allowedLines=drain(allowedQueue);
        check(allowedLines[0].find("sdk=26.51.5 approved_sha256=f82dc967")!=std::string::npos
            &&allowedLines[0].find("actual=match")!=std::string::npos&&allowedLines[1].find("actual=unverified")!=std::string::npos,
            "stage1 emitted identity joins its approved fixed hash and SDK with the actual existing guard result");
        check(allowedLines[3].find("actual_protection=64 expected_allowed=32,64,128")!=std::string::npos,
            "READWRITE success never presents READ alone as its expected protection");
        check(allowedLines[11].find("need_bytes=32 actual_offset_bytes=16 region_size_bytes=48")!=std::string::npos,
            "region trace names required span, allocation-region offset and region size in bytes");
        std::vector<unsigned> order;for(unsigned id=1;id<=14;++id)order.push_back(id);
        for(unsigned base:{20u,40u,60u,80u})for(unsigned offset=0;offset<=10;++offset)order.push_back(base+offset);
        for(unsigned id=100;id<=105;++id)order.push_back(id);
        struct Result {bool ready{},lost{};unsigned resolutions{},queries{},byteCopies{},hooks{};std::vector<unsigned> calls;};
        auto pipeline=[&](unsigned fail,bool diagnostic,PackDiagnosticQueue& queue){
            Result value;WorldPackPreflightDiagnostics trace(queue);if(diagnostic)trace.begin(44,44,true,false,false);
            for(auto id:order){
                value.calls.push_back(id);bool accepted=id!=fail;
                if(id>=11&&id<=14){++value.resolutions;if(diagnostic)accepted=trace.formal(id,accepted,123,local);}
                else if(id==24||id==44||id==64||id==84){++value.queries;if(diagnostic)accepted=trace.equal(id,accepted?48:0,48);}
                else if(id==30||id==50||id==70||id==90){
                    ++value.byteCopies;std::array<unsigned char,32> bytes{};
                    if(diagnostic)accepted=trace.bytes(id,"0000000000000000000000000000000000000000000000000000000000000000",bytes,accepted,accepted?32:7);
                }else if(id>=100){++value.hooks;if(diagnostic)accepted=trace.hook(id,accepted?0:-17);}
                else if(diagnostic)accepted=trace.check(id,accepted,accepted?1:0,1);
                if(!accepted){value.lost=true;if(diagnostic)trace.finish(false,true);return value;}
            }
            value.ready=true;if(diagnostic)trace.finish(true,value.lost);return value;
        };
        std::vector<unsigned> failures{0};failures.insert(failures.end(),order.begin(),order.end());
        for(auto fail:failures){
            auto plainOwner=std::make_unique<PackDiagnosticQueue>(),tracedOwner=std::make_unique<PackDiagnosticQueue>();
            auto& plainQueue=*plainOwner;auto& tracedQueue=*tracedOwner;auto plain=pipeline(fail,false,plainQueue),observed=pipeline(fail,true,tracedQueue);
            check(plain.ready==observed.ready&&plain.lost==observed.lost&&plain.calls==observed.calls
                &&plain.resolutions==observed.resolutions&&plain.queries==observed.queries
                &&plain.byteCopies==observed.byteCopies&&plain.hooks==observed.hooks,
                "each first failure preserves original predicate/call order, short circuit, native work counts and Ready/lost disposition");
            auto lines=drain(tracedQueue);auto summary="first_fail="+std::to_string(fail)+" ";
            check(!lines.empty()&&lines.back().find(summary)!=std::string::npos,
                "startup trace identifies the first evaluated failing ID or zero on unchanged success");
            check(lines.size()<=WorldPackPreflightDiagnostics::Limit
                &&std::all_of(lines.begin(),lines.end(),[](auto const& line){return line.size()<512;}),
                "all preflight success/failure paths fit the existing bounded queue record envelope");
        }
        auto cappedOwner=std::make_unique<PackDiagnosticQueue>();auto& capped=*cappedOwner;WorldPackPreflightDiagnostics limited(capped);
        for(unsigned id=0;id<200;++id)check(limited.equal(400+id,1,1),"diagnostic budget saturation never changes successful predicates");
        check(!limited.equal(777,0,1)&&limited.failure()==777,"failure after trace saturation retains its exact first-fail ID");
        limited.finish(false,true);limited.finish(true,false);auto cappedLines=drain(capped);
        check(cappedLines.size()==WorldPackPreflightDiagnostics::Limit&&limited.count()==WorldPackPreflightDiagnostics::Limit
            &&cappedLines.back().find("first_fail=777")!=std::string::npos,
            "preflight trace caps at 96 copied records and reserves exactly one final failure summary");
        auto compareOwner=std::make_unique<PackDiagnosticQueue>();auto& compareQueue=*compareOwner;WorldPackPreflightDiagnostics compare(compareQueue);
        check(!compare.positive(4,-1)&&compare.atMostSigned(5,-1,4096),"signed DOS range observation retains original negative-offset predicates");
        check(compare.less(21,31,32)&&!compare.less(21,32,32)&&compare.atLeast(22,32,32)
            &&compare.atMost(9,32,32)&&!compare.clearBits(27,0x100,0x101),"range and protection diagnostic adapters return original boundary results");
        auto compareLines=drain(compareQueue);
        check(compareLines[0].find("actual_signed=-1 expected_signed=0")!=std::string::npos
            &&compareLines[1].find("actual_signed=-1 expected_signed=4096")!=std::string::npos,
            "negative DOS offset is emitted as signed -1 rather than an unsigned wraparound value");
        std::atomic_bool settingsAttempted{};auto settingsOwner=std::make_unique<PackDiagnosticQueue>();auto& settingsQueue=*settingsOwner;
        WorldSettingsBootstrapDiagnostics success;for(unsigned id=1;id<=8;++id)success.flag(id,true);
        success.publishFirst(settingsQueue,settingsAttempted);
        check(!settingsAttempted.load()&&drain(settingsQueue).empty(),"successful Settings bootstrap does not consume its first reason1 diagnostic opportunity");
        for(unsigned fail=1;fail<=8;++fail){
            WorldSettingsBootstrapDiagnostics settings;std::vector<unsigned> calls;
            auto flag=[&](unsigned id){calls.push_back(id);return id!=fail;};
            bool rejected=!settings.ownerThread(flag(1)?44:45,44)||!settings.flag(2,flag(2))
                ||!settings.flag(3,flag(3))||!settings.flag(4,flag(4))||!settings.flag(5,!flag(5),false)
                ||!settings.zero(6,flag(6)?0:3)||!settings.zero(7,flag(7)?0:5)||!settings.flag(8,flag(8));
            check(rejected&&settings.failure()==fail&&calls.size()==fail&&settings.evaluatedMask()==((1u<<fail)-1)
                &&settings.passedMask()==((1u<<(fail-1))-1),
                "Settings reason1 observes every actual reached predicate once and marks all later gates unevaluated");
            settings.publishFirst(settingsQueue,settingsAttempted);
        }
        auto settingsLines=drain(settingsQueue);
        check(settingsAttempted.load()&&settingsLines.size()==9&&settingsLines[0].find("first_fail=1")!=std::string::npos,
            "only the first rejected Settings reason1 emits one bounded summary plus eight gate records");
        check(settingsLines.back().find("evaluated=0")!=std::string::npos,
            "short-circuited Settings gates are explicitly unevaluated rather than fabricated false results");
    }
    {
        // The exact production Active dispatcher scopes native origin, whose
        // factory-owned callable reaches Finish and creates a grandchild task.
        auto chain=[&](WorldPackTransaction& tx,WorldNativeContext incoming,bool observing,bool throws=false){
            NativeHookLifetime lifetime;std::atomic<unsigned> callbacks{},queued{};std::mutex mutex;
            check(lifetime.prepare([]{return true;}),"Active propagation fixture retains native module");lifetime.registrationStarted();check(lifetime.open(),"Active propagation native lifetime admitted");
            auto before=tx;WorldNativeContext owned{tx.token,tx.attempt,77,tx.owner,true};owned.producer=77;
            auto ownedBefore=owned;unsigned observations{},activeOrigins{},finishOrigins{},childOrigins{},grandchildOrigins{};
            std::function<void()> child,grandchild;bool lost{};
            WorldNativeActiveInvocation invocation(incoming);
            if(observing)invocation=prepareWorldActiveInvocation(tx,incoming,[&]{
                ++observations;auto cycle=beginWorldCycle(tx,incoming,tx.owner,true,88);
                if(cycle.receipt())owned=cycle;return cycle;
            });
            bool stale=worldNativeStaleAttempt(before,incoming);
            bool untouched=!observing||!before.busy()||stale;
            try{
                runWorldActiveOrigin(invocation,[&]{
                    auto nativeOrigin=[&](std::uintptr_t manager,bool full){
                    check(manager==22&&full,"Active original preserves its exact manager and full arguments");
                    ++activeOrigins;
                    auto expected=invocation.observed.receipt()?invocation.observed:incoming;
                    check(worldNativeContext.token==expected.token&&worldNativeContext.attempt==expected.attempt
                        &&worldNativeContext.cycle==expected.cycle&&worldNativeContext.producer==expected.producer
                        &&worldNativeContext.finishProducer==expected.finishProducer&&worldNativeContext.bodyPhase==expected.bodyPhase
                        &&worldNativeContext.fullVanilla==expected.fullVanilla&&worldNativeContext.owner.world==expected.owner.world
                        &&worldNativeContext.owner.client==expected.owner.client&&worldNativeContext.owner.manager==expected.owner.manager
                        &&worldNativeContext.task==expected.task&&worldNativeContext.invocation==expected.invocation
                        &&worldNativeContext.parentInvocation==expected.parentInvocation&&worldNativeContext.finalizeInvocation==expected.finalizeInvocation,
                        "actual Active native origin receives full selected lineage with exact original manager/full arguments");
                    wrapWorldCallable<void>(worldNativeContext,lifetime,callbacks,std::function<void()>([&,expected]{
                        ++childOrigins;
                        check(worldNativeContext.token==expected.token&&worldNativeContext.attempt==expected.attempt
                            &&worldNativeContext.source().same(expected.source())&&worldNativeContext.bodyPhase==expected.bodyPhase
                            &&worldNativeContext.finalizeInvocation==expected.finalizeInvocation,
                            "Active-origin child factory task retains old source/finish producer/body phase instead of unknown lineage");
                        observeWorldFinish(mutex,tx,owned,worldNativeContext,11,22,7,true,[&]{
                            ++finishOrigins;
                            wrapWorldCallable<void>(worldNativeContext,lifetime,callbacks,std::function<void()>([&,expected]{
                                ++grandchildOrigins;
                                check(worldNativeContext.token==expected.token&&worldNativeContext.attempt==expected.attempt
                                    &&worldNativeContext.source().same(expected.source())&&worldNativeContext.bodyPhase==expected.bodyPhase,
                                    "Finish-origin grandchild factory retains the same captured transaction and source lineage");
                                if(invocation.observed.receipt())++finishOrigins;
                                else observeWorldFinish(mutex,tx,owned,worldNativeContext,11,22,7,true,[&]{++finishOrigins;});
                            }),[&](auto task){grandchild=std::move(task);return 1;},lost,TitleNativeAllocation{},&queued);
                        });
                    }),[&](auto task){child=std::move(task);return 1;},lost,TitleNativeAllocation{},&queued);
                    if(throws)throw std::runtime_error("old native origin");
                    };
                    nativeOrigin(22,true);
                });
                if(invocation.observed.receipt())tx.startResult(invocation.observed.attempt,true);
            }catch(std::runtime_error const&){
                if(invocation.observed.receipt())tx.startResult(invocation.observed.attempt,false);
                check(throws,"old Active original exception propagates to the original hook catch");
            }
            check(!worldNativeContext.seed()&&activeOrigins==1&&!lost&&child&&queued.load()==(incoming.seed()||invocation.observed.seed()?1u:0u),
                "Active original runs exactly once, restores caller TLS, and owns its child callable");
            child();check(grandchild&&childOrigins==1,"actual Active-origin child reaches Finish and creates its grandchild");
            grandchild();child={};grandchild={};
            check(grandchildOrigins==1&&finishOrigins==2&&!callbacks.load()&&!queued.load()&&!lifetime.readers(),
                "both factory-owned native bodies and Finish origins run once with balanced ownership");
            if(untouched){
                check(!observations&&!invocation.observed.receipt()&&owned.source().same(ownedBefore.source())
                    &&tx.phase==before.phase&&tx.token==before.token&&tx.attempt==before.attempt
                    &&tx.started==before.started&&tx.startReturned==before.startReturned&&tx.finished==before.finished
                    &&tx.oldGlobal==before.oldGlobal&&tx.targetGlobal==before.targetGlobal,
                    "skipped old/opless/nonbusy observation and descendant Finish cannot mutate or seal the new transaction");
            }else if(invocation.observed.receipt()){
                check(observations==1&&tx.started&&tx.startReturned&&tx.finished&&tx.phase==WorldSwitchPhase::Reloading,
                    "fresh Active origin retains ordinary native cycle/start/finish observation");
            }else check(observations==1&&tx.phase==WorldSwitchPhase::Recovery,
                "missing/future/current-conflict Active contexts retain fail-closed Recovery through factory descendants");
        };
        WorldNativeContext old{199,1,9,owner(),true};old.producer=9;old.finishProducer=12;
        old.task=13;old.invocation=14;old.parentInvocation=15;old.finalizeInvocation=16;
        old.bodyPhase=WorldNativeContext::BodyPhase::Level26;
        for(auto phase:{WorldSwitchPhase::Preparing,WorldSwitchPhase::Pausing,WorldSwitchPhase::AwaitApply,
            WorldSwitchPhase::Reloading,WorldSwitchPhase::Reacquiring}){
            WorldPackTransaction tx;tx.begin(200,owner(),10);tx.prepared({"old"},{"new"});tx.phase=phase;
            if(phase==WorldSwitchPhase::Reloading||phase==WorldSwitchPhase::Reacquiring){tx.attempt=2;tx.started=true;}
            chain(tx,old,true);
        }
        WorldPackTransaction retry;retry.begin(200,owner(),10);retry.prepared({"old"},{"new"});retry.awaiting({},11);
        check(retry.callback(owner(),12),"same-token second attempt fixture claims first attempt");retry.rollback({"old"},13);retry.awaiting({},14);
        check(retry.callback(owner(),15)&&retry.attempt==2,"same-token second attempt fixture owns attempt two");
        auto priorAttempt=old;priorAttempt.token=200;chain(retry,priorAttempt,true);
        chain(retry,old,true,true); // Old origin throw still cannot call new startResult.
        WorldPackTransaction idle;idle.begin(199,owner(),10);idle.cancel();check(idle.resetCancelledForRetry(true,true),"reset Idle fixture clears old token");
        chain(idle,old,true); // Existing op, nonbusy/token zero.
        WorldPackTransaction withoutObservation;withoutObservation.begin(200,owner(),10);chain(withoutObservation,old,false);
        // Default invocation is shared by the hook's absent-op and denied-lease branches.
        chain(withoutObservation,old,false);
        for(unsigned unexpected=0;unexpected<3;++unexpected){
            WorldPackTransaction tx;tx.begin(200,owner(),10);tx.prepared({"old"},{"new"});tx.awaiting({},11);tx.callback(owner(),12);
            WorldNativeContext context;
            if(unexpected==1){context=old;context.token=201;}
            if(unexpected==2){context=old;context.token=200;} // Same current attempt but foreign existing cycle.
            chain(tx,context,true);
        }
        WorldPackTransaction fresh;fresh.begin(200,owner(),10);fresh.prepared({"old"},{"new"});fresh.awaiting({},11);fresh.callback(owner(),12);
        WorldNativeContext seed{fresh.token,fresh.attempt,0,fresh.owner,false};chain(fresh,seed,true);
    }
    {
        WorldPackTransaction tx;auto current=owner();std::mutex mutex;unsigned origins{};
        check(tx.begin(200,current,10),"new transaction owns stale-context regression fixture");
        WorldNativeContext previous{199,1,9,current,true};previous.producer=9;
        check(worldNativeStaleAttempt(tx,previous)&&!beginWorldCycle(tx,previous,current,true,2).receipt()
            &&tx.phase==WorldSwitchPhase::Preparing&&!tx.attempt,"known old token cannot seal a fresh transaction before its callback claims an attempt");
        observeWorldFinish(mutex,tx,{},previous,11,22,7,true,[&]{++origins;});
        check(tx.phase==WorldSwitchPhase::Preparing&&!tx.attempt&&origins==1,"old finish preserves new preparation and its native origin");origins=0;
        for(auto phase:{WorldSwitchPhase::Preparing,WorldSwitchPhase::Pausing,WorldSwitchPhase::AwaitApply}){
            auto pending=tx;pending.phase=phase;auto initialOrigins=origins;
            check(worldNativeStaleAttempt(pending,previous)&&!beginWorldCycle(pending,previous,current,true,2).receipt()
                &&pending.phase==phase&&!pending.attempt,"known old token cannot seal any fresh pre-callback phase with attempt zero");
            observeWorldFinish(mutex,pending,{},previous,11,22,7,true,[&]{++origins;});
            check(pending.phase==phase&&!pending.attempt&&origins==initialOrigins+1,
                "old finish preserves Preparing/Pausing/AwaitApply while calling its native origin");
            for(bool future:{false,true}){
                WorldNativeContext unexpected;if(future){unexpected=previous;unexpected.token=201;}
                auto conflict=pending;
                check(!worldNativeStaleAttempt(conflict,unexpected)&&!beginWorldCycle(conflict,unexpected,current,true,2).receipt()
                    &&conflict.phase==WorldSwitchPhase::Recovery,"missing/future context still quarantines active pre-callback native start");
                conflict=pending;auto before=origins;
                observeWorldFinish(mutex,conflict,{},unexpected,11,22,7,true,[&]{++origins;});
                check(conflict.phase==WorldSwitchPhase::Recovery&&origins==before+1,
                    "missing/future active pre-callback finish preserves Recovery and native origin");
            }
        }
        WorldPackTransaction idle;idle.begin(199,current,10);idle.cancel();
        check(idle.resetCancelledForRetry(true,true)&&idle.phase==WorldSwitchPhase::Idle&&!idle.token,
            "verified cancelled transaction clears its old identity before same-root editing");
        auto idleOrigins=origins;
        check(!beginWorldCycle(idle,previous,current,true,2).receipt()&&idle.phase==WorldSwitchPhase::Idle&&!idle.token,
            "old context cannot seal or begin native work during reset Idle with token zero");
        observeWorldFinish(mutex,idle,{},previous,11,22,7,true,[&]{++origins;});
        check(idle.phase==WorldSwitchPhase::Idle&&!idle.token&&origins==idleOrigins+1,
            "old finish preserves reset Idle and its original native body");origins=0;
        tx.prepared({"old"},{"new"});tx.awaiting({},11);
        check(tx.callback(current,12),"fresh callback owns attempt one");
        tx.rollback({"old"},13);tx.awaiting({},14);
        check(tx.callback(current,15)&&tx.attempt==2,"retry owns the next monotonic attempt");
        WorldNativeContext seed{tx.token,tx.attempt,0,current,false};
        auto cycle=beginWorldCycle(tx,seed,current,true,1);
        check(cycle.receipt(),"fresh current cycle admitted before stale-context observations");
        for(bool oldToken:{false,true}){
            auto stale=seed;if(oldToken)--stale.token;else --stale.attempt;
            auto phase=tx.phase;auto attempt=tx.attempt;
            check(worldNativeStaleAttempt(tx,stale)&&!beginWorldCycle(tx,stale,current,true,2).receipt()
                &&tx.phase==phase&&tx.attempt==attempt&&tx.started,
                "old token or old attempt seed cannot begin or seal the fresh transaction");
            stale.cycle=9;stale.producer=9;
            observeWorldFinish(mutex,tx,cycle,stale,11,22,7,true,[&]{++origins;});
            check(tx.phase==phase&&!tx.finished,"old finish context executes its native origin without sealing or finishing the fresh attempt");
        }
        check(origins==2,"each stale native finish preserves its original body exactly once");
        tx.startResult(tx.attempt,true);
        observeWorldFinish(mutex,tx,cycle,cycle,11,22,7,true,[&]{++origins;});
        check(tx.finished&&tx.phase==WorldSwitchPhase::Reloading&&origins==3,"fresh matched native finish remains admitted after stale callbacks");
        for(bool futureToken:{false,true}){
            auto future=seed;if(futureToken)++future.token;else ++future.attempt;
            auto conflict=tx;check(!worldNativeStaleAttempt(conflict,future),"future token/attempt is never treated as known old work");
            check(!beginWorldCycle(conflict,future,current,true,2).receipt()&&conflict.phase==WorldSwitchPhase::Recovery,
                "unexpected future native seed preserves fail-closed Recovery");
            conflict=tx;future.cycle=9;future.producer=9;
            observeWorldFinish(mutex,conflict,cycle,future,11,22,7,true,[&]{++origins;});
            check(conflict.phase==WorldSwitchPhase::Recovery&&conflict.finished==tx.finished,
                "unexpected future finish context preserves fail-closed Recovery and its native origin");
        }
    }
    {
        WorldPackReloadRequest r;r.token=3;r.world=7;r.client=33;r.renderer=55;r.oldEpoch=1;r.nextEpoch=2;
        WorldPackReloadReply p;p.token=3;p.world=7;p.client=33;p.renderer=55;p.epoch=1;p.phase=WorldPackReloadPhase::Idle;
        for(auto retained:{0u,1u})for(auto gpu:{OwnedGpuFence::Unknown,OwnedGpuFence::Pending,OwnedGpuFence::NoOwnedSubmission}){
            p.oldOwnersRetained=retained;p.ownedGpu=gpu;
            check(WorldPackPeers::resumedOldReceipt(r,p),"old-epoch Idle resume preserves both participant retention/GPU reply policies");
        }
        for(unsigned corruption=1;corruption<=13;++corruption){
            Fixture f;f.prepared();resumeCorruption=corruption;f.tx.cancel();f.peers.step(f.tx,14);
            check(f.tx.phase==WorldSwitchPhase::Recovery&&!f.peers.cancelVerified&&f.peers.mask==3
                &&f.a.paused()&&f.b.paused(),"accepted ResumeCancelled with any wrong identity/epoch/Idle/open/drain field seals both retained peers");
        }
        for(bool partial:{false,true}){
            WorldPackParticipant a,b;providerA=&a;providerB=&b;resumeCorruption=0;corruptReceipt=false;
            rejectPauseA=!partial;rejectPauseB=partial;WorldPackPeers peers(peerA);peers.count=2;peers.peers[1]=peerB;
            WorldPackTransaction tx;check(tx.begin(120,owner(),10),"rejected-pause control transaction admitted");tx.prepared({"old"},{"new"});
            peers.pause(tx,120,1,11);peers.step(tx,12);
            check(tx.phase==WorldSwitchPhase::Cancelled&&!peers.mask&&!peers.cancelVerified&&!peers.pauseComplete,
                "zero-mask or resumed partial pause never proves that all configured peers returned to the old epoch");
            check(!tx.resetCancelledForRetry(peers.cancelVerified,true),"unverified rejected/partial pause never reopens the editor");
        }
        rejectPauseA=rejectPauseB=false;
    }
    {
        WorldPackTransaction tx;auto old=owner();
        check(tx.begin(80,old,1),"explicit native fixture begins");tx.prepared({"old"},{"new"});
        WorldPackReloadRequest request;request.token=81;request.world=old.world;request.client=old.client;
        request.renderer=old.renderer;request.oldEpoch=1;request.nextEpoch=2;
        tx.awaiting(request,2);check(tx.callback(old,3),"explicit native callback claimed");
        check(tx.nativeStart(old,tx.attempt,true),"native active began");tx.startResult(tx.attempt,true);tx.finish(tx.attempt,true);
        auto replacement=old;replacement.renderer=56;
        check(!tx.settleExecuted(replacement,false,true,{"new"},4)&&tx.phase==WorldSwitchPhase::Reloading,
            "finish return and selected IDs without frame receipt cannot settle");
        check(tx.settleExecuted(replacement,true,true,{"new"},5)
            &&tx.participant.renderer==56&&tx.owner.renderer==56&&tx.phase==WorldSwitchPhase::Reacquiring,
            "explicit frame receipt retargets current renderer at native settlement");
    }
    {
        WorldPackParticipant peer;WorldPackReloadRequest request;
        request.token=90;request.world=7;request.client=33;request.renderer=55;request.oldEpoch=1;request.nextEpoch=2;
        WorldPackReloadReply reply;
        check(peer.command(WorldPackReloadCommand::Pause,&request,&reply)==1,"Praxis retarget fixture pauses");
        peer.ownerServiced(true,1,true,false);peer.ownerServiced(false,1,true,false);
        check(peer.command(WorldPackReloadCommand::Prepare,&request,&reply)==1,"Praxis retarget fixture prepares");
        auto newRenderer=request;newRenderer.renderer=56;
        check(peer.command(WorldPackReloadCommand::NativeSettled,&newRenderer,&reply)==0,"Praxis pre-native retarget rejected");
        check(peer.command(WorldPackReloadCommand::BeginNative,&request,&reply)==1,"Praxis retarget fixture native active");
        auto bad=newRenderer;++bad.token;
        check(peer.command(WorldPackReloadCommand::NativeSettled,&bad,&reply)==0,"Praxis foreign token retarget rejected");
        bad=newRenderer;++bad.client;check(peer.command(WorldPackReloadCommand::NativeSettled,&bad,&reply)==0,"Praxis foreign client retarget rejected");
        bad=newRenderer;++bad.world;check(peer.command(WorldPackReloadCommand::NativeSettled,&bad,&reply)==0,"Praxis foreign world retarget rejected");
        bad=newRenderer;++bad.oldEpoch;check(peer.command(WorldPackReloadCommand::NativeSettled,&bad,&reply)==0,"Praxis foreign epoch retarget rejected");
        check(peer.command(WorldPackReloadCommand::NativeSettled,&newRenderer,&reply)==1&&reply.renderer==56,
            "Praxis drained native boundary retargets renderer");
        check(peer.command(WorldPackReloadCommand::Query,&request,&reply)==0,"Praxis old renderer Query rejected");
        peer.ownerServiced(true,2,true,true);peer.ownerServiced(false,2,true,true);
        check(peer.command(WorldPackReloadCommand::Query,&newRenderer,&reply)==1&&reply.phase==WorldPackReloadPhase::Ready,
            "Praxis replacement renderer reaches Ready only at fresh epoch");
        check(peer.command(WorldPackReloadCommand::Resume,&request,&reply)==0
            &&peer.command(WorldPackReloadCommand::Resume,&newRenderer,&reply)==1,
            "Praxis replacement renderer alone resumes");
    }
    {
        Fixture f;f.prepared();f.apply();check(f.setters==1,"one claimed setter");f.finish();f.settle({"new"});
        check(!f.a.accept(1)&&!f.b.accept(1),"old decode/publication epoch is rejected");f.service(true);f.peers.step(f.tx,23);
        check(f.tx.phase==WorldSwitchPhase::Complete&&!f.a.paused()&&!f.b.paused(),"normal world apply and both owner resumptions complete");f.clean();
    }
    {
        Fixture f;fakeGpu=OwnedGpuFence::Unknown;f.service();check(f.peers.step(f.tx,12)==WorldPeerAction::None,"unknown owned GPU cannot prepare");
        fakeGpu=OwnedGpuFence::Pending;check(f.peers.step(f.tx,12)==WorldPeerAction::None,"pending owned GPU cannot prepare");
        fakeGpu=OwnedGpuFence::Complete;check(f.peers.step(f.tx,12)==WorldPeerAction::Navigate,"real completion receipt permits prepare");f.clean();
    }
    {
        Fixture f;f.service();corruptReceipt=true;check(f.peers.step(f.tx,12)==WorldPeerAction::None,"wrong renderer receipt cannot prepare");
        corruptReceipt=false;f.tx.cancel();f.peers.step(f.tx,13);check(!f.a.paused()&&!f.b.paused()&&f.setters==0,"pre-native abort restores both admissions");f.clean();
    }
    {
        Fixture f;f.prepared();check(f.control.cancel(f.tx.token),"cancel survives dequeue and navigation");f.apply();f.peers.step(f.tx,14);
        check(f.setters==0&&f.tx.phase==WorldSwitchPhase::Cancelled&&!f.a.paused(),"cancelled callback has no setter and resumes old epoch");f.clean();
    }
    {
        Fixture f;f.prepared();std::latch validation(1),release(1);
        std::thread callback([&]{f.apply([&]{validation.count_down();release.wait();return true;});});validation.wait();
        check(f.callbacks.load()==1&&f.lifetime.readers()==1,"real callback has an in-flight lease before validation");
        check(f.control.cancel(f.tx.token),"real concurrent cancellation wins final permission");release.count_down();callback.join();f.peers.step(f.tx,14);
        check(f.setters==0&&!f.a.paused(),"concurrent cancel leaves old stack and restarts owners");f.clean();
    }
    {
        Fixture f;f.prepared();std::latch validation(1),release(1);
        std::thread callback([&]{f.apply([&]{validation.count_down();release.wait();return true;});});validation.wait();
        f.control.close();f.lifetime.close();{std::lock_guard lock(f.mutex);f.tx.seal();f.peers.stop(f.tx);}
        check(f.lifetime.readers()==1,"nonblocking stop retains active callback");release.count_down();callback.join();
        check(f.setters==0&&f.tx.phase==WorldSwitchPhase::Recovery,"late validation cannot overwrite stop Recovery");f.clean();
    }
    {
        Fixture f;f.prepared();std::latch claimed(1),release(1);
        std::thread callback([&]{runWorldApply(f.lifetime,f.callbacks,f.control,f.mutex,f.tx,f.tx.participant.token,
            []{return owner();},[]{return true;},[]{return true;},[&]{++f.setters;claimed.count_down();release.wait();return TitleSetterResult{false,true};},13);});
        claimed.wait();check(!f.control.cancel(f.tx.token),"late cancel cannot revoke an already claimed setter");
        f.control.close();f.lifetime.close();{std::lock_guard lock(f.mutex);f.tx.seal();f.peers.stop(f.tx);}
        check(f.callbacks.load()==1&&f.lifetime.readers()==1,"stop retains setter already in flight");release.count_down();callback.join();
        check(f.tx.phase==WorldSwitchPhase::Recovery,"rejected setter result cannot overwrite post-claim stop");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();auto pending=f.pending;f.apply();check(f.setters==1,"duplicate Apply does not consume another setter");
        observeWorldFinish(f.mutex,f.tx,f.native,{},11,22,7,true,[&]{++f.origins;});
        check(f.origins==1&&f.tx.phase==WorldSwitchPhase::Recovery,"uncaptured finish runs normal origin without false success");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();f.finish();
        check(f.tx.poll(owner(),true,true,true,{"partial"},20)==WorldSwitchAction::None,"partial ready observation");
        check(f.tx.poll(owner(),true,true,true,{"new"},21)==WorldSwitchAction::None,"partial then target does not finish");
        check(f.tx.poll(owner(),true,true,true,{"new"},22)==WorldSwitchAction::NativeSettled,"second exact target finishes");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();f.finish();f.settle({"partial"});f.service(true);
        check(f.peers.step(f.tx,23)==WorldPeerAction::Rollback,"mismatched stable stack requests normal rollback after provider reacquisition");
        auto oldPeer=f.tx.participant.token;f.tx.rollback({"partial"},24);f.peers.pause(f.tx,++f.nextPeerToken,2,24);f.service();
        check(f.peers.step(f.tx,25)==WorldPeerAction::Navigate,"rollback uses a new peer token and epoch");
        f.apply([]{return true;},oldPeer);check(f.setters==1&&f.tx.phase==WorldSwitchPhase::AwaitApply,"old progress callback cannot claim rollback");
        f.apply();check(f.setters==2&&f.tx.attempt==2,"normal rollback claims only the second attempt");f.origins=0;f.finish();f.settle({"old"});f.service(true);f.peers.step(f.tx,30);
        check(f.tx.phase==WorldSwitchPhase::Restored&&f.a.resourceEpoch()==3&&!f.a.paused(),"rollback restores stack and new resource epoch through normal native cycle");f.clean();
    }
    {
        Fixture f;f.tx.targetGlobal=f.tx.oldGlobal;f.prepared();f.apply();f.finish();f.settle({"old"});f.service(true);f.peers.step(f.tx,23);
        check(f.setters==1&&f.tx.phase==WorldSwitchPhase::Complete,"same identity reload still traverses native cycle and owner rebuild");f.clean();
    }
    {
        Fixture f(1);f.service();check(f.heldBorrow->admitted()&&f.peers.step(f.tx,12)==WorldPeerAction::None,"borrowers prevent prepare");
        check(!f.heldBorrow->current(),"pause seals a reader publication even before its exit");f.tx.cancel();f.peers.step(f.tx,13);check(f.a.paused(),"pre-native abort waits for the real borrower");
        f.heldBorrow.reset();f.peers.step(f.tx,14);check(!f.a.paused(),"abort resumes only after the real borrower exits");f.clean();
    }
    {
        Fixture f(2);f.service();check(f.heldJob->admitted(),"owned copied async job counted");
        check(f.peers.step(f.tx,12)==WorldPeerAction::None,"active async job prevents prepare");f.heldJob.reset();
        // A paused gate refuses new jobs, and a request with old token cannot mutate it.
        WorldPackParticipant::AsyncLease late(f.a,1);check(!late.admitted(),"paused gate rejects late async work");
        auto wrong=f.tx.participant;++wrong.token;WorldPackReloadReply reply;check(!f.a.command(WorldPackReloadCommand::BeginNative,&wrong,&reply),"wrong peer token cannot advance native phase");f.clean();
    }
    {
        Fixture f;f.prepared();auto changed=owner();++changed.renderer;f.tx.poll(changed,true,true,true,{"old"},14);f.peers.step(f.tx,14);
        check(f.tx.phase==WorldSwitchPhase::Recovery&&f.a.state()==WorldPackReloadPhase::Invalidated,"renderer change seals and retains owners");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();f.finish();
        f.tx.poll(owner(),true,true,true,{"new"},20);f.tx.poll(owner(),true,true,true,{"new"},21);
        auto foreign=owner();++foreign.thread;
        check(!requestWorldGeometryRebuild(f.tx,foreign,true,[&]{++f.rebuilds;})&&f.rebuilds==0&&f.tx.phase==WorldSwitchPhase::Recovery,"foreign render owner cannot rebuild");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();f.finish();
        f.tx.poll(owner(),true,true,true,{"new"},20);f.tx.poll(owner(),true,true,true,{"new"},21);
        check(!requestWorldGeometryRebuild(f.tx,owner(),true,[]{throw std::runtime_error("native rebuild failure");})&&f.tx.phase==WorldSwitchPhase::Recovery,"owner rebuild exception retains recovery boundary");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();auto wrong=f.native;++wrong.cycle;
        observeWorldFinish(f.mutex,f.tx,f.native,wrong,11,22,7,true,[&]{++f.origins;});
        check(f.origins==1&&f.tx.phase==WorldSwitchPhase::Recovery,"wrong real cycle cannot complete a world operation");f.clean();
    }
    {
        Fixture f;f.prepared();f.apply();f.finish();check(f.tx.poll(owner(),true,true,true,{"new"},20)==WorldSwitchAction::None,"first ready target");
        auto loading=owner();loading.ready=false;f.tx.poll(loading,true,true,true,{"new"},21);
        check(f.tx.poll(owner(),true,true,true,{"new"},22)==WorldSwitchAction::None,"readiness gap resets consecutive identity");f.clean();
    }
    {
        WorldPackReloadRequest r;r.token=1;r.world=7;r.client=33;r.renderer=55;r.oldEpoch=1;r.nextEpoch=2;
        WorldPackReloadReply reply;check(praxis_resource_reload_v4(WorldPackReloadCommand::Pause,&r,&reply)==1,"actual compiled Praxis export accepts ABI v4");
        auto& p=praxisWorldParticipant();p.ownerServiced(true,1,true,false);p.ownerServiced(false,1,true,false);
        check(praxis_resource_reload_v4(WorldPackReloadCommand::Prepare,&r,&reply)==1,"actual export prepares serviced owners");
        check(praxis_resource_reload_v4(WorldPackReloadCommand::BeginNative,&r,&reply)==1,"actual export begins captured native cycle");
        check(praxis_resource_reload_v4(WorldPackReloadCommand::NativeSettled,&r,&reply)==1,"actual export requests normal owner rebuild");
        p.ownerServiced(true,2,true,true);p.ownerServiced(false,2,true,true);
        check(praxis_resource_reload_v4(WorldPackReloadCommand::Query,&r,&reply)==1&&participantReceipt(r,reply,WorldPackReloadPhase::Ready,2),"actual export returns exact ready receipt");
        check(praxis_resource_reload_v4(WorldPackReloadCommand::Resume,&r,&reply)==1&&!p.paused(),"actual export resumes at new epoch");
        r.abi=99;reply={};check(!praxis_resource_reload_v4(WorldPackReloadCommand::Pause,&r,&reply),"actual export rejects incompatible ABI");
    }
    {
        auto beforeA=peerCallsA,beforeB=peerCallsB;Fixture f;
        check(peerCallsA==beforeA+1&&peerCallsB==beforeB+1,"Pause diagnostics issue exactly the original one call per participant");
        auto const& p=f.peers.observations[0];
        check(p.observed&&p.accepted&&p.command==WorldPackReloadCommand::Pause&&p.epoch==1&&p.phase==WorldPackReloadPhase::Pausing,"copied peer diagnostic matches real Pause receipt");
        f.prepared();beforeA=peerCallsA;beforeB=peerCallsB;
        check(f.peers.ready(f.tx,WorldPackReloadPhase::Prepared,1),"original strict receipt check still controls readiness");
        check(peerCallsA==beforeA+1&&peerCallsB==beforeB+1,"readiness diagnostics add no extra Query calls");
        check(f.peers.observations[0].receiptMatched&&f.peers.observations[1].receiptMatched,"copied diagnostics distinguish exact receipt from accepted call");
        corruptReceipt=true;
        check(!f.peers.ready(f.tx,WorldPackReloadPhase::Prepared,1)&&f.peers.observations[1].accepted&&!f.peers.observations[1].receiptMatched,"successful diagnostic command is not permission to accept wrong native receipt");
        corruptReceipt=false;f.clean();
    }
    std::printf("World pack joined provider/coordinator/native adapter: %u checks PASS; Minecraft NOT_RUN\n",checks);
}
