#include "core/RuntimePolicy.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
    unsigned checks{};auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("InventoryTransfer contract");};
    auto stack=[](std::string name,int count=1,int maximum=64){return StackValue{name,name,count,maximum,false,false};};
    try {
        check(dragGesture(1,false,true)==TransferGesture::DragOne);
        check(dragGesture(1,true,false)==TransferGesture::DragStack);
        check(dragGesture(1,true,true)==TransferGesture::DragOne);
        check(dragGesture(2,true,true)==TransferGesture::None&&dragGesture(1,false,false)==TransferGesture::None);
        TransferStroke stroke;check(!stroke.press(1,false,TransferGesture::DragOne));check(!stroke.release(1));
        check(stroke.press(1,true,TransferGesture::DragOne));check(stroke.visit(9));check(!stroke.visit(9));check(stroke.visit(10));
        stroke.cancel();check(!stroke.visit(11));check(stroke.release(1));check(!stroke.release(1));
        check(stroke.press(1,true,TransferGesture::DragStack));check(!stroke.press(1,false,TransferGesture::None));check(!stroke.release(1));
        std::vector<StackValue> player(36),storage(27);player[9]=stack("stone",64);storage[0]=stack("stone",32);
        InventoryTransferOptions o;InventorySortOptions excludes;
        auto run=[&](int hover,TransferGesture gesture,int direction){return planTransfer(player,storage,hover,gesture,direction,o,excludes);};
        auto p=run(9,TransferGesture::WheelOne,1);check(p&&p->steps.size()==1&&p->steps[0]==MoveStep{9,36,1,false});
        p=run(9,TransferGesture::WheelStack,1);check(p&&p->steps.size()==2&&p->result[9].count==0&&p->result[36].count==64&&p->result[37].count==32);
        p=run(36,TransferGesture::WheelOne,1);check(p&&p->steps[0]==MoveStep{9,36,1,false});
        p=run(36,TransferGesture::WheelOne,-1);check(p&&p->steps[0]==MoveStep{36,10,1,false});
        p=run(9,TransferGesture::WheelOne,-1);check(!p); // hovered destination is already full
        p=run(9,TransferGesture::DragStack,0);check(p&&p->result[9].count==0);
        o.reverseWheel=true;p=run(36,TransferGesture::WheelOne,1);check(p&&p->steps[0].source==36);o.reverseWheel=false;
        o.wheelOne=false;check(!run(9,TransferGesture::WheelOne,1));o.wheelOne=true;
        player[0]=stack("stone");check(!run(0,TransferGesture::DragOne,0));o.preserveHotbar=false;check(run(0,TransferGesture::DragOne,0).has_value());o.preserveHotbar=true;
        excludes.excludedInventory[9]=true;check(!run(9,TransferGesture::DragStack,0));excludes.excludedInventory[9]=false;
        storage[0].locked=true;p=run(9,TransferGesture::DragOne,0);check(p&&p->steps[0].target==37);storage[0].locked=false;
        player[35]=stack("stone",3);p=run(9,TransferGesture::WheelStack,1);check(p&&p->steps[0].source==35);
        player[35]=stack("stone:enchantment",3);p=run(9,TransferGesture::WheelStack,1);check(p&&p->steps[0].source==9);
        check(!run(-1,TransferGesture::WheelOne,1));check(!run(90,TransferGesture::WheelOne,1));check(!run(9,TransferGesture::WheelOne,0));
        for(auto& s:storage)s=stack("unrelated",64);check(!run(9,TransferGesture::DragStack,0));
        storage.assign(27,{});storage[0]=stack("stone",63);excludes.excludedStorage.fill(true);excludes.excludedStorage[0]=false;
        p=run(9,TransferGesture::DragStack,0);check(p&&p->steps.size()==1&&p->steps[0].amount==1&&p->result[9].count==63);
        std::mt19937 random(77);
        for(int test=0;test<300;++test){
            player.assign(36,{});storage.assign(1+random()%54,{});o={};excludes={};
            for(int i=9;i<36;++i){if(random()%3)player[i]=stack(std::to_string(random()%5),1+random()%64);excludes.excludedInventory[i]=random()%15==0;}
            for(int i=0;i<static_cast<int>(storage.size());++i){if(random()%3)storage[i]=stack(std::to_string(random()%5),1+random()%64);storage[i].locked=storage[i].count&&random()%13==0;excludes.excludedStorage[i]=random()%15==0;}
            auto original=player;original.insert(original.end(),storage.begin(),storage.end());
            for(auto gesture:{TransferGesture::WheelOne,TransferGesture::WheelStack,TransferGesture::DragOne,TransferGesture::DragStack})for(int direction:{-1,1}){
                int const hover=random()%original.size();auto plan=run(hover,gesture,direction);if(!plan)continue;
                auto simulation=original;
                for(auto step:plan->steps){check(!step.swap&&step.amount>0);check(applyMove(simulation,step));check(totals(simulation)==totals(original));}
                check(simulation==plan->result);
                for(int i=0;i<static_cast<int>(original.size());++i)
                    if(i<9||original[i].locked||(i<36?excludes.excludedInventory[i]:excludes.excludedStorage[i-36]))check(simulation[i]==original[i]);
            }
        }
        RuntimeConfig c;check(validRuntimeConfig(c)&&!c.inventoryTransfer&&c.keybinds[14]==0);
        std::cout<<checks<<" InventoryTransfer checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
