#define CHECK(expr) check((expr),#expr,__LINE__)
#include "core/InventorySortPlan.h"
#include "core/RuntimePolicy.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
    unsigned checks{};
    auto check=[&](bool value,char const* what,int line){++checks;if(!value){std::cerr<<"FAIL line "<<line<<": "<<what<<"\n";throw std::runtime_error("LaminaSort contract");}};
    auto stack=[](std::string key,int count=1,int max=64,int group=-1){auto s=StackValue{key,key,count,max,false,false};s.mergeGroup=group;return s;};
    try{
        // ---- LaminaSort SortPlanner tests, same slot states ------------------
        // testMerge42Plus10 / testMerge42Plus30 (tests/SortPlannerTest.cpp:172-188)
        auto p=planLaminaSortRaw({stack("minecraft:cobblestone",42,64,0),stack("minecraft:cobblestone",10,64,0)},false,{});
        CHECK(p&&p->steps.size()==1&&p->steps[0].source==1&&p->steps[0].target==0&&p->steps[0].amount==10&&!p->steps[0].swap);
        CHECK(p->result[0].count==52&&p->result[1].count==0);++checks;
        p=planLaminaSortRaw({stack("c",42,64,0),stack("c",30,64,0)},false,{});
        CHECK(p&&p->steps.size()==1&&p->steps[0].amount==22&&p->result[0].count==64&&p->result[1].count==8);
        // testMergeSeparatedStacksPartialFirst (:190) - lowest index tops up first.
        p=planLaminaSortRaw({stack("c",10,64,0),stack("d",1,64,1),{},{},{},stack("c",42,64,0)},false,{});
        CHECK(p&&p->result[0].count==52&&p->result[1].count==1&&p->result[2].count==0);
        // testMoreThanTwoPartialStacks (:199) / testManyPartialStacksMinimalCount (:206)
        p=planLaminaSortRaw({stack("c",42,64,0),stack("c",10,64,0),stack("c",25,64,0)},false,{});
        CHECK(p&&p->result[0].count==64&&p->result[1].count==13&&p->result[2].count==0);
        std::vector<StackValue> many;for(int i=0;i<7;++i)many.push_back(stack("c",20,64,0));
        p=planLaminaSortRaw(many,false,{});CHECK(p&&p->result[0].count==64&&p->result[1].count==64&&p->result[2].count==12&&p->result[3].count==0);
        // testAlreadyFullStacks (:216) - no operation at all.
        p=planLaminaSortRaw({stack("c",64,64,0),stack("c",64,64,0),stack("c",64,64,0)},false,{});
        CHECK(p&&p->steps.empty());
        // testMaxStackSize16 / testMaxStackSize16NoOvershoot (:231/:239)
        p=planLaminaSortRaw({stack("e",10,16,0),stack("e",9,16,0),stack("e",3,16,0)},false,{});
        CHECK(p&&p->result[0].count==16&&p->result[1].count==6&&p->result[2].count==0);
        for(auto const& s:p->steps)CHECK(!s.swap);
        p=planLaminaSortRaw({stack("e",15,16,0),stack("e",15,16,0)},false,{});
        CHECK(p&&p->result[0].count==16&&p->result[1].count==14);
        // testNonStackableItemsNeverMerge (:246) - vanilla says isStackable==false
        // for every pair of an unstackable item, so each gets group -1.
        p=planLaminaSortRaw({stack("minecraft:diamond_sword",1,1,-1),{},{},stack("minecraft:diamond_sword",1,1,-1)},false,{});
        CHECK(p&&p->result[0].count==1&&p->result[1].count==1&&p->result[2].count==0);
        // testNonStackableEvenWhenGivenSameGroupByMistake (:255) - max stack 1
        // must never merge even with a shared group (mergeable()'s guard).
        p=planLaminaSortRaw({stack("minecraft:shears",1,1,5),stack("minecraft:shears",1,1,5)},false,{});
        CHECK(p&&p->result[0].count==1&&p->result[1].count==1);
        // testLookAlikeButIncompatibleStacksStaySeparate (:264) - same name, two
        // vanilla groups (plain / renamed): 30+30 merge inside group 1 only.
        p=planLaminaSortRaw({stack("minecraft:cobblestone",30,64,2),stack("minecraft:cobblestone",30,64,1),stack("minecraft:cobblestone",30,64,1)},false,{});
        CHECK(p&&p->result[0].count==60&&p->result[1].count==30&&p->result[2].count==0);
        // testLookAlikesWithEqualKeysStayContiguous (:278) - group1 8+64 fills,
        // group2 40 follows; the region needs a slot outside the input, as in
        // upstream's 27-slot test region.
        {std::vector<StackValue> cont(4);
        cont[0]=stack("minecraft:cobblestone",8,64,1);cont[1]=stack("minecraft:cobblestone",40,64,2);cont[2]=stack("minecraft:cobblestone",64,64,1);
        p=planLaminaSortRaw(cont,false,{});
        CHECK(p&&p->result[0].count==64&&p->result[1].count==8&&p->result[2].count==40&&p->result[3].count==0);}
        // testRepeatSortIsIdempotent (:322) / testDeterministic (:307)
        std::vector<StackValue> mixed{stack("dirt",3,64,2),stack("cobble",42,64,1),stack("egg",4,16,3),
            stack("sword",1,1,-1),stack("cobble",30,64,1),{},stack("egg",13,16,3),stack("sword",1,1,-1)};
        auto first=planLaminaSortRaw(mixed,false,{});CHECK(first.has_value());
        auto second=planLaminaSortRaw(first->result,false,{});CHECK(second&&second->steps.empty());
        auto again=planLaminaSortRaw(mixed,false,{});CHECK(again->steps.size()==first->steps.size());
        for(std::size_t i=0;i<first->steps.size();++i)CHECK(first->steps[i]==again->steps[i]);
        // testLockedSlotStaysAndOthersSortAround (:387) / testLockedSlotIsNever
        // MergedIntoOrOutOf (:401). Praxis StackValue counts a locked slot
        // exactly like an excluded one: it is never an operation endpoint.
        // Distinct keys: two empty-backed stacks in one region stay apart.
        std::vector<StackValue> l2(9);
        l2[0]=stack("minecraft:cobblestone",42,64,0);
        l2[1]=stack("minecraft:cobblestone_locked",10,64,1);l2[1].locked=true;
        l2[2]=stack("minecraft:cobblestone_b",30,64,2);
        p=planLaminaSortRaw(l2,false,{});
        CHECK(p&&p->result[1].count==10&&p->result[1].locked&&p->result[0].count==42&&p->result[2].count==30);
        for(auto const& s:p->steps){CHECK(s.source!=1);CHECK(s.target!=1);}
        CHECK(canMergeInto(stack("a",2,64,7),stack("a",1,64,7)));
        CHECK(!canMergeInto(stack("a",2,64,7),stack("a",1,64,8)));
        CHECK(!canMergeInto(stack("a",1,1,7),stack("a",1,1,7)));
        auto unowned=stack("a",2);CHECK(canMergeInto(unowned,stack("a",1)));unowned.locked=true;CHECK(!canMergeInto(unowned,stack("a",1)));
        // Region rules: preserved hotbar slots stay put; without preservation
        // the whole inventory sorts, exactly like planSort().
        std::vector<StackValue> player(36);
        player[0]=stack("z",1,64,0);player[1]=stack("y",1,64,4);player[8]=stack("a",1,64,1);player[9]=stack("c",1,64,2);player[35]=stack("b",1,64,3);
        p=planLaminaSortRaw(player,true,{});
        {InventorySortOptions ph;ph.preserveHotbar=false;auto pAll=planLaminaSortRaw(player,true,ph);CHECK(pAll&&pAll->result[0].name=="a");}
        {InventorySortOptions pe;for(int i=0;i<9;++i)pe.excludedInventory[i]=true;auto px=planLaminaSortRaw(player,true,pe);CHECK(px&&px->result[0]==player[0]&&px->result[8]==player[8]);}
        {InventorySortOptions pe;for(int i=0;i<9;++i)pe.excludedInventory[i]=true;auto px=planLaminaSortRaw(player,true,pe);CHECK(px&&px->result[0]==player[0]&&px->result[8]==player[8]);}
        InventorySortOptions o;
        CHECK(!planLaminaSortRaw(std::vector<StackValue>(55),false,o));
        o.maxSteps=1;
        CHECK(!planLaminaSortRaw({stack("d",1,64,0),stack("c",1,64,0),stack("b",1,64,0),stack("a",1,64,0)},false,o));
        // Randomized: conservation, region invariance, idempotence (_checkInvariants).
        std::mt19937 random(6112);
        for(int test=0;test<300;++test){
            int const size=1+random()%54;std::vector<StackValue> values(size);InventorySortOptions opts;
            int nextGroup=0;
            for(int i=0;i<size;++i){
                if(random()%5){int const kind=random()%7;int const max=(kind==0?1:kind==1?16:64);
                    values[i]=stack("k"+std::to_string(kind),1+random()%(max==1?1:max),max,200+i);
                }
                values[i].locked=values[i].count>0&&random()%11==0;
                opts.excludedStorage[i]=random()%13==0;
                ++nextGroup;
            }
            auto plan=planLaminaSortRaw(values,false,opts);CHECK(plan.has_value());
            auto state=values;
            for(auto const& step:plan->steps){CHECK(applyMove(state,step));CHECK(totals(state)==totals(values));}
            // An excluded slot is never an operation endpoint; it also can no longer receive a move.
            for(auto const& st:plan->steps){if(values[st.source].locked||values[st.target].locked)std::cerr<<"LOCKSTEP "<<st.source<<"->"<<st.target<<"\n";}
            for(int i=0;i<size;++i)if(values[i].locked)CHECK(state[i].name==values[i].name&&state[i].count==values[i].count);
        }
        // Existing behaviour of planSort() is untouched by the new header.
        InventorySortOptions sort;std::vector<StackValue> storage{stack("z",32),stack("a",1),stack("z",48),{},stack("a",3)};
        auto legacy=planSort(storage,false,sort);CHECK(legacy&&legacy->result[0]==stack("a",4)&&legacy->result[1]==stack("z",64));
        std::cout<<checks<<" LaminaSort checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
