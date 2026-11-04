#include "FlowHarness.h"
#include "ActualInventoryFlow.inc"
#include <iostream>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);}
Context context(std::shared_ptr<ContainerManagerController> const& manager,std::shared_ptr<ContainerScreenController> const& screen){
    Context c;c.manager=manager;c.screen=screen;c.player=&fixture::player;c.view=7;c.snapshot.player=fixture::hand;
    for(int i=0;i<36;++i){c.snapshot.inventory.slots.push_back({"inventory_items",i});c.snapshot.inventory.values.push_back(fixture::hand.slots[i]);}
    c.snapshot.storage.aliases={"container_items"};
    for(int i=0;i<int(fixture::storage.size());++i){c.snapshot.storage.slots.push_back({"container_items",i});c.snapshot.storage.values.push_back(fixture::storage[i]);}
    return c;
}
struct Case {
    std::shared_ptr<ContainerManagerController> manager=std::make_shared<ContainerManagerController>();
    std::shared_ptr<ContainerScreenController> screen=std::make_shared<ContainerScreenController>();
    Context current(){return context(manager,screen);}
};
Case setup(bool transfer=false){
    abortAll();resetRequests();installed=true;runtimeReady=true;++worldGeneration;
    fixture::hand={};fixture::hand.generation=worldGeneration.load();fixture::hand.player=2;fixture::hand.selected=0;
    fixture::hand.slots[9]={"z","z",4,64,false,false};fixture::hand.slots[10]={"a","a",3,64,false,false};fixture::hand.slots[11]={"b","b",2,64,false,false};
    fixture::storage.assign(3,{});fixture::net={};fixture::moves=0;fixture::clock=100;fixture::managerAvailable=true;fixture::failMove=false;fixture::duringNative={};fixture::logs.clear();faulted=false;
    auto config=std::make_shared<Settings>();config->enabled[13]=config->enabled[14]=true;config->inventorySort.mergeStacks=false;publishedSettings=config;
    Case result;auto c=result.current();Job op;op.screen=c.screen;op.view=c.view;op.expected=c.snapshot;op.options=config->inventorySort;op.transferOptions=config->inventoryTransfer;op.started=fixture::clock;op.transfer=transfer;
    if(transfer)op.steps=planTransfer(c.snapshot.inventory.values,c.snapshot.storage.values,9,TransferGesture::All,0,config->inventoryTransfer,config->inventorySort)->steps;
    else op.steps=planSort(c.snapshot.inventory.values,true,config->inventorySort)->steps;
    check(op.steps.size()>1,"fixture has a real multi-step production plan");job=std::make_shared<Job>(std::move(op));return result;
}
void ack(bool success,std::uint64_t generation=worldGeneration.load()){
    auto ids=fixture::net.batch;fixture::net.batch.clear();for(auto id:ids)barrier.response(generation,id,success);
}
}
int main(){try{
    // Complete production tick and request service, not a policy-only model.
    for(bool transfer:{false,true}){
        auto c=setup(transfer);tick(c.current());check(job&&fixture::moves==1,"first native request submitted");
        auto token=job->token;fixture::clock+=80;tick(c.current());check(job&&fixture::moves==1,"latency preserves one native request");
        fixture::hand.slots[20]=fixture::hand.slots[10];tick(c.current());
        check(!job&&faulted&&retired&&owner==token&&barrier.busy(),"hand slot edit immediately cancels plan and retains ACK owner");
        check(!beginRequest(*c.manager),"cancelled native request blocks all new submissions");
        ack(true);tick(c.current());check(!job&&fixture::moves==1,"late accepted ACK cannot resume a cancelled plan");
        auto next=beginRequest(*c.manager);check(next&&next!=token,"complete ACK drain admits a fresh explicit operation");resetRequests();

        c=setup(transfer);tick(c.current());fixture::storage[2]=fixture::hand.slots[11];tick(c.current());
        check(!job&&retired&&fixture::moves==1,"storage slot edit also cancels during ACK wait");ack(false);check(!job&&beginRequest(*c.manager),"late rejection drains without resubmitting");resetRequests();

        c=setup(transfer);tick(c.current());ack(true);fixture::clock+=80;tick(c.current());check(job&&fixture::moves==2,"unchanged accepted snapshot permits the next bounded step");
        auto oldGeneration=worldGeneration.load();auto id=*fixture::net.batch.begin();++worldGeneration;
        tick(c.current());check(!job&&fixture::moves==2,"world generation change cancels follow-up plan");barrier.response(oldGeneration,id,true);resetRequests();

        c=setup(transfer);fixture::duringNative=[](std::int64_t){abortAll();};tick(c.current());
        check(!job&&retired&&owner&&requestResult(owner)==RequestResult::Waiting,"reentrant cancellation retains owner and captures IDs after the call");
        check(!beginRequest(*c.manager),"reentrant cancel cannot open the barrier before ACK");ack(true);check(beginRequest(*c.manager)!=0,"reentrant cancelled request drains safely");resetRequests();

        c=setup(transfer);fixture::duringNative=[](std::int64_t id){barrier.response(worldGeneration.load(),id,false);};tick(c.current());
        auto rejected=job->token;barrier.response(worldGeneration.load(),*fixture::net.batch.begin(),true);tick(c.current());
        check(!job&&faulted&&fixture::moves==1&&requestResult(rejected)==RequestResult::Untracked,"early reject followed by success never executes the next step");resetRequests();
    }
    auto c=setup();abortAll();auto token=beginRequest(*c.manager);check(token!=0,"request owner begins without a job");
    releaseRequest(token);check(owner==token&&retired&&barrier.busy(),"cancel before capture retains token ownership");
    fixture::net.batch={-500000};finishRequest(*c.manager,token);check(requestResult(token)==RequestResult::Waiting,"finish after cancel captures the pending native ID");
    ack(true);check(requestResult(token)==RequestResult::Accepted,"retired token still correlates its ACK");check(beginRequest(*c.manager)!=0,"only a settled retired owner permits begin");resetRequests();fixture::net.batch.clear();

    token=beginRequest(*c.manager);finishRequest(*c.manager,token);releaseRequest(token);
    check(requestResult(token)==RequestResult::Untracked&&!beginRequest(*c.manager),"missing ID is fail-closed after cancellation");
    ++worldGeneration;check(beginRequest(*c.manager)!=0,"world reset releases an untracked old generation");resetRequests();fixture::net.batch.clear();

    token=beginRequest(*c.manager);fixture::net.batch={-500001,-500002};finishRequest(*c.manager,token);releaseRequest(token);fixture::net.batch.clear();
    barrier.response(worldGeneration.load(),-500001,false);check(!beginRequest(*c.manager),"partial rejection retains other unresolved ACKs");
    fixture::clock+=2100;check(!beginRequest(*c.manager),"timeout never clears unresolved ownership");barrier.response(worldGeneration.load(),-500002,true);check(beginRequest(*c.manager)!=0,"last ACK drains a rejected cancelled batch");resetRequests();
    std::cout<<checks<<" extracted production InventoryActions/InventoryRequest flow checks passed; native boundaries doubled, Minecraft NOT_RUN\n";
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
