#include "resourcepacks/WorldPackNativeAdapter.h"
#include "resourcepacks/WorldNativeExecutedCompletion.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>

using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
WorldNativeContext context(){
    WorldNativeContext value;
    value.token=3;value.attempt=2;value.cycle=9;value.producer=9;value.finishProducer=17;
    value.owner={11,22,33,55,7,44,true,true,false};
    return value;
}
}
int main(){
    WorldNativeExecutedCompletion::ObservedOwner const live{11,33,7};
    praxis::companion::NativeHookLifetime lifetime;
    std::atomic<unsigned> callbacks{};
    check(lifetime.prepare([]{return true;}),"lifetime pin");
    lifetime.registrationStarted();check(lifetime.open(),"lifetime open");
    {
        std::shared_ptr<WorldNativeTaskReceipt> receipt;
        std::function<void()> pending;
        bool lost{};
        wrapWorldCallable<void>(context(),lifetime,callbacks,std::function<void()>([]{std::abort();}),
            [&](auto task){pending=std::move(task);return 1;},lost,TitleNativeAllocation{},nullptr,&receipt);
        check(!lost&&receipt&&receipt->entries==0&&!receipt->disposed,"factory creation is not execution");
        check(receipt->source.same({3,2,9,9,17}),"task stores full immutable producer key");
        pending={};
        check(receipt->neverInvoked()&&receipt->returns==0,"disposal of unexecuted callable is distinct");
    }
    {
        std::shared_ptr<WorldNativeTaskReceipt> parentReceipt,childReceipt;
        std::function<void()> parent,child;
        WorldNativeContext parentAtEntry{},childAtEntry{};
        bool lost{};
        wrapWorldCallable<void>(context(),lifetime,callbacks,std::function<void()>([&]{
            parentAtEntry=worldNativeContext;
            wrapWorldCallable<void>(worldNativeContext,lifetime,callbacks,std::function<void()>([&]{childAtEntry=worldNativeContext;}),
                [&](auto task){child=std::move(task);return 1;},lost,TitleNativeAllocation{},nullptr,&childReceipt);
        }),[&](auto task){parent=std::move(task);return 1;},lost,TitleNativeAllocation{},nullptr,&parentReceipt);
        check(parentReceipt&&parentReceipt->entries==0,"no invocation at factory entry");
        parent();
        check(parentReceipt->entries==1&&parentReceipt->returns==1&&!parentReceipt->neverInvoked(),"normal callable return receipt");
        check(parentAtEntry.producer==9&&parentAtEntry.invocation&&parentAtEntry.task==parentReceipt->task,"actual parent entry owns lineage");
        check(childReceipt&&childReceipt->parentInvocation==parentAtEntry.invocation&&childReceipt->entries==0,"child factory inherits only actual parent invocation");
        check(childReceipt->source.same(parentReceipt->source),"child preserves token attempt cycle and producer");
        child();
        check(childAtEntry.producer==9&&childAtEntry.parentInvocation==parentAtEntry.invocation&&childAtEntry.invocation!=parentAtEntry.invocation,"child invocation binds exact parent");
        auto first=childAtEntry.invocation;child();
        check(childReceipt->entries==2&&childReceipt->returns==2&&childAtEntry.invocation!=first,"requeued callable receives distinct invocation IDs");
        check(!worldNativeContext.invocation,"nested TLS scopes restore caller");
    }
    {
        std::shared_ptr<WorldNativeTaskReceipt> receipt;
        std::function<void()> pending;bool lost{};
        wrapWorldCallable<void>(context(),lifetime,callbacks,std::function<void()>([]{throw std::runtime_error("body");}),
            [&](auto task){pending=std::move(task);return 1;},lost,TitleNativeAllocation{},nullptr,&receipt);
        try{pending();check(false,"throw must escape wrapper");}catch(std::runtime_error const&){}
        check(receipt->entries==1&&receipt->throws==1&&receipt->returns==0,"throw is distinct from normal return");
        pending={};check(receipt->disposed&&!receipt->neverInvoked(),"invoked disposal is not unexecuted disposal");
    }
    {
        WorldNativeExecutedCompletion state;
        WorldNativeProducer source{3,2,9,9,17},foreign{3,3,9,9,17};
        WorldNativeInvocation finalize{source,30,31,0},level{source,40,41,0},tess{source,50,51,31};
        check(state.begin(source,11,33,7),"exact producer bound");
        check(!state.loadingFinished(11,33,7,true)&&!state.nativeReadyForFrame(),"loading flag before native execution is insufficient");
        check(!state.beginFrame(11,33,7,55,101),"frame entered before callbacks complete has no permit");
        check(!state.enter(WorldNativeExecutedCompletion::Part::Finalize,{foreign,30,31,0},11,live),"foreign attempt ignored");
        check(state.enter(WorldNativeExecutedCompletion::Part::Finalize,finalize,11,live),"actual Finalize entry accepted");
        check(state.enter(WorldNativeExecutedCompletion::Part::Tess11,tess,11,live),"Tess child inherits exact Finalize invocation");
        check(state.returned(WorldNativeExecutedCompletion::Part::Tess11,tess,live,true,false),"native Tess requeue remains unfinished");
        tess.invocation=52;
        check(state.enter(WorldNativeExecutedCompletion::Part::Tess11,tess,11,live)&&state.returned(WorldNativeExecutedCompletion::Part::Tess11,tess,live,true,true),"later Tess Done completes exact child");
        check(state.returned(WorldNativeExecutedCompletion::Part::Finalize,finalize,live,true,true),"native Finalize original normal return");
        check(state.enter(WorldNativeExecutedCompletion::Part::Level26,level,11,live),"actual Level26 entry accepted");
        check(state.rendererReturned(level,55,live,true),"formal renderer reinit returned within Level26");
        check(state.returned(WorldNativeExecutedCompletion::Part::Level26,level,live,true,true),"actual Level26 Done accepted");
        check(!state.nativeReadyForFrame()&&!state.loadingFinished(11,34,7,true),"foreign client and task returns cannot complete");
        check(state.loadingFinished(11,33,7,true)&&state.nativeReadyForFrame(),"same world/client native loading joins executed receipts before frame join");
        auto permit=state.beginFrame(11,33,7,66,102);
        check(bool(permit)&&!state.frameReady(),"subsequent frame receives current replacement renderer permit");
        check(!state.levelRendered(permit,55,true),"old renderer draw cannot satisfy current frame");
        check(state.levelRendered(permit,66,true),"current renderer native level draw returned");
        check(state.frameReturned(permit,11,33,7,66,true)&&state.frameReady(),"same frame origin normal return completes frame receipt");
        state.requireCurrentFrame(67);
        check(!state.frameReady()&&state.nativeReadyForFrame(),"renderer replacement after a frame requires a fresh draw receipt");
        auto replacement=state.beginFrame(11,33,7,67,105);
        check(bool(replacement)&&state.levelRendered(replacement,67,true)
            &&state.frameReturned(replacement,11,33,7,67,true),"fresh replacement renderer draw restores frame readiness");
    }
    {
        WorldNativeExecutedCompletion state;WorldNativeProducer source{3,2,9,9,17};
        WorldNativeInvocation finalize{source,30,31,0},wrongChild{source,50,51,99};
        check(state.begin(source,11,33,7)&&state.enter(WorldNativeExecutedCompletion::Part::Finalize,finalize,11,live),"second binding begins");
        check(!state.enter(WorldNativeExecutedCompletion::Part::Tess11,wrongChild,11,live),"wrong Finalize child cannot bind Tess");
        check(!state.returned(WorldNativeExecutedCompletion::Part::Finalize,finalize,live,false,false)&&state.sealed(),"native original throw seals binding");
    }
    {
        WorldNativeExecutedCompletion state;WorldNativeProducer source{3,2,9,9,17};
        WorldNativeInvocation finalize{source,30,31,0};
        check(state.begin(source,11,33,7),"third binding begins");
        check(!state.enter(WorldNativeExecutedCompletion::Part::Finalize,finalize,12,live)&&state.sealed(),"same producer with wrong native callable Game seals");
    }
    {
        WorldNativeExecutedCompletion state;WorldNativeProducer source{3,2,9,9,17};
        WorldNativeInvocation level{source,40,41,0};
        check(state.begin(source,11,33,7)&&state.enter(WorldNativeExecutedCompletion::Part::Level26,level,11,live),"foreign reinit fixture actual entry");
        check(!state.rendererReturned(level,55,{11,34,7},true)&&state.sealed(),"other client renderer reinit cannot become owned receipt");
    }
    {
        WorldNativeExecutedCompletion state;WorldNativeProducer source{3,2,9,9,17};
        WorldNativeInvocation level{source,40,41,0};
        check(state.begin(source,11,33,7)&&state.enter(WorldNativeExecutedCompletion::Part::Level26,level,11,live),"world change fixture actual entry");
        check(!state.returned(WorldNativeExecutedCompletion::Part::Level26,level,{11,33,8},true,true)
            &&state.sealed(),"post-entry world generation change cannot record normal completion");
    }
    {
        WorldNativeExecutedCompletion state;WorldNativeProducer source{3,2,9,9,17};
        WorldNativeInvocation finalize{source,30,31,0},level{source,40,41,0},tess{source,50,51,31};
        check(state.begin(source,11,33,7)&&state.enter(WorldNativeExecutedCompletion::Part::Finalize,finalize,11,live)
            &&state.returned(WorldNativeExecutedCompletion::Part::Finalize,finalize,live,true,true),"frame failure fixture Finalize");
        check(state.enter(WorldNativeExecutedCompletion::Part::Level26,level,11,live)
            &&state.rendererReturned(level,55,live,true)
            &&state.returned(WorldNativeExecutedCompletion::Part::Level26,level,live,true,true),"frame failure fixture Level26 and reinit");
        check(state.enter(WorldNativeExecutedCompletion::Part::Tess11,tess,11,live)
            &&state.returned(WorldNativeExecutedCompletion::Part::Tess11,tess,live,true,true)
            &&state.loadingFinished(11,33,7,true),"frame failure fixture Tess and loading");
        auto permit=state.beginFrame(11,33,7,66,103);
        check(bool(permit)&&!state.frameReturned(permit,11,33,7,66,true)&&!state.frameReady()&&!state.sealed(),"frame without native level draw remains pending for a later frame");
        auto next=state.beginFrame(11,33,7,66,104);
        check(bool(next)&&state.levelRendered(next,66,true)&&state.frameReturned(next,11,33,7,66,true),"later actual world draw can complete frame receipt");
    }
    check(callbacks==0&&lifetime.readers()==0,"native callback leases balanced");
    std::printf("WorldNativeTaskReceiptTests PASS (%u checks)\n",checks);
}
