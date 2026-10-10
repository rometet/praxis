#include "core/InventoryActionPolicy.h"
#include "core/InventoryRequestPolicy.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
 unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("inventory extension contract");};
 auto stack=[](std::string name,int count,int id=0,int category=0){return StackValue{name,name,count,64,false,false,id,category};};
 try {
  InventorySortOptions sort;sort.mergeStacks=false;
  std::vector<StackValue> input{stack("z",2,3,1),stack("a",7,8,3),stack("b",4,1,2)};
  sort.sortMethod=1;auto plan=planSort(input,false,sort);check(plan&&plan->result[0].count==7);
  sort.sortMethod=2;plan=planSort(input,false,sort);check(plan&&plan->result[0].id==1);
  sort.sortMethod=3;plan=planSort(input,false,sort);check(plan&&plan->result[0].category==1);
  input[0].rarity=3;input[1].rarity=1;sort.sortMethod=4;plan=planSort(input,false,sort);check(plan&&plan->result[0].rarity==3);
  sort.sortMethod=0;sort.bundleLast=sort.shulkerLast=true;
  plan=planSort({stack("minecraft:blue_bundle",4),stack("minecraft:red_shulker_box",1),stack("minecraft:zombie",2),{}},false,sort);
  check(plan&&plan->result[0].name=="minecraft:zombie"&&plan->result.back().count==0);
  sort.maxSteps=1;check(!planSort({stack("d",1),stack("c",1),stack("b",1),stack("a",1)},false,sort));sort={};
  std::vector<StackValue> player(36),storage(27);player[9]=stack("stone",4);player[10]=stack("stone",7);player[11]=stack("other",3);
  player[10].key="stone:ench";InventoryTransferOptions o;o.leaveOne=true;
  auto bulk=planTransfer(player,storage,9,TransferGesture::SameType,0,o,sort);
  check(bulk&&bulk->result[9].count==1&&bulk->result[10].count==1&&bulk->result[11].count==3);
  check(bulk->result[36].key=="stone"&&bulk->result[37].key=="stone:ench");
  bulk=planTransfer(player,storage,9,TransferGesture::All,0,o,sort);check(bulk&&bulk->result[11].count==1);
  o.maxSteps=1;bulk=planTransfer(player,storage,9,TransferGesture::All,0,o,sort);check(bulk&&bulk->steps.size()==1&&bulk->result[10].count==7);
  // Single/stack gestures retain their existing behavior with the bulk-only reserve enabled.
  bulk=planTransfer(player,storage,9,TransferGesture::DragStack,0,o,sort);check(bulk&&bulk->result[9].count==0);
  std::mt19937 rng(6521);
  for(int test=0;test<300;++test){
   player.assign(36,{});storage.assign(1+rng()%54,{});sort={};o={};o.leaveOne=rng()%2;o.maxSteps=1+rng()%32;
   for(int i=0;i<36;++i){if(rng()%3)player[i]=stack(std::to_string(rng()%4),1+rng()%64);sort.excludedInventory[i]=rng()%9==0;}
   for(int i=0;i<int(storage.size());++i){if(rng()%3)storage[i]=stack(std::to_string(rng()%4),1+rng()%64);storage[i].locked=rng()%9==0;sort.excludedStorage[i]=rng()%9==0;}
   auto original=player;original.insert(original.end(),storage.begin(),storage.end());
   for(auto gesture:{TransferGesture::SameType,TransferGesture::All}){
    auto p=planTransfer(player,storage,rng()%original.size(),gesture,0,o,sort);if(!p)continue;
    check(p->steps.size()<=std::size_t(o.maxSteps));auto state=original;
    for(auto step:p->steps){check(applyMove(state,step));check(totals(state)==totals(original));if(o.leaveOne)check(state[step.source].count>=1);}
    check(state==p->result);
    for(int i=0;i<int(state.size());++i)if(i<9||original[i].locked||(i<36?sort.excludedInventory[i]:sort.excludedStorage[i-36]))check(state[i]==original[i]);
   }
  }
  Settings s;s.inventorySort.actionKey='R';s.inventoryTransfer.sameKey='R';s.inventoryTransfer.sameModifiers=1;s.inventorySort.actionModifiers=2;
  check(!inventoryActionConflict(s,InventoryAction::Sort));s.inventorySort.actionModifiers=8;check(inventoryActionConflict(s,InventoryAction::Sort));
  InventoryActionEdge edge;HotkeyChord chord{'R',2,4};check(!edge.sample(chord,true,2,true));check(!edge.sample(chord,false,2,true));check(edge.sample(chord,true,2,true));
  check(!edge.sample(chord,true,2,true));edge.sample(chord,false,2,false);check(!edge.sample(chord,true,2,false));check(!edge.sample(chord,true,2,true));
  RequestBarrier barrier;barrier.begin(1,10);barrier.capture({-1,-2});check(!barrier.settled(1));barrier.response(1,-1,false);
  check(barrier.result(1,30)==RequestResult::Rejected&&!barrier.settled(1));check(barrier.result(1,2500)==RequestResult::Rejected&&!barrier.settled(1));
  barrier.response(1,-2,true);check(barrier.settled(1));barrier.reset();barrier.begin(1,10);barrier.capture({-3});check(barrier.result(1,2010)==RequestResult::TimedOut&&!barrier.settled(1));check(barrier.settled(2));
  barrier.reset();barrier.begin(1,10);check(barrier.result(1,2010)==RequestResult::TimedOut&&!barrier.settled(1));
  // Every ordering of early/late, success/rejection and duplicates must keep
  // the matching rejection terminal. Unrelated early IDs do not poison a job.
  for(int captureAt=0;captureAt<=4;++captureAt)for(int rejectionAt=0;rejectionAt<4;++rejectionAt){
   RequestBarrier b;check(b.begin(11,10));b.response(11,-999,false);
   for(int i=0;i<4;++i){if(i==captureAt)b.capture({-71,-71});b.response(11,-71,i!=rejectionAt);}
   if(captureAt==4)b.capture({-71,-71});
   b.capture({-72});b.response(11,-71,true);
   check(b.result(11,20)==RequestResult::Rejected&&b.settled(11));
  }
  RequestBarrier epochs;check(epochs.begin(1,10));epochs.response(2,-9,false);epochs.capture({-9});
  epochs.response(2,-9,true);check(epochs.result(1,20)==RequestResult::Waiting);
  epochs.response(1,-9,true);check(epochs.result(1,20)==RequestResult::Accepted);
  epochs.reset();check(epochs.begin(1,30));epochs.capture({-9});epochs.response(1,-9,true);
  check(epochs.result(1,40)==RequestResult::Untracked&&!epochs.settled(1));
  epochs.reset();check(epochs.begin(2,50));epochs.capture({-9});epochs.response(1,-9,true);epochs.response(2,-9,true);
  check(epochs.result(2,60)==RequestResult::Untracked&&!epochs.settled(2)); // No packet world epoch: reused IDs are ambiguous.
  epochs.reset();check(epochs.begin(3,70));epochs.response(2,-10,false);epochs.capture({-10});
  epochs.response(2,-10,true);check(epochs.result(3,80)==RequestResult::Waiting);epochs.response(3,-10,true);check(epochs.result(3,80)==RequestResult::Accepted);
  RequestBarrier overflow;check(overflow.begin(1,10));for(int i=0;i<129;++i)overflow.response(1,i,true);
  overflow.capture({128});check(overflow.result(1,20)==RequestResult::Untracked&&!overflow.settled(1));
  RequestBarrier capacity;for(int i=0;i<65536;++i){check(capacity.begin(1,10));capacity.capture({i});capacity.response(1,i,true);check(capacity.result(1,20)==RequestResult::Accepted);capacity.reset();}
  check(!capacity.begin(2,30));
  RuntimeConfig c;c.inventoryTransferOptions.maxSteps=0;check(!validRuntimeConfig(c));
  std::cout<<checks<<" inventory extension checks passed\n";
 }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
