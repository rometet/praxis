#include "resourcepacks/PassiveReloadTrace.h"
#include "resourcepacks/TaskOutcome.h"
#include "core/NativeHookLifetime.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
ReloadTraceOwner owner(){return {101,202,303,0,true};}
std::vector<ReloadTraceRecord> drain(PassiveReloadTrace& trace){std::vector<ReloadTraceRecord> out;ReloadTraceRecord r;while(trace.next(r))out.push_back(r);return out;}
NativeTaskStamp cycle(PassiveReloadTrace& trace,std::uint64_t serial=10,std::uint64_t update=1){
    trace.title(owner().game,9);
    return trace.start(owner(),serial,update,true,true,10);
}
void identities(){
    NativeTaskStamp unknown{101,202,0,10,{}};
    check(!unknown.valid(),"zero world without explicit session rejected");
    auto title=unknown;title.preGameSession=1;check(title.valid(),"explicit pre-game session valid");
    auto world=unknown;world.world=1;check(world.valid(),"existing world owner unchanged");
    check(!sameTaskCycle(world,title),"world and title are separate owners");
    auto mixed=title;mixed.world=1;check(!mixed.valid(),"mixed title world rejected");
    auto other=title;other.preGameSession=2;check(!sameTaskCycle(title,other),"different session rejected");
    TaskTicket queued{11,22,33,title};
    check(sameTaskCycle(passiveReloadParentStamp(11,&queued,{}),title),"parent queue keeps original title stamp");
    check(sameTaskCycle(passiveReloadParentStamp(0,nullptr,title),title),"synchronous original Start stamp");
    check(!passiveReloadParentStamp(99,nullptr,title).valid(),"foreign executing task cannot borrow Start");
    check(!passiveReloadParentStamp(99,&queued,title).valid(),"wrong executing task rejected");
    check(!passiveReloadParentStamp(0,&queued,title).valid(),"missing current task with parent rejected");
    check(!passiveReloadParentStamp(11,&queued,other).valid(),"conflicting enclosing session rejected");
}
void offAndGuard(){
    for(unsigned mask=0;mask<16;++mask){
        bool request=mask&1,image=mask&2,symbol=mask&4,bytes=mask&8;
        auto guard=reloadTraceGuard(request,image,symbol,bytes);
        check((guard==ReloadTraceGuard::Ready)==(mask==15),"only fully verified requested guard opens");
        auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(guard==ReloadTraceGuard::Ready,0);
        if(mask!=15){
            check(!trace.enabled(),"OFF or unknown build disabled");
            check(!cycle(trace).valid(),"OFF does not observe Start");
            check(!trace.flushBegin(owner(),1,true,true,false,10,44),"OFF does not observe flush");
            trace.event(ReloadTraceEvent::TaskTerminal,{},11,1,1);
            check(trace.view().records==0,"OFF emits no journal records");
        }
    }
    praxis::companion::NativeHookLifetime lifetime;
    check(lifetime.prepare([]{return true;}),"pin prepared");lifetime.registrationStarted();check(lifetime.open(),"lifetime opened");
    {
        praxis::companion::NativeHookLifetime::Lease lease(lifetime);check(lease.admitted(),"reader admitted");
        std::thread closer([&]{lifetime.close();});closer.join();
        check(!lease.admitted()&&lifetime.readers()==1&&lifetime.retained(),"closing retains active hook reader and code");
    }
    check(lifetime.readers()==0&&!lifetime.open(),"stopped hooks cannot re-open");
}
void normalBoundary(){
    auto startupStorage=std::make_unique<PassiveReloadTrace>();auto& startup=*startupStorage;
    startup.enable(true,0);
    check(!startup.start(owner(),8,1,true,true,1).valid(),"zero-world startup without observed title is not a session");
    check(!startup.flushBegin(owner(),1,true,true,false,2,44),"unconfirmed pre-title flush not a manager boundary");
    check(startup.view().session==0&&startup.enabled(),"unknown startup does not poison future title observation");
    check(cycle(startup).valid(),"first confirmed title can observe ordinary reload after unknown startup");
    auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);
    auto update=trace.update(1,0,true,false);check(update==1,"first normal pre-game update");
    trace.title(owner().game,2);check(trace.view().titleSeen,"title seen before reload without manager query");
    auto first=trace.flushBegin(owner(),update,true,true,false,3,44);check(bool(first),"verified native flush entry");
    trace.flushEnd(first,true,4);
    auto second=trace.flushBegin(owner(),update,true,true,false,5,55);check(bool(second),"second registered group");trace.flushEnd(second,true,6);
    auto stamp=cycle(trace,10,update);check(stamp.valid()&&!stamp.world&&stamp.preGameSession==1,"world0 uses explicit session only");
    trace.event(ReloadTraceEvent::FinishEnter,stamp,11);trace.event(ReloadTraceEvent::FinishReturned,stamp,12,0,1);
    trace.event(ReloadTraceEvent::StartReturned,stamp,13,0,1);
    auto records=drain(trace);std::uint64_t flag{},returned{},changed{};unsigned gates{};
    for(auto const& r:records){
        if(r.event==ReloadTraceEvent::FlagGateInferred){flag=r.sequence;++gates;}
        if(r.event==ReloadTraceEvent::FlushReturned)returned=r.sequence;
        if(r.event==ReloadTraceEvent::ChangedStart){changed=r.sequence;check(r.boundary==first.boundary&&(r.value&1),"changed linked to same native update boundary");check(r.titleSeen,"title attribution kept");}
        check(r.session<=1&&r.task==0,"no raw native identity output");
    }
    check(gates==1&&flag<returned&&returned<changed,"flag inference flush return changed ordering");
    check(trace.view().end==ReloadTraceEnd::None,"ordering observation never closes as success");
}
void falseBoundaries(){
    for(unsigned scenario=0;scenario<10;++scenario){
        auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);auto update=trace.update(1,0,true,false);
        trace.title(owner().game,1);
        auto identity=owner();bool caller=true,normal=true,executing=false;std::uintptr_t group=44;
        if(scenario==0)caller=false;if(scenario==1)normal=false;if(scenario==2)executing=true;
        if(scenario==3)identity.preGameKnown=false;if(scenario==4)identity.loads=0;
        if(scenario==5)identity.world=1;if(scenario==6)group=0;
        auto ticket=trace.flushBegin(identity,update,caller,normal,executing,2,group);
        if(scenario<=6)check(!ticket,"unknown context not a boundary");
        if(ticket)trace.flushEnd(ticket,scenario!=7,3);
        if(scenario==8)trace.flushBegin(owner(),update,false,true,false,4,77);
        if(scenario==9)++update;
        auto stamp=cycle(trace,10,update);check(stamp.valid(),"Start can be separately observed without inferred boundary");
        for(auto const& r:drain(trace))if(r.event==ReloadTraceEvent::ChangedStart)
            check(!(r.value&1)&&r.boundary==0,"missing failed foreign stale or reentrant flush never links");
    }
    auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);trace.title(999,1);
    check(!cycle(trace).valid()&&trace.view().end==ReloadTraceEnd::OwnerChanged,"title from another Game cannot establish owner");
    auto changedStorage=std::make_unique<PassiveReloadTrace>();auto& changed=*changedStorage;changed.enable(true,0);check(cycle(changed).valid(),"owner initialized");
    auto other=owner();other.loads=404;
    check(!changed.start(other,11,2,true,true,11).valid()&&changed.view().end==ReloadTraceEnd::OwnerChanged,"different load manager closes instead of re-label");
    auto nestingStorage=std::make_unique<PassiveReloadTrace>();auto& nesting=*nestingStorage;nesting.enable(true,0);nesting.title(owner().game,1);auto a=nesting.flushBegin(owner(),1,true,true,false,1,44);
    check(!nesting.flushBegin(owner(),1,true,true,false,2,55),"nested flush rejected");nesting.flushEnd(a,true,3);cycle(nesting);
    for(auto const& r:drain(nesting))if(r.event==ReloadTraceEvent::ChangedStart)check(!(r.value&1),"nested flush loses boundary correlation");
}
void sameUpdateBoundaryConsumption(){
    // Separate native Starts in one normal update must not borrow a previous
    // flush batch. Vary batch size and IDs; keep delayed old-cycle records.
    for(unsigned scenario=0;scenario<256;++scenario){
        auto storage=std::make_unique<PassiveReloadTrace>();auto& trace=*storage;
        trace.enable(true,0);trace.title(owner().game,1);
        auto update=trace.update(2,0,true,false);
        std::uint64_t firstBoundary{};
        for(unsigned group=0;group<1+scenario%4;++group){
            auto ticket=trace.flushBegin(owner(),update,true,true,false,3,44+group);
            check(bool(ticket),"multi-group first batch admitted");
            if(!firstBoundary)firstBoundary=ticket.boundary;
            check(ticket.boundary==firstBoundary,"one batch keeps one boundary");
            trace.flushEnd(ticket,true,4);
        }
        auto first=trace.start(owner(),1000+scenario*3,update,true,true,5);
        auto second=trace.start(owner(),1001+scenario*3,update,true,true,6);
        check(first.valid()&&second.valid(),"distinct Starts separately recorded in one update");
        auto fresh=trace.flushBegin(owner(),update,true,true,false,7,44);
        check(bool(fresh)&&fresh.boundary!=firstBoundary,"fresh batch gets distinct boundary within same update");
        trace.flushEnd(fresh,true,8);
        auto third=trace.start(owner(),1002+scenario*3,update,true,true,9);
        check(third.valid(),"later same-update Start admitted");
        trace.event(ReloadTraceEvent::TerrainReturned,first,10,77,1);
        unsigned gates{},starts{};std::uint64_t lastGate{},lastReturn{};
        for(auto const& record:drain(trace)){
            if(record.event==ReloadTraceEvent::FlagGateInferred){++gates;lastGate=record.sequence;}
            if(record.event==ReloadTraceEvent::FlushReturned)lastReturn=record.sequence;
            if(record.event==ReloadTraceEvent::ChangedStart){
                ++starts;
                if(record.cycle==first.serial)check((record.value&1)&&record.boundary==firstBoundary,"first Start consumes first batch");
                if(record.cycle==second.serial)check(!(record.value&1)&&!record.boundary,"second Start cannot reuse consumed batch");
                if(record.cycle==third.serial){
                    check((record.value&1)&&record.boundary==fresh.boundary,"third Start uses fresh batch only");
                    check(lastGate<lastReturn&&lastReturn<record.sequence,"fresh gate flush return and changed remain ordered");
                }
            }
            if(record.event==ReloadTraceEvent::TerrainReturned)
                check(record.cycle==first.serial&&record.task==77,"delayed callback retains old cycle after fresh boundary");
        }
        check(gates==2&&starts==3&&trace.view().end==ReloadTraceEnd::None,"two batches never become a success fence");
    }
    auto storage=std::make_unique<PassiveReloadTrace>();auto& trace=*storage;
    trace.enable(true,0);trace.title(owner().game,1);auto update=trace.update(2,0,true,false);
    auto active=trace.flushBegin(owner(),update,true,true,false,3,44);
    auto inside=trace.start(owner(),20,update,true,true,4);
    trace.flushEnd(active,true,5);
    auto after=trace.start(owner(),21,update,true,true,6);
    check(inside.valid()&&after.valid(),"Start during a flush is observed without borrowing its completion");
    for(auto const& record:drain(trace))if(record.event==ReloadTraceEvent::ChangedStart)
        check(!(record.value&1)&&!record.boundary,"reentrant Start also consumes stale boundary after flush return");
}
void callbackFalsePositives(){
    auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);auto older=cycle(trace,10);auto newer=cycle(trace,11,2);
    TaskLineage lineage;lineage.begin(older);auto oldTask=lineage.enqueue(41,51,older);lineage.initialEnqueueReturned(older);
    lineage.begin(newer);auto newTask=lineage.enqueue(42,51,newer);lineage.initialEnqueueReturned(newer);
    auto lookup=lineage.lookup(oldTask.task);check(lookup&&lookup->stamp.serial==10,"pending old task keeps original cycle");
    check(passiveReloadCallbackMatches(oldTask,&*lookup,older.game,0,41,true),"old callback matches its old queue after new Start");
    trace.event(ReloadTraceEvent::TerrainEnter,lookup->stamp,14,lookup->queue);
    trace.event(ReloadTraceEvent::TerrainReturned,lookup->stamp,15,lookup->queue,1);
    for(unsigned i=0;i<4096;++i){
        auto corrupt=oldTask;
        switch(i%10){
        case 0:++corrupt.task;break;case 1:++corrupt.group;break;case 2:++corrupt.queue;break;
        case 3:++corrupt.stamp.game;break;case 4:++corrupt.stamp.manager;break;case 5:++corrupt.stamp.world;break;
        case 6:++corrupt.stamp.serial;break;case 7:++corrupt.stamp.preGameSession;break;
        case 8:++corrupt.stamp.transaction.token;break;case 9:++corrupt.stamp.transaction.operation;break;
        }
        check(!passiveReloadCallbackMatches(oldTask,&corrupt,older.game,0,41,true),"stale identity rejected");
    }
    check(!passiveReloadCallbackMatches(oldTask,&newTask,older.game,0,41,true),"current active cycle cannot certify old callback");
    check(!passiveReloadCallbackMatches(oldTask,nullptr,older.game,0,41,true),"disposed or unknown task rejected");
    check(!passiveReloadCallbackMatches(oldTask,&oldTask,older.game,1,41,true),"world transition rejected");
    check(!passiveReloadCallbackMatches(oldTask,&oldTask,older.game,0,99,true),"foreign execution rejected");
    check(!passiveReloadCallbackMatches(oldTask,&oldTask,older.game,0,41,false),"poisoned observation rejected");
    auto unknown=older;unknown.preGameSession=0;
    trace.event(ReloadTraceEvent::ItemReturned,unknown,16,777,1);
    for(auto const& r:drain(trace)){
        if(r.event==ReloadTraceEvent::TerrainReturned)check(r.cycle==10&&r.task==oldTask.queue,"callback exit remains captured old identity");
        if(r.event==ReloadTraceEvent::CallbackUnknown)check(r.cycle==0&&r.task==0,"unknown callback never borrows latest cycle");
    }
    check(lineage.terminal(oldTask,51,TaskTerminal::Unknown),"cancel or unavailable status remains Unknown");
    auto reused=lineage.enqueue(oldTask.task,51,newer);
    check(reused.queue!=oldTask.queue&&!passiveReloadCallbackMatches(oldTask,&reused,older.game,0,41,true),"pointer reuse cannot certify earlier queue");
    lineage.poison(newer);
    check(lineage.view(newer).poisoned&&!passiveReloadCallbackMatches(newTask,&newTask,newer.game,0,newTask.task,
        !lineage.view(newer).poisoned),"per-cycle poison rejects otherwise matching callback");
}
void outcomes(){
    auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);auto stamp=cycle(trace);
    TaskLineage lineage;lineage.begin(stamp);TaskOutcomeRegistry registry;
    for(unsigned scenario=0;scenario<5;++scenario){
        auto task=100+scenario;auto witness=std::make_shared<TaskBodyWitness>();witness->bind(task);
        if(scenario!=0){witness->enter(task);
            if(scenario==3)witness->exceptionalReturn();else witness->normalReturn(scenario!=2);
        }
        if(scenario!=4)witness->bodyReleased();
        check(registry.factory(task,55,stamp,witness),"factory scoped to explicit pre-game session");
        auto ticket=lineage.enqueue(task,55,stamp);check(registry.queued(ticket,55),"factory joined to exact queue");
        auto body=registry.take(ticket,55);auto result=body->classify(task);
        check(result==(scenario==1?TaskTerminal::Success:scenario==3?TaskTerminal::Error:TaskTerminal::Unknown),"body Done disposed normal return required; no cancel Requeue exception early success");
        check(lineage.terminal(ticket,55,result),"same ticket terminal");
        trace.event(ReloadTraceEvent::TaskTerminal,stamp,20,ticket.queue,static_cast<unsigned>(result));
    }
    auto view=trace.view();check(view.terminals==5&&view.unknownBodies==3&&view.errors==1,"unknown and error coverage stays explicit");
    check(!lineage.view(stamp).succeeded(),"body records do not certify failed cycle");
}
void boundedAndStopped(){
    auto traceStorage=std::make_unique<PassiveReloadTrace>();auto& trace=*traceStorage;trace.enable(true,0);auto stamp=cycle(trace);
    for(unsigned i=0;i<6000;++i)trace.event(ReloadTraceEvent::TaskQueued,stamp,10,i+1);
    auto view=trace.view();check(view.records<=4096&&!trace.enabled()&&view.end==ReloadTraceEnd::RecordLimit&&view.lost,"journal bounded and overflow explicit");
    auto records=drain(trace);check(!records.empty()&&records.back().event==ReloadTraceEvent::End,"end loss marker retained");
    for(std::size_t i=0;i<records.size();++i)check(records[i].sequence==i+1,"bounded journal order");
    auto cyclesStorage=std::make_unique<PassiveReloadTrace>();auto& cycles=*cyclesStorage;cycles.enable(true,0);for(unsigned i=1;i<=4;++i)check(cycle(cycles,i,i).valid(),"bounded native cycles admitted");
    check(!cycle(cycles,5,5).valid()&&cycles.view().end==ReloadTraceEnd::CycleLimit,"fifth cycle cannot overwrite old evidence");
    auto groupsStorage=std::make_unique<PassiveReloadTrace>();auto& groups=*groupsStorage;groups.enable(true,0);groups.title(owner().game,1);for(unsigned i=1;i<=64;++i){auto ticket=groups.flushBegin(owner(),1,true,true,false,1,i);check(bool(ticket),"bounded group id");groups.flushEnd(ticket,true,2);}
    check(!groups.flushBegin(owner(),1,true,true,false,3,65)&&groups.view().end==ReloadTraceEnd::ObservationError,"excess groups stop without native intervention");
    auto deadlineStorage=std::make_unique<PassiveReloadTrace>();auto& deadline=*deadlineStorage;deadline.enable(true,0);check(deadline.update(600000,0,true,false)==0&&deadline.view().end==ReloadTraceEnd::Deadline,"deadline closes trace only");
    auto worldStorage=std::make_unique<PassiveReloadTrace>();auto& world=*worldStorage;world.enable(true,0);check(world.update(1,0,false,false)==0&&world.enabled(),"startup not ready waits");
    check(world.update(2,1,true,false)==0&&world.view().end==ReloadTraceEnd::OwnerChanged,"world generation stops pre-game trace");
    auto inGameStorage=std::make_unique<PassiveReloadTrace>();auto& inGame=*inGameStorage;inGame.enable(true,0);check(inGame.update(1,0,true,true)==0&&!inGame.enabled(),"in-game with unknown generation cannot become pre-game success");
    auto stoppedStorage=std::make_unique<PassiveReloadTrace>();auto& stopped=*stoppedStorage;stopped.enable(true,0);auto original=cycle(stopped);stopped.stop();auto count=stopped.view().records;
    stopped.event(ReloadTraceEvent::TaskTerminal,original,20,1,1);stopped.enable(true,30);
    check(!stopped.enabled()&&stopped.view().records==count,"closed journal cannot record stale terminal or reopen");
    check(std::string_view(reloadTraceLabel(static_cast<ReloadTraceEvent>(999)))=="invalid","labels use fixed bounded vocabulary");
}
}
int main(){identities();offAndGuard();normalBoundary();falseBoundaries();sameUpdateBoundaryConsumption();callbackFalsePositives();outcomes();boundedAndStopped();
    std::cout<<"Passive reload trace checks="<<checks<<" PASS; game=NOT_RUN; native/GPU fence=Unverified\n";
}
