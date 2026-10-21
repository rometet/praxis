#include "resourcepacks/TaskLineage.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace praxis::companion::packs;
static unsigned checks{}, failures{};
void check(bool value){++checks;if(!value)++failures;}
NativeTaskStamp stamp(std::uint64_t serial){return {11,22,33,serial,{44,11,22,55,33,serial,serial}};}
int main(){
    {
        TaskLineage graph;auto s=stamp(1);graph.begin(s);
        auto worker=graph.enqueue(101,201,s);auto callback=graph.enqueue(102,201,s);
        check(!graph.view(s).succeeded());graph.initialEnqueueReturned(s);
        check(graph.terminal(worker,201,TaskTerminal::Success));check(!graph.view(s).succeeded());
        // Main callback queues Tessellator after the initial enqueue returned.
        auto parent=graph.lookup(102);check(parent&&sameTaskCycle(parent->stamp,s));
        auto tessellator=graph.enqueue(103,202,parent->stamp);
        check(graph.terminal(callback,201,TaskTerminal::Success));check(!graph.view(s).succeeded());
        check(graph.terminal(tessellator,202,TaskTerminal::Success));check(graph.view(s).succeeded());
    }
    for(auto terminal:{TaskTerminal::Unknown,TaskTerminal::Cancelled,TaskTerminal::Error}){
        TaskLineage graph;auto s=stamp(1);graph.begin(s);auto task=graph.enqueue(101,201,s);
        graph.initialEnqueueReturned(s);check(graph.terminal(task,201,terminal));check(!graph.view(s).succeeded());
        auto v=graph.view(s);check(v.pending==0&&v.unknown+v.cancelled+v.errors==1);
    }
    {
        TaskLineage graph;auto old=stamp(1),next=stamp(2);graph.begin(old);
        auto delayed=graph.enqueue(101,201,old);graph.initialEnqueueReturned(old);graph.begin(next);
        auto fresh=graph.enqueue(102,201,next);graph.initialEnqueueReturned(next);
        auto actual=graph.lookup(101);check(actual&&sameTaskCycle(actual->stamp,old));
        check(actual&&!callbackBindingMatches(actual->stamp.transaction,next.transaction));
        check(graph.terminal(delayed,201,TaskTerminal::Success));check(graph.view(next).pending==1);
        check(!graph.view(next).succeeded());check(graph.terminal(fresh,201,TaskTerminal::Success));
        check(graph.view(next).succeeded());
    }
    {
        // Address reuse cannot let a duplicate completion retire a new task.
        TaskLineage graph;auto old=stamp(1),next=stamp(2);graph.begin(old);
        auto prior=graph.enqueue(101,201,old);graph.initialEnqueueReturned(old);
        check(graph.terminal(prior,201,TaskTerminal::Success));graph.begin(next);
        auto live=graph.enqueue(101,201,next);graph.initialEnqueueReturned(next);
        check(!graph.terminal(prior,201,TaskTerminal::Success));check(graph.view(next).pending==1);
        check(graph.terminal(live,201,TaskTerminal::Success));
    }
    for(unsigned field=0;field<4;++field){
        TaskLineage graph;auto s=stamp(1);graph.begin(s);auto task=graph.enqueue(101,201,s);
        graph.initialEnqueueReturned(s);auto wrong=task;
        if(field==0)wrong.stamp.world++;if(field==1)wrong.stamp.game++;
        if(field==2)wrong.stamp.manager++;if(field==3)wrong.queue++;
        check(!graph.terminal(wrong,201,TaskTerminal::Success));check(graph.view(s).pending==1);
        check(graph.view(s).poisoned);check(!graph.view(s).succeeded());
    }
    {
        TaskLineage graph;auto s=stamp(1);graph.begin(s);auto task=graph.enqueue(101,201,s);
        graph.initialEnqueueReturned(s);check(!graph.terminal(task,202,TaskTerminal::Success));
        check(graph.view(s).pending==1&&!graph.view(s).succeeded());
        check(!graph.enqueue(101,201,s).queue);check(graph.view(s).poisoned);
    }
    {
        TaskLineage graph;auto s=stamp(1);graph.begin(s);
        for(unsigned i=0;i<4096;++i)check(graph.enqueue(1000+i,201,s).queue!=0);
        check(!graph.enqueue(99999,201,s).queue);check(graph.view(s).poisoned);
        check(graph.view(s).pending==4096);check(!graph.view(s).succeeded());
    }
    {
        TaskLineage graph;auto s=stamp(1);graph.begin(s);graph.initialEnqueueReturned(s);
        std::atomic<unsigned> errors{};std::vector<std::thread> threads;
        for(unsigned worker=0;worker<4;++worker)threads.emplace_back([&,worker]{
            for(unsigned i=0;i<500;++i){
                auto id=1000+worker*1000+i;auto task=graph.enqueue(id,201,s);auto looked=graph.lookup(id);
                if(!task.queue||!looked||looked->queue!=task.queue||!graph.terminal(task,201,TaskTerminal::Success))++errors;
            }
        });
        for(auto& thread:threads)thread.join();check(errors.load()==0);check(graph.view(s).queued==2000);
        check(graph.view(s).pending==0);check(graph.view(s).succeeded());
    }
    // Shipping proof remains unavailable even if a synthetic CPU graph succeeds.
    check(SupportedNativeSerialization==NativeSerialization::Unverified);
    std::cout<<"Task lineage: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
