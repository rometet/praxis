#pragma once
#include "core/HandRestockPolicy.h"
#include "core/HotkeyPolicy.h"
#include <array>
#include <map>
#include <vector>
namespace praxis::companion {
struct InventorySortOptions {
    bool preserveHotbar=true;
    bool mergeStacks=true;
    int actionKey=0;
    unsigned actionModifiers=anyModifiers;
    int sortMethod=0; // Keep older opaque "order" profile fields unowned.
    bool shulkerLast=false,bundleLast=false;
    bool excludeInventoryGui=false,excludeStorageGui=false;
    int maxSteps=256;
    std::array<bool,36> excludedInventory{};
    std::array<bool,54> excludedStorage{};
    bool operator==(InventorySortOptions const&)const=default;
};
inline bool validInventorySortOptions(InventorySortOptions const& o) noexcept {
    return o.actionKey>=0&&o.actionKey<=255&&o.actionKey!=0x79&&o.actionKey!=0x7A
        &&validModifiers(o.actionModifiers)&&o.sortMethod>=0&&o.sortMethod<=4&&o.maxSteps>=1&&o.maxSteps<=256;
}
namespace inventory {
struct MoveStep {int source{},target{},amount{};bool swap{};bool operator==(MoveStep const&)const=default;};
inline bool validStacks(std::vector<StackValue> const& values) {
    if(values.size()>54)return false;
    for(auto const& s:values)if(s.count<0||(s.count>0&&(s.key.empty()||s.maximum<1||s.maximum>255||s.count>s.maximum)))return false;
    return true;
}
inline std::map<std::string,int> totals(std::vector<StackValue> const& values) {
    std::map<std::string,int> result;
    for(auto const& s:values)if(s.count>0)result[s.key]+=s.count;
    return result;
}
inline bool applyMove(std::vector<StackValue>& values,MoveStep const& step) {
    if(step.source<0||step.target<0||step.source==step.target||step.source>=static_cast<int>(values.size())||step.target>=static_cast<int>(values.size()))return false;
    auto& from=values[step.source];auto& to=values[step.target];
    if(from.locked||to.locked)return false;
    if(step.swap){std::swap(from,to);return true;}
    if(from.count<=0||step.amount<=0||step.amount>from.count
        ||(to.count>0&&(to.key!=from.key||to.maximum!=from.maximum)))return false;
    if(to.count+step.amount>from.maximum)return false;
    int const count=to.count;if(!count)to=from;
    to.count=count+step.amount;from.count-=step.amount;if(!from.count)from={};return true;
}
struct SortPlan {std::vector<MoveStep> steps;std::vector<StackValue> result;};
inline std::optional<SortPlan> planSort(std::vector<StackValue> const& original,bool player,InventorySortOptions const& o) {
    if(!validInventorySortOptions(o)||!validStacks(original)||(player&&original.size()!=36))return {};
    auto values=original;std::vector<int> slots;
    for(int i=0;i<static_cast<int>(values.size());++i){
        bool const exclude=player?o.excludedInventory[i]||(o.preserveHotbar&&i<9):o.excludedStorage[i];
        if(!exclude&&!values[i].locked)slots.push_back(i);
    }
    SortPlan plan;
    auto move=[&](MoveStep step){if(!applyMove(values,step))return false;plan.steps.push_back(step);return plan.steps.size()<=static_cast<std::size_t>(o.maxSteps);};
    if(o.mergeStacks)for(std::size_t a=0;a<slots.size();++a){int target=slots[a];
        if(!values[target].count)continue;
        for(std::size_t b=a+1;b<slots.size()&&values[target].count<values[target].maximum;++b){int source=slots[b];
            if(values[source].count<=0||values[source].key!=values[target].key||values[source].maximum!=values[target].maximum)continue;
            int const amount=std::min(values[source].count,values[target].maximum-values[target].count);
            if(!move({source,target,amount,false}))return {};
        }
    }
    auto last=[&](StackValue const& s){
        bool const shulker=s.name=="minecraft:shulker_box"||s.name.ends_with("_shulker_box");
        bool const bundle=s.name=="minecraft:bundle"||s.name.ends_with("_bundle");
        return (o.shulkerLast&&shulker)||(o.bundleLast&&bundle);
    };
    auto before=[&](StackValue const& a,StackValue const& b){
        if(!a.count||!b.count)return a.count>0&&b.count==0;
        if(last(a)!=last(b))return !last(a);
        if(o.sortMethod==1&&a.count!=b.count)return a.count>b.count;
        if(o.sortMethod==2&&a.id!=b.id)return a.id<b.id;
        if(o.sortMethod==3&&a.category!=b.category)return a.category<b.category;
        if(o.sortMethod==4&&a.rarity!=b.rarity)return a.rarity>b.rarity;
        if(a.name!=b.name)return a.name<b.name;
        if(a.key!=b.key)return a.key<b.key;
        return a.count>b.count;
    };
    std::vector<StackValue> desired;for(int i:slots)desired.push_back(values[i]);
    std::stable_sort(desired.begin(),desired.end(),before);
    for(std::size_t a=0;a<slots.size();++a){int target=slots[a];
        if(values[target]==desired[a])continue;
        auto b=a+1;while(b<slots.size()&&values[slots[b]]!=desired[a])++b;
        if(b==slots.size()||!move({slots[b],target,0,true}))return {};
    }
    if(!validStacks(values)||totals(original)!=totals(values))return {};
    plan.result=std::move(values);return plan;
}
}
}
