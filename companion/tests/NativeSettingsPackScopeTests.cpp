#include "resourcepacks/NativeSettingsPackScope.h"
#include "resourcepacks/WorldPackNativeAdapter.h"
#include "resourcepacks/WorldPackPeers.h"
#include "resourcepacks/WorldPackParticipant.h"
#include "map/MapResourceService.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <map>
#include <stdexcept>
#include <thread>
using namespace praxis::companion::packs;
namespace map=praxis::companion::map;
namespace {
unsigned checks{};
void check(bool ok,char const* text){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
WorldPackParticipant* participant{};
int peer(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept{return participant->command(c,r,p);}
WorldPackOwner owner(){return {11,22,33,44,7,99,true,true,false};}
void service(WorldPackParticipant& p,bool ready=false){auto e=p.resourceEpoch();p.ownerServiced(true,e,true,ready);p.ownerServiced(false,e,true,ready);}
struct ModelManager {};
struct ModelGame;
struct ModelClient {
    ModelGame* game{};ModelManager* manager{};
    ModelGame& getMinecraftGame_DEPRECATED() const{return *game;}
    ModelManager& getResourcePackManager() const{return *manager;}
};
struct ModelGame {
    std::map<int,std::shared_ptr<ModelClient>> clients;
    auto getClientInstanceMap(){return clients;}
};
struct ModelInterface {
    int id{};ModelManager* manager{};
    // Deliberately contradictory generated-field view. Official accessors
    // remain authoritative when a secondary native base changes the layout.
    ModelClient* mClient{};ModelGame* mMinecraft{};
    int getSubClientId() const{return id;}
    ModelManager& getResourcePackManager() const{return *manager;}
};
}
int main(){
    {
        // Native _init calculates Root flags before invoking General's ctor.
        // Exercise the shared production policies with the actual participant
        // transaction, including a child that r7 already made editable.
        using Reason=NativeSettingsRootExitReason;using Route=NativeSettingsExitRoute;
        NativeSettingsRootReloadView initialized{true,true,true,true,true,true,true,true,true};
        auto validExit=[] {NativeSettingsRootExitView v;
            v.exitEvent=v.ownerThread=v.rootReload=v.generalPresent=v.canExit=v.importsIdle=v.eventAlive=true;
            v.modelPresent=v.sameGeneral=v.sourcePresent=v.childGlobal=v.childReload=v.settingsMode=true;
            v.ownerCurrent=v.revisionValid=v.sameScene=v.globalUnchanged=v.otherStacksUnchanged=true;return v;};
        bool rootGlobal=false,rootReload=false;
        auto child=nativeSettingsConstructorFlags(false,false,true);
        auto v=validExit();v.childGlobal=child.global;v.childReload=child.reloadOnExit;v.rootReload=rootReload;
        check(child.global&&child.reloadOnExit&&nativeSettingsRootExit(v).reason==Reason::RootReloadDisabled,
            "editable General with disabled Root reload directly classifies the r7 source mismatch");
        rootReload=nativeSettingsRootReload(rootReload,initialized);v.rootReload=rootReload;
        check(rootReload&&!rootGlobal&&nativeSettingsRootExit(v).route==Route::Inspect,
            "qualified normal Root initialization enables reload without changing native world-return/global navigation");
        for(auto field:{&NativeSettingsRootReloadView::initializationOwned,&NativeSettingsRootReloadView::sameModel,
            &NativeSettingsRootReloadView::sameGeneral,&NativeSettingsRootReloadView::ownerCurrent,
            &NativeSettingsRootReloadView::settingsMode,&NativeSettingsRootReloadView::globalPolicy,
            &NativeSettingsRootReloadView::childGlobal,&NativeSettingsRootReloadView::childReload,
            &NativeSettingsRootReloadView::sourcePresent}){
            auto denied=initialized;denied.*field=false;
            check(!nativeSettingsRootReload(false,denied)&&nativeSettingsRootReload(true,denied),
                "every init/model/General/owner/world/thread/mode/policy/source gate denies compensation and preserves native true");
        }
        unsigned nativeExit{},nativeApply{};WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        auto borrowed=std::make_unique<WorldPackParticipant::Lease>(p,true);
        check(nativeSettingsRootExit(v).reason==Reason::Hold&&tx.begin(201,owner(),10),"normal changed Root exit reserves the transaction before native save");
        tx.prepared({"old-a","old-b"},{"old-b"});peers.pause(tx,201,1,11);service(p);
        check(nativeExit==0&&nativeApply==0&&p.paused()&&peers.step(tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::None,
            "deactivation waits for actual old borrowers without dispatching save or apply");
        auto duplicate=v;duplicate.pending=true;duplicate.canExit=false;
        check(nativeSettingsRootExit(duplicate).reason==Reason::Pending&&nativeSettingsRootExit(duplicate).route==Route::Consume,
            "duplicate Back is consumed before the independent native reload branch even when canExit becomes false");
        borrowed.reset();check(peers.step(tx,13,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate,"old borrower drain permits normal Root event replay");
        ++nativeExit;tx.queueCallback(14);check(tx.callback(owner(),14),"replayed normal Root exit owns the standard native apply callback");
        auto cycle=beginWorldCycle(tx,WorldNativeContext{tx.token,tx.attempt,0,tx.owner,false},owner(),true,1);
        check(cycle.receipt()&&peers.transition(tx,WorldPackReloadCommand::BeginNative),"Root exit keeps the actual native/peer receipt checks");
        ++nativeApply;tx.startResult(cycle.attempt,true);tx.finish(cycle.attempt,true);
        tx.poll(owner(),true,true,true,{"old-b"},15);tx.poll(owner(),true,true,true,{"old-b"},16);
        peers.step(tx,17,worldPeerStepPolicy(true,true));service(p,true);peers.step(tx,18,worldPeerStepPolicy(true,true));
        check(nativeExit==1&&nativeApply==1&&tx.phase==WorldSwitchPhase::Complete&&!p.paused()&&p.resourceEpoch()==2,
            "normal changed Root exit reaches native apply once and resumes only reacquired owners");
        WorldPackTransaction unchanged;auto noChange=validExit();noChange.selectionUnchanged=true;
        auto oldEpoch=p.resourceEpoch();
        check(nativeSettingsRootExit(noChange).reason==Reason::NoChange&&nativeSettingsRootExit(noChange).route==Route::OriginNormal
            &&unchanged.phase==WorldSwitchPhase::Idle&&!unchanged.token&&!p.paused()&&p.resourceEpoch()==oldEpoch,
            "unchanged Root exit stays native without pause, an apply transaction or an epoch advance");
        for(auto [field,reason]:{
            std::pair{&NativeSettingsRootExitView::ownerThread,Reason::WrongThread},
            std::pair{&NativeSettingsRootExitView::generalPresent,Reason::GeneralMissing},
            std::pair{&NativeSettingsRootExitView::canExit,Reason::CanExitDenied},
            std::pair{&NativeSettingsRootExitView::importsIdle,Reason::ImportsPending},
            std::pair{&NativeSettingsRootExitView::eventAlive,Reason::EventExpired},
            std::pair{&NativeSettingsRootExitView::modelPresent,Reason::ModelMissing},
            std::pair{&NativeSettingsRootExitView::sameGeneral,Reason::GeneralMismatch},
            std::pair{&NativeSettingsRootExitView::sourcePresent,Reason::SourceMissing},
            std::pair{&NativeSettingsRootExitView::childGlobal,Reason::ChildGlobalDisabled},
            std::pair{&NativeSettingsRootExitView::childReload,Reason::ChildReloadDisabled},
            std::pair{&NativeSettingsRootExitView::settingsMode,Reason::SettingsMode},
            std::pair{&NativeSettingsRootExitView::ownerCurrent,Reason::OwnerChanged},
            std::pair{&NativeSettingsRootExitView::revisionValid,Reason::RevisionInvalid},
            std::pair{&NativeSettingsRootExitView::sameScene,Reason::SceneChanged},
            std::pair{&NativeSettingsRootExitView::globalUnchanged,Reason::GlobalChanged},
            std::pair{&NativeSettingsRootExitView::otherStacksUnchanged,Reason::OtherStacksChanged}}){
            auto denied=validExit();denied.*field=false;auto decision=nativeSettingsRootExit(denied);
            check(decision.reason==reason&&decision.route==Route::Consume,"each Root apply rejection is classifiable and cannot leak into unpaused native save");
        }
        auto otherEvent=validExit();otherEvent.exitEvent=false;otherEvent.pending=true;
        check(nativeSettingsRootExit(otherEvent).route==Route::OriginNormal,"other Settings controls retain the normal event route");
        WorldPackParticipant cancelled;participant=&cancelled;WorldPackPeers cancelledPeers(peer);WorldPackTransaction retry;
        check(retry.begin(202,owner(),20),"Root cancellation starts from a current owner");retry.prepared({"old"},{"new"});
        cancelledPeers.pause(retry,202,1,21);service(cancelled);retry.cancel();cancelledPeers.step(retry,22,worldPeerStepPolicy(true,true));
        NativeSettingsCancelledRetryView current{true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
        current.peersResumed=cancelledPeers.cancelVerified;current.peersOpen=!cancelledPeers.mask;
        check(nativeSettingsCancelledRetry(retry,current)&&nativeSettingsRootExit(validExit()).route==Route::Inspect,
            "same Root can retry after verified pre-native cancellation with native root-global remaining false");
        check(retry.begin(203,owner(),23),"cancelled Root retry receives a new transaction token");retry.prepared({"old"},{"changed"});
        cancelledPeers.pause(retry,203,1,24);service(cancelled);cancelledPeers.step(retry,25,worldPeerStepPolicy(true,true));
        auto departed=owner();++departed.world;retry.poll(departed,true,false,true,{"old"},26);cancelledPeers.step(retry,27,worldPeerStepPolicy(true,true));
        auto stale=validExit();stale.ownerCurrent=false;
        check(retry.phase==WorldSwitchPhase::Recovery&&cancelled.paused()&&!retry.callback(departed,28)
            &&nativeSettingsRootExit(stale).reason==Reason::OwnerChanged,
            "world/owner teardown seals paused resources and classifies stale Root exit without native apply");
        auto reentry=initialized;reentry.ownerCurrent=false;
        check(!nativeSettingsRootReload(false,reentry),"reused Root/child addresses from a previous world cannot enable reload on reentry");
    }
    {
        using Route=NativeSettingsExitRoute;
        check(nativeSettingsExitRoute(true,false,false,true,false)==Route::Consume,
            "pending owned exit consumes a duplicate even when native canExit is false");
        check(nativeSettingsExitRoute(true,false,false,false,false)==Route::OriginNormal,
            "ordinary ineligible exit preserves the native dispatcher");
        check(nativeSettingsExitRoute(false,false,false,true,false)==Route::OriginNormal,
            "another controller's exit remains native even while this root is paused");
        check(nativeSettingsExitRoute(true,true,false,true,false)==Route::Consume,
            "replay permit does not bypass an invalid frame owner");
        check(nativeSettingsExitRoute(true,true,true,true,false)==Route::OriginReplay
            &&nativeSettingsExitRoute(true,false,true,true,false)==Route::Consume,
            "verified one-use replay passes origin once; the next duplicate is consumed");
    }
    {
        NativeSettingsApplyOnce once;once.token=41;once.world=7;
        check(!once.claim(40,7)&&!once.claim(41,8),"another transaction or world cannot claim the saved native action");
        std::atomic_uint invoked{};
        std::thread first([&]{if(once.claim(41,7))++invoked;});
        std::thread second([&]{if(once.claim(41,7))++invoked;});
        first.join();second.join();
        check(invoked==1&&!once.claim(41,7),"duplicate callback dispatch commits the original action at most once");
        NativeSettingsApplyOnce cancelled;cancelled.token=42;cancelled.world=8;
        check(!cancelled.claim(42,9)&&!cancelled.claim(43,8),"stale unexecuted callback cannot cross world generation or transaction");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(13,owner(),10),"paused-simulation Settings owner admitted");tx.prepared({"old"},{"new"});peers.pause(tx,13,1,11);
        auto oldReader=std::make_unique<WorldPackParticipant::Lease>(p,true);
        // Pause correctly denies fresh readers; hold the actual old async job
        // in the separate case below. Preview's existing UI owner is serviced.
        check(!oldReader->admitted(),"UI service never reopens closed borrower admission");oldReader.reset();
        p.ownerServiced(true,1,true,false);
        check(peers.step(tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::None&&tx.phase==WorldSwitchPhase::Pausing,"without player ticks the original Map owner remains unserviced despite zero CPU readers");
        unsigned retired{},regionQueries{};auto cpu=std::make_shared<int>(1);auto retainedCpu=cpu;
        auto ui=[&](WorldPackOwner value,bool region=true){map::serviceMapResourceOwner(p,value.world,value.client,value.renderer,
            [&]{++retired;cpu.reset();},[&]{++regionQueries;return region;});};
        for(auto field:{&WorldPackOwner::client,&WorldPackOwner::renderer}){auto wrong=owner();++(wrong.*field);ui(wrong);}
        auto wrong=owner();++wrong.world;ui(wrong);
        check(retired==0&&regionQueries==0&&tx.phase==WorldSwitchPhase::Pausing,"other client/world/renderer UI callbacks cannot service Map ownership");
        ui(owner());
        check(retired==1&&!cpu&&retainedCpu&&regionQueries==0,"matching UI withdraws only CPU publication and retains existing reader storage; preparation does not query world region");
        check(peers.step(tx,13,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate&&tx.phase==WorldSwitchPhase::AwaitApply,"actual UI owner service reaches Prepared without any LocalPlayer tick");
        check(!p.accept(1)&&p.paused(),"Prepared selection keeps old epoch closed");
        NativeSettingsPackContext c{55,33,22,7,true};NativeSettingsEditorView v{33,7,true,true,true,true,true,true,true,true};
        check(nativeSettingsEditorPreGame(false,c,v),"Prepared same-world editor can compensate the native binding query");
        tx.queueCallback(14);check(tx.callback(owner(),14),"UI-serviced selection admits the matching original callback");
        WorldNativeContext seed{tx.token,tx.attempt,0,tx.owner,false};auto cycle=beginWorldCycle(tx,seed,owner(),true,1);
        check(cycle.receipt()&&peers.transition(tx,WorldPackReloadCommand::BeginNative),"UI-serviced path keeps original native and ABI4 receipt");
        tx.startResult(cycle.attempt,true);tx.finish(cycle.attempt,true);tx.poll(owner(),true,true,true,{"new"},15);tx.poll(owner(),true,true,true,{"new"},16);
        peers.step(tx,17,worldPeerStepPolicy(true,true));p.ownerServiced(true,2,true,true);ui(owner(),false);
        check(regionQueries==1&&p.state()==WorldPackReloadPhase::Reacquiring&&p.paused(),"missing current region does not grant new-epoch Map readiness");
        check(peers.step(tx,18,worldPeerStepPolicy(true,true))==WorldPeerAction::None&&tx.phase==WorldSwitchPhase::Reacquiring,"native settlement alone never resumes an unready Map owner");
        ui(owner());peers.step(tx,19,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Complete&&!p.paused()&&p.resourceEpoch()==2,"same UI owner can reacquire and resume after native settlement without player ticks");
        auto count=retired;ui(owner());check(retired==count,"idle Map UI callback has no resource service side effect");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        auto borrow=std::make_unique<WorldPackParticipant::Lease>(p,true);
        auto job=std::make_unique<WorldPackParticipant::AsyncLease>(p,1);
        check(borrow->admitted()&&job->admitted()&&tx.begin(14,owner(),10),"active old reader/job drain control admitted");tx.prepared({"old"},{"new"});peers.pause(tx,14,1,11);
        p.ownerServiced(true,1,true,false);unsigned retire{};
        map::serviceMapResourceOwner(p,7,33,44,[&]{++retire;},[]{return true;});
        check(retire==1&&peers.step(tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::None&&tx.phase==WorldSwitchPhase::Pausing,"UI CPU service preserves old borrower/job drain requirements");
        borrow.reset();check(peers.step(tx,13,worldPeerStepPolicy(true,true))==WorldPeerAction::None,"remaining async job still prevents Prepared");
        job.reset();check(peers.step(tx,14,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate,"last old job retirement permits Prepared with unchanged receipt checks");
        tx.cancel();peers.step(tx,15,worldPeerStepPolicy(true,true));check(!p.paused()&&p.resourceEpoch()==1,"cancel before native resumes the retained original epoch");
    }
    {
        ModelManager manager,otherManager;ModelGame game,otherGame;
        auto client=std::make_shared<ModelClient>(ModelClient{&game,&manager});
        game.clients[1]=client;auto observed=reinterpret_cast<std::uintptr_t>(client.get());
        ModelInterface model{1,&manager,nullptr,&otherGame};
        check(model.mClient==nullptr&&model.mMinecraft!=&game,"old direct-field owner check rejects the contradictory SDK view");
        auto held=nativeSettingsClient(model,game,observed);
        check(held==client,"formal model id and manager resolve the actual Game-owned client despite contradictory fields");
        check(!nativeSettingsClient(model,game,0),"absent update owner remains denied");
        check(!nativeSettingsClient(model,game,observed+1),"other observed client remains denied");
        auto wrong=model;wrong.id=2;check(!nativeSettingsClient(wrong,game,observed),"model from another subclient cannot inherit current owner");
        wrong=model;wrong.manager=&otherManager;check(!nativeSettingsClient(wrong,game,observed),"model from another Game manager remains denied");
        client->game=&otherGame;check(!nativeSettingsClient(model,game,observed),"client must still belong to the observed Game");client->game=&game;
        client->manager=&otherManager;check(!nativeSettingsClient(model,game,observed),"model/client manager identity remains binding");client->manager=&manager;
        game.clients[2]=std::make_shared<ModelClient>(ModelClient{&game,&manager});
        check(!nativeSettingsClient(model,game,observed),"split-screen owner mapping stays unsupported");game.clients.erase(2);
        game.clients[1].reset();check(!nativeSettingsClient(model,game,observed),"null map owner remains denied");
        game.clients.clear();check(!nativeSettingsClient(model,game,observed),"empty client map remains denied");
        std::weak_ptr<ModelClient> weak=client;client.reset();
        check(!weak.expired()&&held.get()==reinterpret_cast<ModelClient*>(observed),"resolved shared client remains alive through Game-map removal");
        held.reset();check(weak.expired(),"owner lifetime ends after the final reader releases its shared client");
    }
    for(bool global:{false,true})for(bool reload:{false,true}){
        auto original=nativeSettingsConstructorFlags(global,reload,false);
        check(original.global==global&&original.reloadOnExit==reload,"title/unsupported Settings preserve both native flags");
        auto scoped=nativeSettingsConstructorFlags(global,reload,true);
        check(scoped.global&&scoped.reloadOnExit,"verified world Settings use native Global content and reload on exit");
    }
    NativeSettingsPackContext context{55,33,22,7,true};
    check(!nativeSettingsPreGame(false,33,7),"native world result preserved outside setter");
    check(nativeSettingsPreGame(true,33,7),"native true result preserved");
    {
        NativeSettingsPackScope scope(context);
        check(nativeSettingsPreGame(false,33,7),"matching setter client admitted");
        check(!nativeSettingsPreGame(false,34,7),"other client not changed");
        check(!nativeSettingsPreGame(false,33,8),"other world not changed");
        bool otherThread{};std::thread t([&]{otherThread=nativeSettingsPreGame(false,33,7);});t.join();
        check(!otherThread,"setter scope is thread local");
        {NativeSettingsPackScope nested({});check(!nativeSettingsPreGame(false,33,7),"unrelated nested callback masks scope");}
        check(nativeSettingsPreGame(false,33,7),"nested scope restores owner");
        bool flag=false;unsigned origins{};
        int result=withNativeSettingsGlobal(flag,22,7,[&]{++origins;check(flag,"native compose sees formal Global flag");return 17;});
        check(result==17&&origins==1&&!flag,"compose result/origin count/old flag preserved");
        withNativeSettingsGlobal(flag,23,7,[&]{check(!flag,"other manager policy preserved");});
        withNativeSettingsGlobal(flag,22,8,[&]{check(!flag,"other generation policy preserved");});
        try{withNativeSettingsGlobal(flag,22,7,[&]{throw std::runtime_error("origin");});}catch(std::runtime_error const&){}
        check(!flag,"formal flag restored on native exception");
        flag=true;withNativeSettingsGlobal(flag,22,7,[&]{check(flag,"original Global true retained");});check(flag,"original true restored");
    }
    check(!nativeSettingsPackContext.valid(),"setter exit clears current scope");
    check(nativeSettingsMayIncludeGlobal(false,false,true),"ordinary local world without Level resources can include Global");
    check(!nativeSettingsMayIncludeGlobal(false,true,true),"multiplayer native false policy preserved");
    check(!nativeSettingsMayIncludeGlobal(false,false,false),"world resource native false policy preserved");
    check(nativeSettingsMayIncludeGlobal(true,true,false),"native allowed Global policy preserved with server resources");
    {
        NativeSettingsEditorView editor{33,7,true,true,true,true,true,true,true,true};
        check(!nativeSettingsPreGame(false,33,7),"edit bindings remain outside exit setter TLS");
        check(nativeSettingsEditorPreGame(false,context,editor),"matching live Global tab edits without a Prepared transaction");
        {
            NativeSettingsEditorPause deniedSetter;
            check(!nativeSettingsEditorPreGame(false,context,editor),"denied setter cannot borrow an editor permission instead of Prepared peer admission");
            check(nativeSettingsEditorPreGame(true,context,editor),"suppression preserves original native true");
            {NativeSettingsEditorPause nested;check(!nativeSettingsEditorPreGame(false,context,editor),"nested setter suppression remains closed");}
            check(!nativeSettingsEditorPreGame(false,context,editor),"nested suppression restores the outer gate");
            {NativeSettingsPackScope authorized(context);check(nativeSettingsPreGame(false,33,7),"explicitly authorized original setter retains its independent scoped permission");}
        }
        check(nativeSettingsEditorPreGame(false,context,editor),"setter exit restores ordinary live edit binding");
        auto other=editor;other.client=34;check(!nativeSettingsEditorPreGame(false,context,other),"other client edit binding remains native false");
        other=editor;other.world=8;check(!nativeSettingsEditorPreGame(false,context,other),"world change immediately rejects old editor");
        for(auto field:{&NativeSettingsEditorView::ownerThread,&NativeSettingsEditorView::sessionOpen,
            &NativeSettingsEditorView::rootAlive,&NativeSettingsEditorView::sameController,&NativeSettingsEditorView::sameTopScene,
            &NativeSettingsEditorView::globalTab,&NativeSettingsEditorView::settingsScreen,&NativeSettingsEditorView::ownerCurrent}){
            other=editor;other.*field=false;check(!nativeSettingsEditorPreGame(false,context,other),"each editor lifetime/owner/tab/scene gate stays binding");
            check(nativeSettingsEditorPreGame(true,context,other),"original native true is preserved for every denied editor gate");
        }
        auto denied=context;denied.includeGlobal=false;check(!nativeSettingsEditorPreGame(false,denied,editor),"editor never bypasses world/server Global policy");
        auto nativeRoot=std::make_shared<int>(24);std::weak_ptr<int> weak=nativeRoot;
        {auto held=weak.lock();nativeRoot.reset();editor.rootAlive=bool(held);
            check(nativeSettingsEditorPreGame(false,context,editor),"current reader holds actual shared root through native owner release");}
        editor.rootAlive=bool(weak.lock());check(!nativeSettingsEditorPreGame(false,context,editor),"expired controller never grants editing or reads a stale root");
        editor.rootAlive=true;editor.sessionOpen=false;check(!nativeSettingsEditorPreGame(false,context,editor),"queued apply/closing Settings ends persistent edit grant");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(1,owner(),10),"native Settings owns world context");tx.prepared({"old"},{"new"});
        auto borrow=std::make_unique<WorldPackParticipant::Lease>(p,true);peers.pause(tx,1,1,11);service(p);
        check(peers.step(tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::None&&tx.phase==WorldSwitchPhase::Pausing,"Settings exit waits for actual old borrowers");
        borrow.reset();check(peers.step(tx,13,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate,"same participants prepare before native exit callback");
        tx.queueCallback(14);check(tx.callback(owner(),14),"native callback enters matching owner");
        WorldNativeContext seed{tx.token,tx.attempt,0,tx.owner,false};auto cycle=beginWorldCycle(tx,seed,owner(),true,1);
        check(cycle.receipt()&&peers.transition(tx,WorldPackReloadCommand::BeginNative),"native original start preserves ABI4 transition");
        tx.startResult(cycle.attempt,true);tx.finish(cycle.attempt,true);
        check(tx.poll(owner(),false,true,true,{"new"},15)==WorldSwitchAction::None,"inflight original calls prevent reacquire");
        check(tx.poll(owner(),true,true,true,{"new"},16)==WorldSwitchAction::None,"first owner observation remains incomplete");
        check(tx.poll(owner(),true,true,true,{"new"},17)==WorldSwitchAction::NativeSettled,"native CPU settlement reaches existing participant boundary");
        check(!tx.geometryRequested,"native route never creates mod terrain dirty request");
        peers.step(tx,18,worldPeerStepPolicy(false,false));check(p.state()==WorldPackReloadPhase::NativeActive,"legacy route still requires its dirty request");
        peers.step(tx,19,worldPeerStepPolicy(true,true));check(p.state()==WorldPackReloadPhase::Reacquiring&&p.paused(),"native-owned reload advances while borrowers remain closed");
        peers.step(tx,20,worldPeerStepPolicy(true,true));check(p.paused(),"native geometry ownership does not bypass owner reacquisition");
        service(p,true);peers.step(tx,21,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Complete&&!p.paused()&&p.resourceEpoch()==2&&!peers.mask,"native route resumes only matched current owner epoch");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.phase==WorldSwitchPhase::Idle&&!tx.token&&!peers.mask&&!p.paused(),
            "opening unchanged Settings has no transaction or pause to cancel");
        check(tx.phase==WorldSwitchPhase::Idle&&!tx.token&&!peers.mask&&!p.paused()&&p.resourceEpoch()==1,
            "closing unchanged Settings leaves the old epoch admitted without a transaction");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(3,owner(),10),"long selection owner admitted");tx.prepared({"old"},{"new"});peers.pause(tx,3,1,11);service(p);
        check(peers.step(tx,12,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate&&tx.awaitingSelection,"prepared native Settings enters human selection wait");
        for(auto now:{std::uint64_t{30013},std::uint64_t{180013},std::uint64_t{3600013}}){
            tx.poll(owner(),true,false,true,{"old"},now);peers.step(tx,now,worldPeerStepPolicy(true,true));
            check(tx.phase==WorldSwitchPhase::AwaitApply&&tx.awaitingSelection&&tx.deadline==0,"human selection survives processing budgets and one hour");
            check(p.paused()&&p.resourceEpoch()==1&&peers.ready(tx,WorldPackReloadPhase::Prepared,1),"long selection retains actual Prepared owners and old epoch");
        }
        tx.queueCallback(3600014);
        check(!tx.awaitingSelection&&tx.deadline==3600014+WorldPackTransaction::ReloadBudget,"exit starts fresh bounded callback wait");
        tx.poll(owner(),true,false,true,{"old"},3630015);
        check(tx.phase==WorldSwitchPhase::AwaitApply,"queued callback is allowed beyond old preparation budget");
        check(tx.callback(owner(),3630016),"exit after long selection admits matching native callback");
        WorldNativeContext seed{tx.token,tx.attempt,0,tx.owner,false};auto cycle=beginWorldCycle(tx,seed,owner(),true,1);
        check(cycle.receipt()&&peers.transition(tx,WorldPackReloadCommand::BeginNative),"long selection exit keeps ABI4 BeginNative receipt");
        tx.startResult(cycle.attempt,true);tx.finish(cycle.attempt,true);
        tx.poll(owner(),true,true,true,{"new"},3630017);
        check(tx.poll(owner(),true,true,true,{"new"},3630018)==WorldSwitchAction::NativeSettled,"long selection exit reaches native CPU settlement");
        peers.step(tx,3630019,worldPeerStepPolicy(true,true));peers.step(tx,3630020,worldPeerStepPolicy(true,true));service(p,true);peers.step(tx,3630021,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Complete&&!p.paused()&&p.resourceEpoch()==2&&!peers.mask,"long selection exit resumes only reacquired new owners");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(4,owner(),10),"long selection cancel owner admitted");tx.prepared({"old"},{"old"});peers.pause(tx,4,1,11);service(p);peers.step(tx,12,worldPeerStepPolicy(true,true));
        tx.poll(owner(),true,false,true,{"old"},3600013);tx.cancel();peers.step(tx,3600014,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Cancelled&&!p.paused()&&p.resourceEpoch()==1&&!peers.mask,"cancel/no-change exit after one hour resumes retained old epoch");
        check(!tx.callback(owner(),3600015)&&!tx.started,"cancelled selection cannot start a later native callback");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(5,owner(),10),"long selection world-change owner admitted");tx.prepared({"old"},{"new"});peers.pause(tx,5,1,11);service(p);peers.step(tx,12,worldPeerStepPolicy(true,true));
        auto changed=owner();++changed.world;tx.poll(changed,true,false,true,{"old"},3600013);peers.step(tx,3600014,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Recovery&&p.paused()&&p.resourceEpoch()==1&&peers.mask,"world change during selection stops retained owners despite unbounded human wait");
        check(!tx.callback(changed,3600015)&&!tx.started,"world change prevents stale Settings callback admission");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        check(tx.begin(6,owner(),10),"preparation timeout control owner admitted");tx.prepared({"old"},{"new"});
        auto borrow=std::make_unique<WorldPackParticipant::Lease>(p,true);peers.pause(tx,6,1,11);service(p);peers.step(tx,12,worldPeerStepPolicy(true,true));
        tx.poll(owner(),true,false,true,{"old"},30012);peers.step(tx,30012,worldPeerStepPolicy(true,true));
        check(tx.phase==WorldSwitchPhase::Cancelled&&p.paused(),"actual preparation still times out while old borrower is retained");
        borrow.reset();peers.step(tx,30013,worldPeerStepPolicy(true,true));
        check(!p.paused()&&p.resourceEpoch()==1&&!peers.mask,"preparation timeout resumes only after retained old borrower drains");
    }
    {
        WorldPackTransaction tx;check(tx.begin(7,owner(),10),"automatic Apply timeout control admitted");tx.prepared({"old"},{"new"});tx.awaiting({},12);
        tx.poll(owner(),true,false,true,{"old"},30013);
        check(tx.phase==WorldSwitchPhase::Cancelled&&!tx.awaitingSelection,"automatic Apply keeps existing thirty-second callback deadline");
        WorldPackTransaction queued;check(queued.begin(8,owner(),10),"native callback timeout control admitted");queued.prepared({"old"},{"new"});queued.awaiting({},12,true);queued.queueCallback(3600014);
        check(!queued.callback(owner(),3780015)&&queued.phase==WorldSwitchPhase::Recovery&&!queued.started,"late queued native callback remains sealed after its fresh 180-second budget");
    }
    {
        WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
        auto first=owner();check(tx.begin(9,first,10),"editor lifecycle first world admitted");tx.prepared({"old"},{"new"});peers.pause(tx,9,1,11);service(p);peers.step(tx,12,worldPeerStepPolicy(true,true));
        NativeSettingsPackContext editorContext{55,first.client,first.manager,first.world,true};
        NativeSettingsEditorView screen{first.client,first.world,true,true,true,true,true,true,true,true};
        auto editing=[&]{return nativeSettingsEditorPreGame(false,editorContext,screen);};
        check(editing(),"same-world Global editor enables native UI binding independently of Prepared");
        auto unavailable=first;unavailable.pending=true;screen.ownerCurrent=unavailable.usable()&&first.same(unavailable);
        check(!editing(),"native pending restack does not inherit editor compensation");
        unavailable=first;++unavailable.manager;screen.ownerCurrent=unavailable.usable()&&first.same(unavailable);
        check(!editing(),"resource manager replacement ends the previous editor owner grant");
        unavailable=first;++unavailable.renderer;screen.ownerCurrent=unavailable.usable()&&first.same(unavailable);
        check(!editing(),"renderer replacement ends the previous editor owner grant");
        screen.ownerCurrent=true;
        screen.globalTab=false;check(!editing(),"switching to another native Settings tab ends edit compensation");
        screen.globalTab=true;check(editing(),"return to Global tab admits only the same live owner");
        screen.sameTopScene=false;check(!editing(),"another top scene or modal does not inherit Global compensation");
        screen.sameTopScene=true;tx.cancel();peers.step(tx,13,worldPeerStepPolicy(true,true));screen.sessionOpen=false;
        check(!editing()&&!p.paused()&&p.resourceEpoch()==1,"cancelled Settings loses edit compensation while old owners resume");
        auto reentered=first;++reentered.world;
        check(tx.begin(10,reentered,14),"reentered world requires a new transaction");tx.prepared({"old"},{"new"});peers.pause(tx,10,1,15);service(p);peers.step(tx,16,worldPeerStepPolicy(true,true));
        editorContext.world=reentered.world;
        check(!editing(),"same controller address from old world cannot grant reentry editing");
        screen.world=reentered.world;screen.sessionOpen=true;screen.sameController=false;check(!editing(),"reentry requires the newly associated controller");
        screen.sameController=true;check(editing(),"fresh reentered world owner/scene can edit again");
        WorldPackTransaction queued;check(queued.begin(12,reentered,17),"queued editor control owns matching world");queued.prepared({"old"},{"new"});queued.awaiting({},17,true);queued.queueCallback(17);
        auto reserved=screen;reserved.sessionOpen=false;
        check(!nativeSettingsEditorPreGame(false,editorContext,reserved),"native apply reservation ends persistent editor grant before callback starts");
        tx.cancel();peers.step(tx,18,worldPeerStepPolicy(true,true));screen.sessionOpen=false;
        check(!editing(),"closed editor remains denied after cancellation");
        check(tx.begin(11,reentered,19),"world-exit control owns fresh Settings");tx.prepared({"old"},{"new"});peers.pause(tx,11,1,20);service(p);peers.step(tx,21,worldPeerStepPolicy(true,true));
        auto departed=reentered;departed.present=false;tx.poll(departed,true,false,true,{"old"},22);peers.step(tx,23,worldPeerStepPolicy(true,true));
        check(!editing()&&tx.phase==WorldSwitchPhase::Recovery&&p.paused(),"world exit closes compensation and keeps invalidated native owners stopped");
    }
    {
        auto allCurrent=[] {return NativeSettingsCancelledRetryView{true,true,true,true,true,true,true,true,true,true,
            true,true,true,true,true,true,true,true,true,true};};
        NativeSettingsPackContext context{55,33,22,7,true};
        NativeSettingsEditorView editor{33,7,true,false,true,true,true,true,true,true};
        for(unsigned reason=0;reason<3;++reason){
            WorldPackParticipant p;participant=&p;WorldPackPeers peers(peer);WorldPackTransaction tx;
            check(tx.begin(90+reason,owner(),10),"retry cancellation owns the same live Settings root");tx.prepared({"old"},{"new"});
            auto borrow=std::make_unique<WorldPackParticipant::Lease>(p,true);peers.pause(tx,90+reason,1,11);service(p);
            if(reason==2)tx.poll(owner(),true,false,true,{"old"},30012);
            else tx.cancel(); // Expired event or changed selected IDs, before replay.
            auto v=allCurrent();peers.step(tx,30012,worldPeerStepPolicy(true,true));v.peersResumed=peers.cancelVerified;v.peersOpen=!peers.mask;
            check(!nativeSettingsCancelledRetry(tx,v)&&tx.phase==WorldSwitchPhase::Cancelled,
                "expired/selection/prepare-timeout retry waits for actual retained peer borrowers");
            check(nativeSettingsExitRoute(true,false,false,tx.phase!=WorldSwitchPhase::Idle,true)==NativeSettingsExitRoute::Consume,
                "same-root repeated Back remains consumed during cancellation drain");
            editor.sessionOpen=tx.phase==WorldSwitchPhase::Idle;
            check(!nativeSettingsEditorPreGame(false,context,editor),"same-root editing remains closed before peer resume");
            borrow.reset();peers.step(tx,30013,worldPeerStepPolicy(true,true));v.peersResumed=peers.cancelVerified;v.peersOpen=!peers.mask;
            check(peers.cancelVerified&&!p.paused()&&nativeSettingsCancelledRetry(tx,v)&&tx.phase==WorldSwitchPhase::Idle,
                "each pre-native cancellation resets the same-root editor only after verified old-epoch resume");
            editor.sessionOpen=tx.phase==WorldSwitchPhase::Idle;
            check(nativeSettingsEditorPreGame(false,context,editor),"same live Global editor opens after cancellation retry reset");
            check(nativeSettingsExitRoute(true,false,false,tx.phase!=WorldSwitchPhase::Idle,true)==NativeSettingsExitRoute::Inspect
                &&nativeSettingsExitRoute(true,false,false,false,false)==NativeSettingsExitRoute::OriginNormal,
                "same-root Back can inspect a fresh target or exit through the normal native no-change route");
            check(tx.begin(100+reason,owner(),30014),"same root starts a fresh apply transaction after cancellation");
            tx.prepared({"old"},{"changed"});peers.pause(tx,100+reason,1,30015);service(p);
            check(peers.step(tx,30016,worldPeerStepPolicy(true,true))==WorldPeerAction::Navigate&&tx.targetGlobal==std::vector<std::string>{"changed"},
                "fresh apply retries through actual Pause and Prepared with new token/selection");
        }
        for(auto field:{&NativeSettingsCancelledRetryView::nativeSettings,&NativeSettingsCancelledRetryView::peersResumed,
            &NativeSettingsCancelledRetryView::peersOpen,&NativeSettingsCancelledRetryView::pendingReleased,
            &NativeSettingsCancelledRetryView::notReplayed,&NativeSettingsCancelledRetryView::notNavigated,
            &NativeSettingsCancelledRetryView::noCompletion,&NativeSettingsCancelledRetryView::activeOperation,
            &NativeSettingsCancelledRetryView::ownerCurrent,&NativeSettingsCancelledRetryView::ownerThread,
            &NativeSettingsCancelledRetryView::sameRoot,&NativeSettingsCancelledRetryView::sameModel,
            &NativeSettingsCancelledRetryView::sameController,&NativeSettingsCancelledRetryView::sameSource,
            &NativeSettingsCancelledRetryView::sameTopScene,&NativeSettingsCancelledRetryView::settingsScreen,
            &NativeSettingsCancelledRetryView::globalPolicy,&NativeSettingsCancelledRetryView::oldGlobalUnchanged,
            &NativeSettingsCancelledRetryView::otherStacksUnchanged,&NativeSettingsCancelledRetryView::callbacksIdle}){
            WorldPackTransaction tx;tx.begin(101,owner(),10);tx.prepared({"old"},{"new"});tx.cancel();
            auto v=allCurrent();v.*field=false;
            check(!nativeSettingsCancelledRetry(tx,v)&&tx.phase==WorldSwitchPhase::Cancelled&&tx.token==101,
                "every peer/lifetime/root/model/source/owner/world/stack/continuation gate must remain current before reset");
        }
        for(auto phase:{WorldSwitchPhase::Reloading,WorldSwitchPhase::Reacquiring,WorldSwitchPhase::Complete,
            WorldSwitchPhase::Restored,WorldSwitchPhase::Recovery}){
            WorldPackTransaction tx;tx.begin(102,owner(),10);tx.phase=phase;
            check(!nativeSettingsCancelledRetry(tx,allCurrent())&&tx.phase==phase,"postcommit or recovery never returns to Idle");
        }
        for(unsigned progress=0;progress<8;++progress){
            WorldPackTransaction tx;tx.begin(103,owner(),10);tx.prepared({"old"},{"new"});tx.cancel();
            if(progress==0)tx.attempt=1;if(progress==1)tx.started=true;if(progress==2)tx.startReturned=true;
            if(progress==3)tx.finished=true;if(progress==4)tx.restoring=true;if(progress==5)tx.needRollback=true;
            if(progress==6)tx.changed=true;if(progress==7)tx.geometryRequested=true;
            check(!nativeSettingsCancelledRetry(tx,allCurrent()),"native attempt/progress cannot be reset as a pre-native cancellation");
        }
        WorldPackTransaction tx;tx.begin(110,owner(),10);tx.prepared({"old"},{"new"});tx.cancel();
        NativeSettingsApplyOnce old;old.token=110;old.world=7;
        check(nativeSettingsCancelledRetry(tx,allCurrent()),"old callback regression resets a verified cancelled attempt");
        check(tx.begin(111,owner(),11),"new callback regression admits a new transaction");tx.prepared({"old"},{"new"});tx.awaiting({},12,true);
        auto before=tx.phase;
        auto dispatch=[&](NativeSettingsApplyOnce& once,std::uint64_t world,bool current){
            auto admission=nativeSettingsApplyAdmission(once,tx.token,world,current);
            if(admission==NativeSettingsApplyAdmission::Duplicate)tx.seal();return admission;
        };
        check(dispatch(old,7,true)==NativeSettingsApplyAdmission::Stale&&tx.phase==before&&!old.claimed.load(),
            "old captured callback cannot claim, seal, or mutate a new same-root transaction");
        old.claimed=true;
        check(dispatch(old,7,true)==NativeSettingsApplyAdmission::Stale&&tx.phase==before,
            "copied already-used old callback cannot seal the fresh transaction");
        NativeSettingsApplyOnce fresh;fresh.token=111;fresh.world=7;
        check(dispatch(fresh,8,true)==NativeSettingsApplyAdmission::Stale&&dispatch(fresh,7,false)==NativeSettingsApplyAdmission::Stale
            &&tx.phase==before&&!fresh.claimed.load(),"stale world or context cannot claim or mutate the new transaction");
        check(dispatch(fresh,7,true)==NativeSettingsApplyAdmission::Execute,"fresh exact callback identity executes once");
        check(dispatch(fresh,7,true)==NativeSettingsApplyAdmission::Duplicate&&tx.phase==WorldSwitchPhase::Recovery,
            "duplicate current callback preserves the existing Recovery quarantine");
    }
    std::printf("NativeSettingsPackScopeTests PASS (%u checks)\n",checks);
}
