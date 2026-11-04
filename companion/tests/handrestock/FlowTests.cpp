#include "FlowHarness.h"
#include "ActualHandRestockFlow.inc"
#include <iostream>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);}
std::shared_ptr<HudContainerManagerController> setup(bool split=false,std::string name="minecraft:golden_carrot") {
    resetHandRestock();hud.reset();resetRequested=false;insideUse=false;
    fixture::inventory={};fixture::inventory.selected=0;fixture::inventory.generation=1;fixture::inventory.player=2;
    fixture::inventory.slots[0]={"exact-key:"+name,name,1,64,false,false};
    fixture::inventory.slots[9]=fixture::inventory.slots[0];fixture::inventory.slots[9].count=64;
    fixture::clock=100;fixture::enabled=true;fixture::focus=true;fixture::failMove=false;
    fixture::moves=fixture::requestBegins=0;fixture::response=RequestResult::Accepted;fixture::options={};
    fixture::requestFinishes=fixture::requestReleases=0;fixture::requestOwned=fixture::requestRetired=false;fixture::duringNative={};
    fixture::swapEnabled=false;fixture::swapKey='G';fixture::swapModifiers=8;fixture::keys.fill(false);
    fixture::logs.clear();fixture::addresses.clear();
    auto controller=std::make_shared<HudContainerManagerController>();controller->split=split;hud=controller;return controller;
}
void quiet(){fixture::clock+=150;tickHandRestock(fixture::player);tickHandRestock(fixture::player);}
void completedBeforeEmpty(){
    auto op=beforeUse(fixture::player);
    fixture::inventory.slots[0]={};afterUse(fixture::player,op,true);quiet();
}
}
int main(int argc,char**) {
    bool baseline=argc>1;
    try {
        auto c=setup();completedBeforeEmpty();check(fixture::moves==1,"whole36 golden carrot1->empty with spare");
        check(fixture::inventory.slots[0].count==64&&fixture::inventory.slots[9].count==0,"native move conserves reserve count");
        c=setup(true);completedBeforeEmpty();
        check(fixture::moves==(baseline?0u:1u),"split27+9 golden carrot1->empty");
        if(!baseline)check(fixture::addresses==std::vector<std::string>{"inventory_items:0","hotbar_items:0"},"split collection source and target");
        c=setup(true,"minecraft:stone");completedBeforeEmpty();check(fixture::moves==(baseline?0u:1u),"split block placement1->empty");
        c=setup();tickHandRestock(fixture::player);
        auto start=beforeUse(fixture::player);afterUse(fixture::player,start,true);
        fixture::clock=1200;tickHandRestock(fixture::player); // successful food start has no immediate decrement
        fixture::clock=1690;tickHandRestock(fixture::player);
        fixture::clock=1700;fixture::inventory.slots[0]={};
#ifdef HAS_COMPLETION_CAPTURE
        auto complete=beforeUse(fixture::player,HandSlot::Mainhand,true);
#else
        auto complete=beforeUse(fixture::player);
#endif
        afterUse(fixture::player,complete,true);quiet();
        check(fixture::moves==(baseline?0u:1u),"food completion observes last carrot after native slot cleared");
        c=setup();auto equip=beforeUse(fixture::player);fixture::inventory.selected=1;fixture::inventory.slots[0]={};afterUse(fixture::player,equip,true);quiet();check(!fixture::moves,"equip cancels");
        c=setup();auto exit=beforeUse(fixture::player);fixture::inventory.slots[0]={};afterUse(fixture::player,exit,true);fixture::inventory.generation=2;quiet();check(!fixture::moves,"world exit cancels");
        c=setup();auto drop=beforeUse(fixture::player);fixture::inventory.slots[0]={};afterUse(fixture::player,drop,true);resetHandRestock();quiet();check(!fixture::moves,"drop cancellation discards pending");
        c=setup();auto gui=beforeUse(fixture::player);fixture::inventory.slots[0]={};afterUse(fixture::player,gui,true);fixture::focus=false;quiet();check(!fixture::moves,"GUI/out of game focus cancels");
        c=setup();auto changed=beforeUse(fixture::player);fixture::inventory.slots[0]={};afterUse(fixture::player,changed,true);fixture::inventory.slots[9].count=63;quiet();check(!fixture::moves,"reserve conflict cancels");
        c=setup();fixture::failMove=true;completedBeforeEmpty();check(fixture::moves==1&&fixture::inventory.slots[0].count==0&&fixture::inventory.slots[9].count==64,"native failed move leaves items unchanged");
        c=setup();fixture::response=RequestResult::Waiting;completedBeforeEmpty();check(fixture::moves==1&&pending,"latency waits without duplicate submission");fixture::clock+=100;tickHandRestock(fixture::player);check(fixture::moves==1,"no duplicate while waiting");fixture::response=RequestResult::Accepted;tickHandRestock(fixture::player);check(!pending&&!faulted,"accepted native request finishes");
        c=setup();fixture::response=RequestResult::Rejected;completedBeforeEmpty();check(!pending&&faulted,"server rejection stops automation");
        c=setup();tickHandRestock(fixture::player);invokeActualDrop(fixture::player);fixture::inventory.slots[0]={};
#ifdef HAS_COMPLETION_CAPTURE
        auto dropped=beforeUse(fixture::player,HandSlot::Mainhand,true);
#else
        auto dropped=beforeUse(fixture::player);
#endif
        afterUse(fixture::player,dropped,true);quiet();check(!fixture::moves,"actual drop callback clears the remembered identity");
        c=setup();tickHandRestock(fixture::player);fixture::inventory.slots[0]={};quiet();check(!fixture::moves,"empty observation alone never triggers refill");
        c=setup();fixture::inventory.slots[9]={};completedBeforeEmpty();check(!fixture::moves,"no spare is not fabricated");
        c=setup();fixture::inventory.slots[9]={};fixture::inventory.slots[1]=fixture::inventory.slots[0];fixture::inventory.slots[1].count=12;
        completedBeforeEmpty();check(!fixture::moves,"other hotbar excluded by default");
        c=setup();fixture::inventory.slots[9].key+="different-compound-tag";completedBeforeEmpty();check(!fixture::moves,"different native NBT spare rejected");
        c=setup();auto unsuccessful=beforeUse(fixture::player);fixture::inventory.slots[0]={};afterUse(fixture::player,unsuccessful,false);quiet();check(!fixture::moves,"unsuccessful use does not refill");
        c=setup();check(!beforeUse(fixture::player,HandSlot::Offhand),"offhand remains excluded");
        if(!baseline){
            for(bool split:{false,true}){
                c=setup(split,"minecraft:arrow");fixture::options.offhand=true;
                fixture::inventory.slots[36]=fixture::inventory.slots[0];fixture::inventory.slots[0]={};
                auto off=beforeUse(fixture::player,HandSlot::Offhand);check(bool(off),"explicit offhand use is captured when enabled");
                fixture::inventory.slots[36]={};afterUse(fixture::player,off,true);quiet();
                check(fixture::moves==1&&fixture::inventory.slots[36].count==64,"actual production offhand refill uses the native collection");
                check(fixture::addresses.back()=="offhand_items:0","offhand refill target is the validated native slot");
            }
            c=setup();fixture::options.excludedInventory[9]=true;completedBeforeEmpty();check(!fixture::moves,"excluded spare never submits");
            c=setup();fixture::options.excludedItems[0]="minecraft:golden_carrot";completedBeforeEmpty();check(!fixture::moves,"excluded item never submits");
            c=setup();fixture::options.offhand=true;c->offhand=false;fixture::inventory.slots[36]=fixture::inventory.slots[0];fixture::inventory.slots[0]={};
            auto missing=beforeUse(fixture::player,HandSlot::Offhand);fixture::inventory.slots[36]={};afterUse(fixture::player,missing,true);quiet();check(!fixture::requestBegins&&!fixture::moves,"unavailable native offhand collection fails before request admission");
            c=setup();fixture::enabled=false;fixture::swapEnabled=true;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);
            check(fixture::moves==1&&fixture::inventory.slots[36].count==1&&fixture::inventory.slots[0].count==0,"explicit action works with automatic restock disabled");
            tickHandRestock(fixture::player);tickHandRestock(fixture::player);check(fixture::moves==1,"held action does not repeat");
            fixture::keys['G']=false;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);tickHandRestock(fixture::player);check(fixture::moves==2&&fixture::inventory.slots[0].count==1,"a second fresh press swaps back");
            c=setup();fixture::swapEnabled=true;fixture::keys['G']=true;tickHandRestock(fixture::player);check(!fixture::moves,"entering with an already held key does not swap");
            c=setup();fixture::swapEnabled=true;fixture::swapModifiers=2;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);check(!fixture::moves,"wrong modifier blocks swap");
            fixture::keys['G']=false;tickHandRestock(fixture::player);fixture::keys[VK_SHIFT]=fixture::keys['G']=true;tickHandRestock(fixture::player);check(fixture::moves==1,"Shift2 chord swaps once");
            c=setup();fixture::swapEnabled=true;fixture::response=RequestResult::Waiting;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);
            fixture::keys['G']=false;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);check(fixture::moves==1&&pending,"fresh press while awaiting ACK cannot resubmit");
            fixture::inventory=pending->before;fixture::response=RequestResult::Rejected;tickHandRestock(fixture::player);check(faulted&&!pending&&fixture::moves==1,"native rollback/rejection retires the explicit job");
            c=setup();fixture::swapEnabled=true;fixture::focus=false;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);fixture::focus=true;tickHandRestock(fixture::player);check(!fixture::moves,"focus interruption cannot convert held key into an action");
            c=setup();tickHandRestock(fixture::player);fixture::focus=false;serviceHandRestock();fixture::focus=true;fixture::inventory.slots[0]={};
            auto interrupted=beforeUse(fixture::player,HandSlot::Mainhand,true);afterUse(fixture::player,interrupted,true);quiet();check(!fixture::moves,"GUI watchdog clears the remembered identity even without a HUD frame");
            c=setup();fixture::response=RequestResult::Waiting;completedBeforeEmpty();fixture::response=RequestResult::TimedOut;serviceHandRestock();check(faulted&&!pending&&fixture::moves==1,"ACK timeout watchdog retires automation without a UI callback");
            c=setup();fixture::response=RequestResult::Waiting;completedBeforeEmpty();fixture::inventory.slots[12]=fixture::inventory.slots[0];
            tickHandRestock(fixture::player);check(!pending&&operationStatus==6&&fixture::moves==1,"slot change cancels immediately before ACK");
            check(fixture::requestOwned&&fixture::requestRetired&&fixture::requestReleases==1,"cancel retains the native request lease");
            fixture::response=RequestResult::Accepted;tickHandRestock(fixture::player);check(!pending&&operationStatus==6&&fixture::moves==1,"late success cannot resurrect a cancelled refill");
            c=setup();fixture::response=RequestResult::Waiting;completedBeforeEmpty();fixture::inventory.selected=1;
            serviceHandRestock();check(!pending&&operationStatus==6&&fixture::requestOwned,"normalTick sees a selected-slot change while awaiting ACK");
            c=setup();fixture::enabled=false;fixture::swapEnabled=true;fixture::response=RequestResult::Waiting;
            tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);fixture::keys['G']=false;tickHandRestock(fixture::player);
            fixture::inventory.slots[14]=fixture::inventory.slots[36];fixture::keys['G']=true;tickHandRestock(fixture::player);
            check(!pending&&fixture::moves==1&&operationStatus==6,"fresh swap key cannot skip pending slot-change cancellation");
            fixture::keys['G']=false;tickHandRestock(fixture::player);fixture::keys['G']=true;tickHandRestock(fixture::player);
            check(fixture::moves==1&&fixture::requestOwned,"explicit new job stays blocked until the cancelled ACK drains");
            c=setup();fixture::response=RequestResult::Waiting;fixture::duringNative=[] {cancel();};completedBeforeEmpty();
            check(!pending&&fixture::requestFinishes==1&&fixture::requestOwned&&fixture::requestRetired,"reentrant cancel still finishes native ID capture");
#ifdef HAS_COMPLETION_CAPTURE
            c=setup();tickHandRestock(fixture::player);fixture::inventory.slots[0]={};fixture::clock+=251;
            auto stale=beforeUse(fixture::player,HandSlot::Mainhand,true);afterUse(fixture::player,stale,true);quiet();check(!fixture::moves,"stale remembered identity rejected");
            c=setup();tickHandRestock(fixture::player);fixture::inventory.slots[0]={};fixture::inventory.player=3;
            auto otherPlayer=beforeUse(fixture::player,HandSlot::Mainhand,true);afterUse(fixture::player,otherPlayer,true);quiet();check(!fixture::moves,"player identity change rejected");
            c=setup();tickHandRestock(fixture::player);fixture::focus=false;tickHandRestock(fixture::player);fixture::focus=true;fixture::inventory.slots[0]={};
            auto closed=beforeUse(fixture::player,HandSlot::Mainhand,true);afterUse(fixture::player,closed,true);quiet();check(!fixture::moves,"GUI interruption clears remembered identity");
#endif
            // The physical polling action must never reuse a camera movement edge.
            for(int key:cameraMovementKeys)for(unsigned mods:{0u,1u,2u,4u,7u,8u}){
                c=setup();fixture::enabled=false;fixture::swapEnabled=true;fixture::swapKey=key;fixture::swapModifiers=mods;
                tickHandRestock(fixture::player);fixture::keys[key]=true;
                fixture::keys[VK_CONTROL]=bool(mods&1);fixture::keys[VK_SHIFT]=bool(mods&2)||key==VK_SHIFT;fixture::keys[VK_MENU]=bool(mods&4);
                tickHandRestock(fixture::player);check(!fixture::requestBegins&&!fixture::moves,"all eight camera movement keys reject offhand request admission");
                tickHandRestock(fixture::player);check(!fixture::requestBegins&&!fixture::moves,"held camera movement cannot execute a swap");
                fixture::keys={};fixture::swapKey='G';fixture::swapModifiers=8;tickHandRestock(fixture::player);
                fixture::keys['G']=true;tickHandRestock(fixture::player);
                check(fixture::moves==1,"legal fresh offhand action remains available after movement-key rejection");
            }
            for(bool split:{false,true})for(int selected=0;selected<9;++selected)for(int source=9;source<36;++source){
                c=setup(split);auto value=fixture::inventory.slots[0];fixture::inventory.slots.fill({});fixture::inventory.selected=selected;
                fixture::inventory.slots[selected]=value;value.count=16;fixture::inventory.slots[source]=value;
                auto op=beforeUse(fixture::player);fixture::inventory.slots[selected]={};afterUse(fixture::player,op,true);quiet();
                check(fixture::moves==1&&fixture::inventory.slots[selected].count==16&&fixture::inventory.slots[source].count==0,"all selected/main-inventory collection addresses preserve item total");
            }
        }
        std::cout<<checks<<" extracted production HandRestock flow checks passed; "<<(baseline?"baseline split/late-food defects reproduced":"corrected capture/collection behavior")<<"; native boundaries doubled, Minecraft NOT_RUN\n";
    }catch(std::exception const& error){std::cerr<<error.what()<<"\n";return 1;}
}
