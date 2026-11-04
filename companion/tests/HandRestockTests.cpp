#include "core/RuntimePolicy.h"
#include "core/InventoryRequestPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
    unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("HandRestock contract");};
    try{
        HandRestockOptions o;HandSnapshot before;before.selected=0;before.generation=1;before.player=2;
        before.slots[0]={"stone","minecraft:stone",7,64,false,false};
        before.slots[9]={"stone","minecraft:stone",32,64,false,false};
        before.slots[35]={"stone","minecraft:stone",64,64,false,false};
        auto after=before;after.slots[0].count=6;
        auto p=planRestock(before,after,o);check(p&&p->source==35&&p->amount==58&&!p->swap);
        check(!planRestock(before,before,o));
        after.slots[0].count=5;check(!planRestock(before,after,o));
        auto latest=before;latest.slots[0].count=6;auto finalUse=latest;finalUse.slots[0].count=5;
        auto continued=planRestock(latest,finalUse,o);check(continued&&continued->amount==59&&continued->source==35);
        latest.slots[0].count=1;finalUse=latest;finalUse.slots[0]={};
        continued=planRestock(latest,finalUse,o);check(continued&&continued->amount==64);
        after=before;after.slots[0].count=6;after.slots[10]={"x","x",1,1,false,false};check(!planRestock(before,after,o));
        after=before;after.slots[0].count=6;after.slots[36]={"x","x",1,1,false,false};check(!planRestock(before,after,o));
        after=before;after.slots[0].count=6;after.selected=1;check(!planRestock(before,after,o));
        after=before;after.slots[0].count=6;after.generation=2;check(!planRestock(before,after,o));
        for(int maximum:{1,16,64}){
            auto a=before;a.slots[0].maximum=maximum;a.slots[0].count=1;auto b=a;b.slots[0]={};
            auto step=planRestock(a,b,o);check(step&&step->amount==maximum);
        }
        auto a=before;a.slots[0].count=1;a.slots[0].key="water";a.slots[0].name="minecraft:water_bucket";a.slots[0].maximum=1;
        a.slots[9]=a.slots[0];a.slots[35]={};auto b=a;b.slots[0]={"bucket","minecraft:bucket",1,16,false,false};
        p=planRestock(a,b,o);check(p&&p->swap&&p->source==9);
        o.replaceRemainders=false;check(!planRestock(a,b,o));o.replaceRemainders=true;
        a=before;a.slots[9]={};a.slots[35]={};a.slots[1]=before.slots[9];b=a;b.slots[0].count=6;
        check(!planRestock(a,b,o));o.fromHotbar=true;check(planRestock(a,b,o)->source==1);
        a.slots[1].locked=true;b=a;b.slots[0].count=6;check(!planRestock(a,b,o));
        a=before;a.slots[0].damageable=true;b=a;b.slots[0].count=6;check(!planRestock(a,b,o));
        a=before;a.slots[36]=a.slots[0];b=a;b.slots[36].count=6;o={};
        check(!planRestock(a,b,o,36));o.offhand=true;p=planRestock(a,b,o,36);check(p&&p->target==36&&p->source==35);
        o.excludedInventory[35]=true;p=planRestock(a,b,o,36);check(p&&p->source==9);o.excludedInventory[9]=true;check(!planRestock(a,b,o,36));
        o={};o.offhand=true;o.excludedItems[0]="minecraft:stone";check(!planRestock(a,b,o,36));
        o={};o.excludedInventory[0]=true;check(!planRestock(before,after,o));
        auto swap=planOffhandSwap(before);check(swap&&swap->source==0&&swap->target==36&&swap->swap);
        a=before;a.slots[0].locked=true;check(!planOffhandSwap(a));a=before;a.slots[36].locked=true;check(!planOffhandSwap(a));
        a=before;a.slots[0]=a.slots[36]={};check(!planOffhandSwap(a));
        RequestBarrier barrier;check(!barrier.begin(0,100));check(barrier.begin(1,100));check(!barrier.begin(1,101));
        barrier.capture({-1,-2});barrier.response(1,-99,true);check(barrier.result(1,120)==RequestResult::Waiting);
        barrier.response(1,-2,true);check(barrier.result(1,121)==RequestResult::Waiting);barrier.response(1,-1,true);check(barrier.result(1,122)==RequestResult::Accepted);
        barrier.response(1,-1,false);check(barrier.result(1,123)==RequestResult::Rejected);
        barrier.reset();barrier.begin(1,100);barrier.response(1,-3,false);barrier.response(1,-3,true);barrier.capture({-3});check(barrier.result(1,110)==RequestResult::Rejected);
        barrier.reset();check(barrier.begin(1,100));barrier.response(1,-5,true);barrier.capture({-5});check(barrier.result(1,110)==RequestResult::Accepted);
        barrier.reset();barrier.begin(1,100);barrier.capture({-4});check(barrier.result(1,2100)==RequestResult::TimedOut);check(barrier.result(2,150)==RequestResult::Rejected);
        barrier.reset();barrier.begin(1,100);barrier.capture({});check(barrier.result(1,150)==RequestResult::Untracked);
        RuntimeConfig c;check(validRuntimeConfig(c)&&!c.handRestock&&c.keybinds[12]==0);
        std::cout<<checks<<" HandRestock/request checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what();return 1;}
}
