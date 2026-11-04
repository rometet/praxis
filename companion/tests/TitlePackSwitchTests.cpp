#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/TitleNativeCast.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstring>

using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
TitlePackOwner owner(){return {11,22,33,0,44,true,true,false,true};}
TitlePackTransaction prepared(){TitlePackTransaction t;check(t.begin(1,owner(),10),"fresh title admitted");t.prepared({"old"},{"new"},11);return t;}
void start(TitlePackTransaction& t){check(t.callback(owner(),12)==TitlePackAction::Apply,"setter admitted only from native application callback");check(t.nativeStart(owner(),1),"native start bound to operation attempt");t.nativeStartReturned(1,true);t.setterResult(true,false);}
TitlePackSettlement settled(std::vector<std::string> ids={"new"}){return {owner(),true,true,true,std::move(ids)};}
void finish(TitlePackTransaction& t){t.nativeFinish(t.attempt,true);}
}
int main(){
    {struct Base{virtual ~Base()=default;};struct Title:Base{};struct Other:Base{};
        bool unavailable{};std::shared_ptr<Base> empty;
        check(!titleNativeSharedCast<Title>(empty,unavailable)&&!unavailable,"null native owner is rejected without RTTI inspection");
        auto original=std::make_shared<Title>();std::shared_ptr<Base> base=original;
        auto title=titleNativeSharedCast<Title>(base,unavailable);
        check(title.get()==original.get()&&!unavailable,"valid RTTI preserves exact title owner");
        check(!title.owner_before(base)&&!base.owner_before(title)&&base.use_count()==3,"native shared cast aliases original control block");
        auto other=std::make_shared<Other>();std::shared_ptr<Base> unrelated=other;
        check(!titleNativeSharedCast<Title>(unrelated,unavailable)&&!unavailable&&unrelated.use_count()==2,"unrelated native owner is rejected without granting access");
        std::weak_ptr<Title> weak=original;original.reset();base.reset();
        check(!weak.expired()&&title.use_count()==1,"successful alias keeps native owner alive");
        title.reset();check(weak.expired(),"last cast owner releases its original control block");}
    {auto t=prepared();check(t.phase==TitlePackPhase::AwaitApply,"preparation cannot invoke setter");check(t.cancel(),"pre-native cancel accepted");check(t.callback(owner(),12)==TitlePackAction::None,"cancelled callback cannot mutate");}
    {auto t=prepared();auto x=owner();x.loadingFinished=false;x.renderReady=false;check(t.callback(x,12)==TitlePackAction::Apply,"normal application callback can start while its own progress screen reports loading");}
    {auto t=prepared();check(t.callback(owner(),12)==TitlePackAction::Apply,"first callback grants one setter");check(t.callback(owner(),13)==TitlePackAction::None,"duplicate callback cannot invoke second setter");t.setterResult(false,true);check(t.phase==TitlePackPhase::Rejected,"false setter with unchanged old state rejects");}
    {auto t=prepared();start(t);check(!t.cancel(),"cancel does not interrupt native reload");
        check(t.poll(settled(),15)==TitlePackAction::None&&t.busy(),"loading-finished alone is not completion");finish(t);
        check(t.poll(settled(),16)==TitlePackAction::None&&t.busy(),"first settled update is insufficient");t.poll(settled(),17);check(t.phase==TitlePackPhase::Complete,"finish plus normal UI return and two matching settled reads complete");}
    {auto t=prepared();start(t);finish(t);auto x=settled();x.startScreen=false;t.poll(x,16);check(t.busy(),"application screen must finish before completion");
        x=settled();x.callbacksIdle=false;t.poll(x,17);check(t.busy(),"in-flight native callback prevents completion");
        x=settled();x.owner.pending=true;t.poll(x,18);check(t.busy(),"pending restack prevents completion");
        x=settled();x.owner.loadingFinished=false;t.poll(x,19);check(t.busy(),"official native loading state must finish");}
    {auto t=prepared();auto x=owner();x.world=1;x.fresh=false;check(t.callback(x,12)==TitlePackAction::None,"world callback cannot invoke setter");check(t.phase==TitlePackPhase::Rejected,"world change before mutation preserves state");}
    {auto t=prepared();start(t);auto x=settled();x.owner.client=99;t.poll(x,13);check(t.phase==TitlePackPhase::RecoveryRequired,"client change after native begin retains owners");}
    {auto t=prepared();start(t);check(!t.nativeStart(owner(),1)&&t.phase==TitlePackPhase::RecoveryRequired,"foreign or duplicate native start is rejected");}
    {auto t=prepared();start(t);t.nativeFinish(2,true);check(t.phase==TitlePackPhase::RecoveryRequired,"wrong generation finish is rejected");}
    {auto t=prepared();start(t);t.nativeStartReturned(1,false);check(t.phase==TitlePackPhase::RecoveryRequired,"native exception cannot produce success");}
    {auto t=prepared();start(t);finish(t);auto x=settled();x.otherStacksUnchanged=false;t.poll(x,15);check(t.phase==TitlePackPhase::RecoveryRequired,"other stacks are never overwritten for rollback");}
    {auto t=prepared();start(t);finish(t);auto x=settled({"partial"});t.poll(x,15);check(t.poll(x,16)==TitlePackAction::NavigateRollback,"mismatch schedules rollback only after settlement");
        check(t.rollbackNavigation(17),"rollback uses a new normal application screen");
        check(t.callback(owner(),18)==TitlePackAction::Rollback&&t.attempt==2,"rollback has a fresh generation and one setter");
        check(t.callback(owner(),18)==TitlePackAction::None,"rollback setter cannot run twice");
        check(t.nativeStart(owner(),2),"rollback start correlated");t.nativeStartReturned(2,true);t.setterResult(true,false);finish(t);
        t.poll(settled({"old"}),20);t.poll(settled({"old"}),21);check(t.phase==TitlePackPhase::Restored,"old stack restoration verified through complete normal cycle");}
    {auto t=prepared();start(t);t.setterResult(false,false);finish(t);t.poll(settled({"old"}),15);t.poll(settled({"old"}),16);check(t.phase==TitlePackPhase::Restored,"already restored old stack does not need another setter");}
    {auto t=prepared();start(t);finish(t);auto x=settled({"partial"});t.poll(x,15);t.poll(x,16);t.rollbackNavigation(17);
        check(t.callback(owner(),18)==TitlePackAction::Rollback,"rollback attempt is admitted");t.setterResult(false,false);
        check(t.phase==TitlePackPhase::RecoveryRequired,"rollback setter refusal cannot claim restoration");}
    {auto t=prepared();start(t);finish(t);auto x=settled({"partial"});t.poll(x,15);t.poll(x,16);t.rollbackNavigation(17);
        check(t.callback(owner(),TitlePackTransaction::CallbackBudget+18)==TitlePackAction::None&&t.phase==TitlePackPhase::RecoveryRequired,"expired rollback callback cannot force a setter or claim unchanged state");}
    {auto t=prepared();start(t);t.poll(settled(),TitlePackTransaction::ReloadBudget+13);check(t.phase==TitlePackPhase::RecoveryRequired,"deadline never forces flush or rollback setter");}
    {auto t=prepared();check(t.callback(owner(),TitlePackTransaction::CallbackBudget+12)==TitlePackAction::None&&t.phase==TitlePackPhase::Rejected,"expired pre-native callback cannot mutate");}
    {TitlePackTransaction t;auto x=owner();x.fresh=false;check(!t.begin(1,x,0),"in-world/after-world not admitted");x=owner();x.pending=true;check(!t.begin(1,x,0),"pre-existing reload not admitted");}
    {std::vector<std::string> old{"fixedA","a","fixedB","b"},selected{"b","a","c"};
        auto merged=mergeTitleGlobal<std::string>(old,selected,[](auto const& s){return s=="a"||s=="b";},[](auto const& a,auto const& b){return a==b;});
        check(merged&&*merged==std::vector<std::string>{"fixedA","b","fixedB","a","c"},"untargeted Global anchors and relative order preserved");
        std::vector<std::string> conflict{"fixedB"};check(!mergeTitleGlobal<std::string>(old,conflict,[](auto const& s){return s=="a"||s=="b";},[](auto const& a,auto const& b){return a==b;}),"target conflicts with preserved pack identity rejected");
        std::vector<std::string> none;merged=mergeTitleGlobal<std::string>(old,none,[](auto const& s){return s=="a"||s=="b";},[](auto const& a,auto const& b){return a==b;});check(merged&&*merged==std::vector<std::string>{"fixedA","fixedB"},"empty selection removes only editable ordinary textures");}
    {TitlePackControl control;TitlePackView v;v.available=true;v.until=100;v.revision=7;v.current.known=true;control.publish(v);
        auto catalog=std::make_shared<Catalog>();catalog->complete=true;catalog->generation=1;catalog->packs.push_back({"a","a","uuid","1.0.0","","texture",true});Selection s;s.generation=1;s.order={"a"};
        auto unknown=v;unknown.current={};control.publish(unknown);check(!control.request(catalog,s,10),"Unknown current state cannot queue application");control.publish(v);
        check(!control.request(catalog,s,101),"stale frontend availability cannot queue work");check(control.request(catalog,s,10),"GUI queues copied CPU proposal without native call");
        check(!control.request(catalog,s,11),"only one request admitted");control.cancel(control.view(10).token);check(!control.take(),"queued cancellation cannot be lost before native consume");
        control.publish(v);check(control.request(catalog,s,10),"fresh title can request again");auto req=control.take();check(req&&req->revision==7&&req->selection.order==s.order,"native receives exact frozen proposal/revision");
        control.close();control.publish(v);check(!control.view(10).available&&!control.view(10).current.known&&!control.request(catalog,s,10),"stop and late publications cannot reopen admission");}
    {Pack p{"a","texture","945a688d-82d4-4e9a-85f4-2a4871da0e2d","0.0.1","","texture",true};
        AppliedStackView current;check(packCurrentState(p,current)==PackCurrentState::Unknown,"unknown Global observation is never inactive");
        current.known=true;check(packCurrentState(p,current)==PackCurrentState::Inactive,"known absent header is inactive");
        current.order.push_back({p.uuid,p.version,"",6});
        check(packCurrentState(p,current)==PackCurrentState::EditableActive,"ordinary current header is active and editable");
        current.order[0].subpack="default";
        check(packCurrentState(p,current)==PackCurrentState::ReadOnlyActive,"active named-subpack header must not be mislabeled inactive");
        check(!matchesApplied(p,current.order[0])&&!appliedTo(p,current).value(),"display observation does not relax native/editable match");
        current.order[0].subpack.clear();current.order[0].packType=0;
        check(packCurrentState(p,current)==PackCurrentState::ReadOnlyActive&&!matchesApplied(p,current.order[0]),"unexpected native type remains active for display but grants no editing permission");
        current.order[0].packType=6;p.selectable=false;
        check(packCurrentState(p,current)==PackCurrentState::ReadOnlyActive,"unsupported content currently active is shown as active read-only");}
    {PackDiagnosticBudget budget;
        std::array<std::thread,4> workers;
        std::atomic<unsigned> admitted{};
        for(auto& worker:workers)worker=std::thread([&]{for(unsigned i=0;i<200;++i)if(budget.take())++admitted;});
        for(auto& worker:workers)worker.join();
        check(admitted==PackDiagnosticBudget::Limit&&budget.count()==PackDiagnosticBudget::Limit,"concurrent diagnostic flood stops exactly at process cap");
        check(!budget.take()&&budget.count()==PackDiagnosticBudget::Limit,"exhausted diagnostic cap never increments or reopens");}
    {PackDiagnosticQueue queue;
        std::array<std::thread,4> workers;
        for(unsigned t=0;t<workers.size();++t)workers[t]=std::thread([&,t]{for(unsigned i=0;i<200;++i)queue.record("event=fixture thread=%u value=%u",t,i);});
        for(auto& worker:workers)worker.join();
        unsigned total{},maxBatch{};bool valid=true;
        for(unsigned pass=0;pass<100;++pass){auto batch=queue.drain([&](char const* text) noexcept {++total;valid=valid&&std::strncmp(text,"event=fixture thread=",21)==0;});maxBatch=std::max(maxBatch,batch);}
        check(total==PackDiagnosticBudget::Limit&&valid,"concurrent records are published once without torn payloads or extra logs");
        check(maxBatch==8,"normal owner logger work has an eight-line drain bound");
        check(queue.drain([](char const*) noexcept {})==0,"drained bounded queue never emits duplicate lines");}
    {TitlePackControl control;TitlePackView v;v.available=true;v.until=100;v.current.known=true;v.canReload=true;control.publish(v);
        auto catalog=std::make_shared<Catalog>();catalog->complete=true;catalog->generation=1;Selection s;s.generation=1;
        auto result=control.requestResult(catalog,s,10,true);
        check(result.accepted&&result.block==TitleRequestBlock::None&&result.token==1,"detailed reload request accepted with same original guards");
        auto request=control.take();check(request&&request->reload&&request->selection.order.empty(),"reload intent and empty removal proposal survive dispatch");
        check(control.requestResult(catalog,s,11).block==TitleRequestBlock::Busy,"busy rejection cannot mutate queue");
        control.cancel(result.token);control.publish(v);v.canReload=false;control.publish(v);
        check(control.requestResult(catalog,s,10,true).block==TitleRequestBlock::ReloadUnavailable&&!control.take(),"title reload refusal identifies reason and queues nothing");
        v.canReload=true;control.publish(v);check(control.requestResult(catalog,s,101).block==TitleRequestBlock::Expired&&!control.take(),"expired observation cannot queue work");
        v.current={};control.publish(v);check(control.requestResult(catalog,s,10).block==TitleRequestBlock::UnknownCurrent,"unknown actual stack blocks detailed request");
        v.current.known=true;v.available=false;v.block=PackApplyBlock::Loading;control.publish(v);
        check(control.requestResult(catalog,s,10).block==TitleRequestBlock::Unavailable,"diagnostic reason cannot reopen admission");
        v.available=true;control.publish(v);s.generation=2;
        check(control.requestResult(catalog,s,10).block==TitleRequestBlock::StaleCatalog&&!control.take(),"stale selection remains blocked with detailed reason");
        control.close();check(control.requestResult(catalog,s,10).block==TitleRequestBlock::Closed,"closed remains terminal");
        check(control.view(10).block==PackApplyBlock::Closed&&!control.view(10).current.known,"closed copied view explains blocked state without granting current stack");}
    {TitlePackControl control;TitlePackView previous;previous.available=true;previous.until=100;previous.revision=7;previous.current.known=true;
        auto catalog=std::make_shared<Catalog>();catalog->complete=true;catalog->generation=1;Selection selection;selection.generation=1;
        control.publish(previous);auto first=control.requestResult(catalog,selection,10);check(first.accepted&&first.token==1,"first attributed request accepted");
        check(control.take()->token==first.token,"first request consumed");
        previous.token=first.token;previous.phase=TitlePackPhase::Complete;previous.status="first result";previous.observation.world=true;previous.observation.finished=true;previous.observation.stackChecked=true;
        control.publish(previous);check(control.view(10).token==first.token&&control.view(10).status=="first result","published result keeps its producer token");
        auto second=control.requestResult(catalog,selection,11);check(second.accepted&&second.token==2,"next request has an independent token");
        auto queued=control.view(11);check(queued.token==second.token&&queued.phase==TitlePackPhase::Queued&&queued.status==titlePackStatusJa(TitlePackPhase::Queued),"queue publication is attributed to the queued request");
        check(!queued.observation.world&&!queued.observation.finished&&!queued.observation.stackChecked,"queued request cannot inherit prior native completion");
        control.publish(previous);queued=control.view(11);check(queued.token==second.token&&queued.phase==TitlePackPhase::Queued&&!queued.observation.world,"old producer publication during queue preserves queue attribution only");
        auto dequeued=control.take();check(dequeued&&dequeued->token==second.token&&dequeued->revision==7,"next request dequeued unchanged");
        control.publish(previous);auto old=control.view(12);check(old.token==first.token&&old.status=="first result"&&old.observation.finished,"retained old result is never relabeled with latest dequeued token");
        TitlePackView rejection;rejection.until=100;rejection.token=dequeued->token;rejection.phase=TitlePackPhase::Rejected;rejection.status="second rejected before native";control.publish(rejection);
        auto result=control.view(12);check(result.token==second.token&&result.phase==TitlePackPhase::Rejected&&result.status==rejection.status&&!result.observation.world,"dequeue refusal belongs to rejected request without old native observations");
        control.publish(previous);check(control.view(12).token==first.token,"later retained operation publication keeps its own token");
        check(control.requestResult(catalog,selection,101).block==TitleRequestBlock::Expired&&control.view(12).token==first.token,"unaccepted request cannot change result attribution");
        control.publish(previous);auto third=control.requestResult(catalog,selection,13);check(third.accepted&&third.token==3,"third request token advances once");
        check(control.cancel(third.token),"queued cancellation still uses exact current token");auto cancelled=control.view(13);
        check(cancelled.token==third.token&&cancelled.phase==TitlePackPhase::Rejected&&!cancelled.busy&&!cancelled.observation.world&&!control.take(),"cancellation result belongs to cancelled queue and clears previous observation");}
    std::printf("Title application controller: %u checks PASS; native Minecraft NOT_RUN\n",checks);
}
