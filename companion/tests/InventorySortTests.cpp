#include "core/RuntimePolicy.h"
#include <random>
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
    unsigned checks{};auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("InventorySort contract");};
    auto stack=[](std::string key,int count=1,int max=64){return StackValue{key,key,count,max,false,false};};
    try{
        InventorySortOptions options;
        std::vector<StackValue> storage{stack("z",32),stack("a",1),stack("z",48),{},stack("a",3)};
        auto plan=planSort(storage,false,options);check(plan.has_value());
        auto now=storage;auto const inventoryTotals=totals(storage);
        for(auto step:plan->steps){check(applyMove(now,step));check(totals(now)==inventoryTotals);check(validStacks(now));}
        check(now==plan->result&&now[0]==stack("a",4)&&now[1]==stack("z",64)&&now[2]==stack("z",16)&&now[3].count==0);
        check(planSort(now,false,options)->steps.empty());
        options.mergeStacks=false;plan=planSort(storage,false,options);check(plan->result[0]==stack("a",3)&&plan->result[1]==stack("a",1));
        options.mergeStacks=true;options.excludedStorage[1]=true;storage[2].locked=true;
        plan=planSort(storage,false,options);check(plan->result[1]==storage[1]&&plan->result[2]==storage[2]);
        for(auto step:plan->steps)check(step.source!=1&&step.source!=2&&step.target!=1&&step.target!=2);
        std::vector<StackValue> player(36);player[0]=stack("z");player[8]=stack("a");player[9]=stack("c");player[35]=stack("b");
        options={};plan=planSort(player,true,options);check(plan->result[0]==player[0]&&plan->result[8]==player[8]&&plan->result[9]==stack("b"));
        options.preserveHotbar=false;plan=planSort(player,true,options);check(plan->result[0]==stack("a"));
        auto special=std::vector<StackValue>{stack("a:ench1"),stack("a:ench2"),stack("a:ench1")};
        plan=planSort(special,false,options);check(plan->result[0]==stack("a:ench1",2)&&plan->result[1]==stack("a:ench2"));
        check(!planSort(std::vector<StackValue>(55),false,options));check(!planSort(std::vector<StackValue>(27),true,options));
        check(!planSort({stack("bad",65)},false,options));check(!planSort({stack("bad",1,0)},false,options));
        auto invalid=storage;check(!applyMove(invalid,{-1,0,1,false}));check(!applyMove(invalid,{0,0,1,false}));check(!applyMove(invalid,{1,0,1,false}));check(!applyMove(invalid,{0,3,999,false}));
        // Randomized conservation and excluded/locked slot invariance, with
        // enough repeated types to exercise both merges and permutations.
        std::mt19937 random(417);
        for(int test=0;test<300;++test){
            int const size=1+random()%54;std::vector<StackValue> values(size);InventorySortOptions o;
            for(int i=0;i<size;++i){if(random()%5)values[i]=stack(std::to_string(random()%7),1+random()%64);values[i].locked=values[i].count>0&&random()%11==0;o.excludedStorage[i]=random()%13==0;}
            auto p=planSort(values,false,o);check(p.has_value());auto simulation=values;
            for(auto step:p->steps){check(applyMove(simulation,step));check(totals(simulation)==totals(values));}
            check(simulation==p->result);
            for(int i=0;i<size;++i)if(o.excludedStorage[i]||values[i].locked)check(simulation[i]==values[i]);
            check(planSort(simulation,false,o)->steps.empty());
        }
        RuntimeConfig c;check(!c.inventorySort&&validRuntimeConfig(c)&&c.keybinds[13]==0);
        c.inventorySortOptions.actionKey=0x79;check(!validRuntimeConfig(c));
        std::cout<<checks<<" InventorySort checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
