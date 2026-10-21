#include "resourcepacks/TitlePackNativeAdapter.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <latch>
#include <stdexcept>
#include <thread>

using namespace praxis::companion;
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
TitlePackOwner owner(){return {11,22,33,0,44,true,true,false,true};}
struct Fixture {
    NativeHookLifetime lifetime;
    std::atomic<unsigned> callbacks{};
    TitlePackControl control;
    std::mutex mutex;
    TitlePackTransaction transaction;
    TitleNativeContext native;
    std::uint64_t token{},cycles{};
    unsigned setters{},origins{};
    Fixture(){
        check(lifetime.prepare([]{return true;}),"lifetime pin boundary");lifetime.registrationStarted();check(lifetime.open(),"lifetime opens");
        TitlePackView view;view.available=true;view.current.known=true;view.until=100;control.publish(view);
        auto catalog=std::make_shared<Catalog>();catalog->complete=true;catalog->generation=1;catalog->packs.push_back({"a","a","uuid","1.0.0","","texture",true});Selection selection;selection.generation=1;selection.order={"a"};
        check(control.request(catalog,selection,10),"actual queue admits request");auto request=control.take();check(bool(request),"actual queue dequeues request");token=request->token;
        check(transaction.begin(token,owner(),10),"begin queued transaction");transaction.prepared({"old"},{"new"},11);
    }
    template<class Validate,class Set> void apply(Validate&& validate,Set&& set){
        runTitlePackApplyCallback(lifetime,callbacks,control,mutex,transaction,[]{return owner();},
            std::forward<Validate>(validate),std::forward<Set>(set),12);
    }
    TitleSetterResult setter(TitlePackAction){
        ++setters;std::lock_guard lock(mutex);
        native=beginTitleNativeCycle(transaction,titleNativeContext,owner(),true,++cycles);
        check(native.receipt(),"native entry inherits this claimed setter seed");transaction.nativeStartReturned(native.attempt,true);
        return {true,false};
    }
    void apply(){apply([](auto){return true;},[&](auto action){return setter(action);});}
    void finish(TitleNativeContext context,bool full=true,bool throwing=false){
        invokeTitleNativeBody(context,lifetime,callbacks,[&]{
            TitleCallbackLease lease(lifetime,callbacks);
            observeTitleNativeFinish(mutex,transaction,native,titleNativeContext,11,22,0,full,[&]{++origins;if(throwing)throw std::runtime_error("native fixture");});
        });
    }
    TitlePackSettlement settled(std::vector<std::string> global){return {owner(),true,true,true,std::move(global)};}
    void clean(){check(callbacks.load()==0&&lifetime.readers()==0,"callback counts and lifetime readers balance");}
};
struct FailAllocation {
    template<class Callable> std::shared_ptr<TitleNativeCallable<Callable>> operator()(Callable&&)const {throw std::bad_alloc();}
};
}
int main(){
    {Fixture f;check(f.control.cancel(f.token),"cancel remains bound after dequeue");f.apply();check(f.setters==0&&f.transaction.phase==TitlePackPhase::Rejected,"dequeued cancellation prevents actual adapter setter");f.clean();}
    {Fixture f;f.apply([&](auto){check(f.control.cancel(f.token),"cancel during native validation accepted before claim");return true;},[&](auto action){return f.setter(action);});
        check(f.setters==0&&f.transaction.phase==TitlePackPhase::Rejected,"validation-to-setter cancellation race cannot mutate");f.clean();}
    {Fixture f;std::latch validating{1},proceed{1};std::thread callback([&]{f.apply([&](auto){validating.count_down();proceed.wait();return true;},[&](auto action){return f.setter(action);});});
        validating.wait();bool cancelled=f.control.cancel(f.token);proceed.count_down();callback.join();
        check(cancelled&&f.setters==0&&f.transaction.phase==TitlePackPhase::Rejected,"concurrent token cancellation during callback prevents mutation");f.clean();}
    {Fixture f;check(!f.control.cancel(f.token+1),"foreign cancellation token rejected");f.apply([](auto){return true;},[&](auto action){check(!f.control.cancel(f.token),"cancel loses after mutation claim");return f.setter(action);});
        check(f.setters==1,"one mutation claim executes once");f.apply();check(f.setters==1,"duplicate callback cannot execute setter twice");f.clean();}
    {Fixture f;f.control.close();f.lifetime.close();f.apply();check(f.setters==0,"closed lifetime prevents late callback mutation");f.clean();}
    {Fixture f;f.apply([&](auto){check(f.callbacks.load()==1&&f.lifetime.readers()==1,"Apply validation is inside callback lease");f.control.close();f.lifetime.close();return true;},[&](auto action){return f.setter(action);});
        check(f.setters==0,"stop that wins final claim prevents setter");f.clean();}
    {Fixture f;std::latch validating{1},proceed{1};std::thread callback([&]{f.apply([&](auto){validating.count_down();proceed.wait();return true;},[&](auto action){return f.setter(action);});});
        validating.wait();bool retained=f.callbacks.load()==1&&f.lifetime.readers()==1;f.control.close();f.lifetime.close();
        {std::lock_guard lock(f.mutex);f.transaction.recovery();}proceed.count_down();callback.join();
        check(retained&&f.setters==0&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"concurrent stop retains callback and does not downgrade sealed recovery to rejection");f.clean();}
    {Fixture f;f.apply([](auto){return true;},[&](auto action){
            check(f.callbacks.load()==1&&f.lifetime.readers()==1,"claimed setter retains callback lease");
            f.control.close();f.lifetime.close();check(f.lifetime.readers()!=0,"nonblocking stop sees admitted in-flight callback");
            auto result=f.setter(action);f.transaction.recovery();return result;
        });check(f.setters==1&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"already claimed setter remains retained during stop without false success");f.clean();}
    {Fixture f;f.apply();f.finish({});check(f.origins==1&&!f.transaction.finished&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"unknown finish runs origin but cannot borrow current attempt");f.clean();}
    {Fixture f;f.apply();auto foreign=f.native;++foreign.cycle;f.finish(foreign);check(f.origins==1&&!f.transaction.finished&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"foreign engine cycle cannot complete current attempt through native adapter");f.clean();}
    {Fixture f;f.apply();auto old=f.native;f.finish(old);f.transaction.poll(f.settled({"partial"}),15);f.transaction.poll(f.settled({"partial"}),16);
        check(f.transaction.rollbackNavigation(17),"normal settled mismatch enters rollback route");f.transaction.targetGlobal={"partial"};f.apply();check(f.native.attempt==2&&f.native.cycle!=old.cycle,"rollback owns distinct native provenance");
        std::thread late([&]{f.finish(old);});late.join();check(f.origins==2&&!f.transaction.finished&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"old asynchronous finish is never relabeled as rollback attempt");f.clean();}
    {Fixture f;f.apply();f.finish(f.native,false);check(!f.transaction.finished&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"native flag mismatch rejects receipt");f.clean();}
    {Fixture f;f.apply();bool caught{};try{f.finish(f.native,true,true);}catch(std::runtime_error const&){caught=true;}
        check(caught&&f.origins==1&&!f.transaction.finished&&f.transaction.phase==TitlePackPhase::RecoveryRequired,"native origin exception propagates without completion");f.clean();}
    {Fixture f;f.apply();std::thread worker([&]{f.finish(f.native);});worker.join();
        check(f.transaction.finished&&f.origins==1,"matching captured async native cycle accepted");
        check(!titleNativeContext.seed(),"native scope restores caller context");
        f.transaction.poll(f.settled({"partial"}),15);f.transaction.poll(f.settled({"new"}),16);
        check(f.transaction.phase==TitlePackPhase::Reloading&&f.transaction.stable==1,"partial then target is only one matching observation");
        f.transaction.poll(f.settled({"new"}),17);check(f.transaction.phase==TitlePackPhase::Complete,"two consecutive target identities required for Complete");f.clean();}
    {Fixture f;f.apply();f.finish(f.native);f.transaction.poll(f.settled({"partial"}),15);f.transaction.poll(f.settled({"partial"}),16);f.transaction.rollbackNavigation(17);f.transaction.targetGlobal={"partial"};f.apply();f.finish(f.native);
        f.transaction.poll(f.settled({"partial"}),18);f.transaction.poll(f.settled({"old"}),19);
        check(f.transaction.phase==TitlePackPhase::RollingBack&&f.transaction.stable==1,"partial then old does not falsely restore");
        f.transaction.poll(f.settled({"old"}),20);check(f.transaction.phase==TitlePackPhase::Restored,"two consecutive old identities required for Restored");f.clean();}
    {Fixture f;f.apply();f.finish(f.native);f.transaction.poll(f.settled({"new"}),15);auto interrupted=f.settled({"new"});interrupted.callbacksIdle=false;f.transaction.poll(interrupted,16);f.transaction.poll(f.settled({"new"}),17);
        check(f.transaction.phase==TitlePackPhase::Reloading&&f.transaction.stable==1,"loss of settlement resets identity streak");f.transaction.poll(f.settled({"new"}),18);check(f.transaction.phase==TitlePackPhase::Complete,"fresh streak required after interrupted settlement");f.clean();}
    {Fixture f;f.apply();auto context=f.native;bool lost{};unsigned bodies{};
        std::function<void()> task=[&]{++bodies;check(titleNativeContext.same(context),"native factory wrapper carries immutable context");};
        auto wrapped=wrapTitleNativeCallable<void>(context,f.lifetime,f.callbacks,std::move(task),[](auto body){return body;},lost);
        std::thread worker([&]{wrapped();});worker.join();check(!lost&&bodies==1,"engine factory body executes exactly once asynchronously");f.clean();}
    {Fixture f;f.apply();bool lost{};unsigned bodies{};std::function<void()> task=[&]{++bodies;};
        auto fallback=wrapTitleNativeCallable<void>(f.native,f.lifetime,f.callbacks,std::move(task),[](auto body){return body;},lost,FailAllocation{});
        fallback();check(lost&&bodies==1,"allocation failure preserves intact original callable");f.clean();}
    {Fixture f;f.apply();auto sentinel=std::make_shared<int>(7);std::weak_ptr<int> weak=sentinel;std::function<void()> task=[sentinel]{};sentinel.reset();bool lost{};
        wrapTitleNativeCallable<void>(f.native,f.lifetime,f.callbacks,std::move(task),[&](auto body){body=nullptr;check(weak.expired(),"native factory rejection disposes original captures without local owner extension");return 0;},lost);
        check(!lost,"normal native rejection is not observation allocation failure");f.clean();}
    std::printf("Title native adapter: %u checks PASS; native Minecraft NOT_RUN\n",checks);
}
