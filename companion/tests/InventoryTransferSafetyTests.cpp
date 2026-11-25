#include "core/InventoryTransferSafetyPolicy.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace praxis::companion;
using namespace praxis::companion::inventory;
int main(){
    unsigned checks{};
    auto checkAt=[&](bool value,int line){++checks;if(!value){std::cerr<<"FAILED check #"<<checks<<" at line "<<line<<'\n';throw std::runtime_error("TransferSafety contract");}};
#define check(x) checkAt((x),__builtin_LINE())
    if(std::getenv("W4_TRACE")){
        std::vector<StackValue> storage(27),inventory(36);
        auto st=[](std::string n,int c){return StackValue{n,n,c,64,false,false};};
        storage[0]=st("stone",10);
        inventory[0]=st("stone",54);inventory[0].locked=true;
        inventory[1]=st("stone",54);
        auto p=planTransferRollback(storage,inventory,128);
        std::cerr<<"locked-trace: plan="<<(p?"yes":"null")<<" size="<<(p?p->size():0)<<'\n';
        for(auto& s:(p?*p:std::vector<MoveStep>{}))std::cerr<<"  step "<<s.source<<"->"<<s.target<<" x"<<s.amount<<'\n';
    }
    auto stack=[](std::string name,int count=1,int maximum=64){return StackValue{name,name,count,maximum,false,false};};
    try {
        // --- BEI StationTransfer.cpp:355 -- refuse while an item is on the cursor.
        check(transferAdmittedByCursor(0));
        check(!transferAdmittedByCursor(1));
        check(!transferAdmittedByCursor(64));
        check(!transferAdmittedByCursor(-1)); // unobservable cursor fails closed
        // --- BEI StationTransfer.cpp:396-401 -- write-back target selection.
        check(rollbackDestinationIsStorage(0)&&rollbackDestinationIsStorage(35));
        check(!rollbackDestinationIsStorage(36)&&!rollbackDestinationIsStorage(89));
        check(!rollbackDestinationIsStorage(-1)&&!rollbackDestinationIsStorage(90));

        // Empty destination region: nothing to return.
        {
            std::vector<StackValue> storage(27),inventory(36);
            auto plan=planTransferRollback(storage,inventory,128);
            check(plan&&plan->empty());
        }
        // BEI ordering rule, stated precisely: a destination stack that is already the
        // same item with room is topped up during pass 0; empty slots are only used in
        // pass 1. Here storage holds stone in two slots and the player already has partial
        // stone stacks, so the pass-0 top-ups must all be consumed before slot 36 (empty).
        {
            std::vector<StackValue> storage(27),inventory(36);
            storage[0]=stack("stone",20);storage[1]=stack("stone",5);
            inventory[3]=stack("stone",40);inventory[9]=stack("stone",1);
            auto plan=planTransferRollback(storage,inventory,128);
            check(plan&&!plan->empty());
            check(plan->size()==2);                       // both top-ups fit: no empty slot needed
            // 27 is the source-region width here, so 27+9 == the player's slot 9.
            check(plan->at(0).target==27+9);              // largest room first (63 > 24)
            check(plan->at(0).amount==20);                // storage[0]'s whole stack goes there
            check(plan->at(1).target==27+9);              // then storage[1]'s 5
            check(plan->at(1).amount==5);
            auto combined=storage;combined.insert(combined.end(),inventory.begin(),inventory.end());
            unsigned moved{};
            for(auto step:*plan){
                check(!step.swap&&step.amount>0);
                check(step.source<27&&step.target>=27&&step.target<27+36);
                check(!combined[step.target].locked);
                auto const before=totals(combined);
                check(applyMove(combined,step));
                check(totals(combined)==before); // conservation per step
                moved+=step.amount;
            }
            if(moved!=25)std::cerr<<"fixed moved="<<moved<<" steps="<<plan->size()<<'\n';
            check(moved==25);
            for(int i=0;i<27;++i){
                if(combined[i].count!=0)std::cerr<<"fixed drain storage["<<i<<"] key="<<combined[i].key
                                                 <<" count="<<combined[i].count<<'\n';
                check(combined[i].count==0); // source region fully drained
            }
        }
        // A locked destination slot is never used.
        {
            std::vector<StackValue> storage(27),inventory(36);
            storage[0]=stack("stone",10);
            inventory[0]=stack("stone",54);inventory[0].locked=true; // room, but locked
            inventory[1]=stack("stone",54);
            auto plan=planTransferRollback(storage,inventory,128);
            check(plan&&plan->size()==1&&plan->at(0).target==27+1);
        }
        // Unreturnable content -> nullopt, never a partial restore.
        {
            std::vector<StackValue> storage(27),inventory(36);
            storage[0]=stack("stone",64);
            // Every player slot is full of something else: the stone cannot be written back.
            for(int i=0;i<36;++i)inventory[i]=stack("dirt",64);
            auto plan=planTransferRollback(storage,inventory,128);
            check(!plan); // nowhere to put the stone: whole write-back refused
        }
        // maxSteps bound: a plan that cannot finish inside the budget is refused.
        {
            std::vector<StackValue> storage(27),inventory(36);
            storage[0]=stack("stone",64);
            auto plan=planTransferRollback(storage,inventory,128);
            check(plan&&plan->size()==1);
            auto tight=planTransferRollback(storage,inventory,0);
            check(!tight);
        }
        // maxSteps honoured exactly across several destination slots.
        {
            std::vector<StackValue> storage(27),inventory(36);
            // Distinct keys: identical full stacks would coalesce and need only one slot.
            storage[0]=stack("stone",64);storage[1]=stack("dirt",64);storage[2]=stack("sand",64);
            auto plan=planTransferRollback(storage,inventory,128);
            check(plan&&plan->size()==3);
            check(!planTransferRollback(storage,inventory,2));
        }
        std::mt19937 random(4242);
        for(int test=0;test<200;++test){
            std::vector<StackValue> storage(1+random()%54),inventory(36);
            for(auto& s:storage)if(random()%2)s=stack(std::to_string(random()%4),1+random()%64);
            for(auto& s:inventory)if(random()%2)s=stack(std::to_string(random()%4),1+random()%64);
            for(auto& s:inventory)if(random()%13==0)s.locked=true;
            auto plan=planTransferRollback(storage,inventory,128);
            if(!plan)continue;
            int const n=static_cast<int>(storage.size());
            // Flat numbering, same convention as planTransfer().
            auto sim=storage;sim.insert(sim.end(),inventory.begin(),inventory.end());
            auto const original=sim;
            for(auto step:*plan){
                auto const ok=step.source>=0&&step.source<n
                    &&step.target>=n&&step.target<static_cast<int>(sim.size())
                    &&!step.swap&&step.amount>0&&!sim[step.target].locked;
                if(!ok)std::cerr<<"iteration "<<test<<" bad step "<<step.source<<"->"<<step.target
                                    <<" x"<<step.amount<<" n="<<n<<" size="<<sim.size()<<'\n';
                if(!ok){
                    for(int i=0;i<n;++i)if(storage[i].count)std::cerr<<"   s["<<i<<"] "<<storage[i].key
                        <<" x"<<storage[i].count<<(storage[i].locked?" LOCKED":"")<<'\n';
                    for(int i=0;i<36;++i)if(inventory[i].count||inventory[i].locked)std::cerr<<"   p["<<i<<"] "
                        <<inventory[i].key<<" x"<<inventory[i].count<<(inventory[i].locked?" LOCKED":"")<<'\n';
                    for(auto& ss:*plan)std::cerr<<"   step "<<ss.source<<"->"<<ss.target<<" x"<<ss.amount<<'\n';
                }
                check(ok);
                auto before=totals(sim);
                auto const applied=applyMove(sim,step);
                if(!applied)std::cerr<<"iteration "<<test<<" applyMove failed "<<step.source<<"->"
                                     <<step.target<<" x"<<step.amount<<" n="<<n<<'\n';
                check(applied);
                if(totals(sim)!=before)std::cerr<<"iteration "<<test<<" conservation broken at "
                                                <<step.source<<"->"<<step.target<<" x"<<step.amount<<'\n';
                check(totals(sim)==before);
            }
            // A successful write-back always empties every unlocked source slot and
            // preserves totals; locked slots are pinned and must be left untouched.
            for(int i=0;i<n;++i){
                if(storage[i].locked){
                    if(!(sim[i]==original[i]))std::cerr<<"iteration "<<test<<" locked storage["<<i
                        <<"] mutated: "<<original[i].count<<" -> "<<sim[i].count<<'\n';
                    check(sim[i]==original[i]);
                    continue;
                }                if(sim[i].count!=0)
                    std::cerr<<"iteration "<<test<<" storage["<<i<<"] key="<<sim[i].key
                             <<" count="<<sim[i].count<<'\n';
                check(sim[i].count==0);
            }
            check(totals(sim)==totals(original));
        }
        std::cout<<checks<<" InventoryTransferSafety checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
