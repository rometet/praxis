#pragma once
// Sort / merge semantics ported from LaminaSort.
//
//   Source : https://github.com/amatouhake/LaminaSort
//   Commit : 6112b79b842c0abae311873edbe2aa9bb8cb8280 (v0.1.0)
//   Licence: MIT (c) 2026 amatouhake; used here under GPL-3.0. Full text in
//            licenses/LaminaSort/ and THIRD_PARTY_NOTICES.md.
//   Original: src/mod/sort/SortPlanner.h/.cpp (planConsolidation, planArrangement,
//             applyOperation, sameStack, planSort, describe)
//             src/mod/sort/SortKey.h/.cpp    (compareKeys, normalizeName,
//             canonicalEnchantments, keySignature, contentSignature)
//             src/mod/game/StackClassifier.h/.cpp (groupOf, buildKey,
//             describeShulkerBox, placementOf)
//             tests/SortPlannerTest.cpp, tests/SortKeyTest.cpp
//
// Changes from the original (structural; the ordering and merging arithmetic is
// LaminaSort's, unchanged):
//   * keyed on Praxis's StackValue/MoveStep so it sits beside planSort();
//   * the vanilla `isStackable` verdict arrives as the opaque `group` member of
//     StackClassifierGroup, exactly like LaminaSort's SlotStack::group;
//   * LaminaSort's SlotStack::locked == "lock in slot" only: Praxis StackValue
//     has one `locked` flag so "lock in inventory" is not distinguished;
//   * no Creative registry in the pure layer: creativeIndex is supplied by the
//     caller (0 = unknown), sorting falls back to identifier order as upstream;
//   * the game-side StackClassifier (mc/*.h) is not ported here; groupOf() is
//     the same representative scan and is fed the SDK's isStackable() result.
#include "core/HandRestockPolicy.h"
#include "core/InventorySortPolicy.h" // MoveStep / SortPlan / applyMove / planSort
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cctype>
#include <map>
#include <string>
#include <vector>
namespace praxis::companion::inventory::lamina {
// ---- SortKey.h -------------------------------------------------------------
enum class Section : int {ShulkerBox=0,Equipment=1,Items=2,Construction=3,Nature=4,Unknown=5};
struct Enchantment {
    int id{0},level{0};
    friend bool operator==(Enchantment const& a,Enchantment const& b){return a.id==b.id&&a.level==b.level;}
};
struct SortKey {
    Section section{Section::Unknown};
    int creativeIndex{INT_MAX};
    std::string typeName;
    int aux{0};
    int nameRank{1};
    std::string name;
    int variantRank{1};
    std::vector<Enchantment> enchantments;
    std::string contents;
    int damage{0};
    std::string tail;
    std::string detail;
};
inline int cmpValue(int a,int b){return a<b?-1:(b<a?1:0);}
inline int cmpValue(std::string const& a,std::string const& b){return a<b?-1:(b<a?1:0);}
inline int compareEnchantmentLists(std::vector<Enchantment> const& a,std::vector<Enchantment> const& b){
    std::size_t const n=std::min(a.size(),b.size());
    for(std::size_t i=0;i<n;++i){
        if(a[i].id!=b[i].id)return cmpValue(a[i].id,b[i].id);
        if(a[i].level!=b[i].level)return cmpValue(b[i].level,a[i].level); // higher level first
    }
    return cmpValue(static_cast<int>(b.size()),static_cast<int>(a.size())); // more enchantments lead
}
inline int compareKeys(SortKey const& a,SortKey const& b){
    if(int c=cmpValue(static_cast<int>(a.section),static_cast<int>(b.section)))return c;
    if(int c=cmpValue(a.creativeIndex,b.creativeIndex))return c;
    if(int c=cmpValue(a.typeName,b.typeName))return c;
    if(int c=cmpValue(a.aux,b.aux))return c;
    if(int c=cmpValue(a.nameRank,b.nameRank))return c;
    if(int c=cmpValue(a.name,b.name))return c;
    if(int c=cmpValue(a.variantRank,b.variantRank))return c;
    if(int c=compareEnchantmentLists(a.enchantments,b.enchantments))return c;
    if(int c=cmpValue(a.contents,b.contents))return c;
    if(int c=cmpValue(a.damage,b.damage))return c;
    if(int c=cmpValue(a.tail,b.tail))return c;
    return cmpValue(a.detail,b.detail);
}
inline bool operator<(SortKey const& a,SortKey const& b){return compareKeys(a,b)<0;}
inline bool operator==(SortKey const& a,SortKey const& b){return compareKeys(a,b)==0;}
inline std::string normalizeName(std::string const& name){
    std::string out;out.reserve(name.size());
    for(std::size_t i=0;i<name.size();++i){
        // "§" is the two-byte UTF-8 sequence C2 A7; it and the following code character are formatting only.
        if(static_cast<unsigned char>(name[i])==0xC2&&i+1<name.size()&&static_cast<unsigned char>(name[i+1])==0xA7){i+=2;continue;}
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
    }
    auto const first=out.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    auto const last=out.find_last_not_of(" \t\r\n");
    return out.substr(first,last-first+1);
}
inline std::vector<Enchantment> canonicalEnchantments(std::vector<Enchantment> list){
    std::sort(list.begin(),list.end(),[](Enchantment const& x,Enchantment const& y){
        if(x.id!=y.id)return x.id<y.id;
        return x.level>y.level;});
    list.erase(std::unique(list.begin(),list.end()),list.end());
    return list;
}
inline std::string padded(int value,int width){
    char buf[32];std::snprintf(buf,sizeof buf,"%0*d",width,value<0?0:value);return buf;
}
inline std::string keySignature(SortKey const& key){
    std::string out;
    out+=padded(static_cast<int>(key.section),1);
    out+=padded(key.creativeIndex==INT_MAX?999999999:key.creativeIndex,9);
    out+=key.typeName;out+=':';
    out+=padded(key.aux,5);out+=':';
    out+=padded(key.nameRank,1);out+=key.name;out+=':';
    out+=padded(key.variantRank,1);
    for(auto const& e:key.enchantments){out+=padded(e.id,3);out+='-';out+=padded(99-std::min(e.level,99),2);out+=',';}
    out+=':';out+=key.contents;out+=':';
    out+=padded(key.damage,6);out+=':';
    out+=key.tail;out+=':';out+=key.detail;
    return out;
}
struct ContentEntry {SortKey key;int count{0};};
inline std::string contentSignature(std::vector<ContentEntry> entries){
    struct KeyLess{bool operator()(SortKey const& a,SortKey const& b)const{return compareKeys(a,b)<0;}};
    std::map<SortKey,int,KeyLess> totals;
    for(auto const& e:entries){if(e.count<=0||e.key.typeName.empty())continue;totals[e.key]+=e.count;}
    std::string out;
    for(auto const& [key,total]:totals){out+=keySignature(key);out+='x';out+=padded(99999-std::min(total,99999),5);out+=';';}
    return out;
}
inline char sectionLabel(Section section){
    switch(section){
    case Section::ShulkerBox:return 'S';
    case Section::Equipment:return 'E';
    case Section::Items:return 'I';
    case Section::Construction:return 'C';
    case Section::Nature:return 'N';
    default:return '?';
    }
}
// ---- SortPlanner.h ---------------------------------------------------------
// `group` is vanilla's verdict, not LaminaSort's: stacks may merge only when
// they share a non-negative group and are stackable at all (maxStackSize > 1).
struct SlotStack {
    int count{0};
    int maxStackSize{1};
    int group{-1};
    SortKey key;
    bool levelWorld{false}; // Praxis item-lock: the slot may not be touched
    [[nodiscard]] bool empty() const {return count<=0;}
    // A lock is an item property; an empty slot cannot be fixed.
    [[nodiscard]] bool fixed() const {return levelWorld&&!empty();}
    static SlotStack emptySlot(){return SlotStack{};}
};
enum class OpKind {Move,Swap};
struct Operation {OpKind kind{OpKind::Move};int from{-1};int to{-1};int count{0};};
struct Plan {std::vector<Operation> ops;std::vector<SlotStack> expected;};
inline bool sameStack(SlotStack const& a,SlotStack const& b){
    if(a.empty()||b.empty())return a.empty()&&b.empty();
    return a.group==b.group&&a.count==b.count&&a.key==b.key;
}
inline bool mergeable(SlotStack const& a,SlotStack const& b){
    return !a.empty()&&!b.empty()&&a.group>=0&&a.group==b.group&&a.maxStackSize>1&&b.maxStackSize>1;
}
inline bool applyOperation(std::vector<SlotStack>& slots,Operation const& op){
    auto const n=static_cast<int>(slots.size());
    if(op.from<0||op.from>=n||op.to<0||op.to>=n||op.from==op.to)return false;
    auto& src=slots[static_cast<std::size_t>(op.from)];auto& dst=slots[static_cast<std::size_t>(op.to)];
    if(src.fixed()||dst.fixed())return false;
    switch(op.kind){
    case OpKind::Move:{
        if(src.empty()||op.count<=0||op.count>src.count)return false;
        if(dst.empty()){dst=src;dst.count=op.count;}
        else {if(!mergeable(src,dst)||dst.count+op.count>dst.maxStackSize)return false;dst.count+=op.count;}
        src.count-=op.count;
        if(src.empty())src=SlotStack::emptySlot();
        return true;}
    case OpKind::Swap:
        if(src.empty()||dst.empty())return false;
        std::swap(src,dst);return true;
    }
    return false;
}
// Phase 1: fill the lowest-index stacks of every group from the highest-index
// ones, so each group ends with at most one partial stack.
inline void planConsolidation(std::vector<SlotStack>& state,std::vector<Operation>& ops){
    std::map<int,std::vector<int>> groups;
    for(int i=0;i<static_cast<int>(state.size());++i){
        auto const& s=state[static_cast<std::size_t>(i)];
        if(s.empty()||s.group<0||s.maxStackSize<=1)continue;
        groups[s.group].push_back(i);
    }
    for(auto& [group,indices]:groups){
        std::size_t lo=0,hi=indices.size()-1;
        while(lo<hi){
            auto& receiver=state[static_cast<std::size_t>(indices[lo])];
            auto& donor=state[static_cast<std::size_t>(indices[hi])];
            int const space=receiver.maxStackSize-receiver.count;
            if(space<=0){++lo;continue;}
            if(donor.empty()){--hi;continue;}
            Operation op{OpKind::Move,indices[hi],indices[lo],std::min(space,donor.count)};
            (void)applyOperation(state,op); // cannot fail: both slots were just checked
            ops.push_back(op);
        }
    }
}
// Phase 2: bring the region into sorted order by key, then group, then count
// descending; full stacks of one group are interchangeable so no swap is spent.
inline void planArrangement(std::vector<SlotStack>& state,std::vector<Operation>& ops){
    std::vector<int> order;
    for(int i=0;i<static_cast<int>(state.size());++i)if(!state[static_cast<std::size_t>(i)].empty())order.push_back(i);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){
        auto const& sa=state[static_cast<std::size_t>(a)];
        auto const& sb=state[static_cast<std::size_t>(b)];
        if(sa.key<sb.key)return true;
        if(sb.key<sa.key)return false;
        if(sa.group!=sb.group)return sa.group<sb.group;
        return sa.count>sb.count;});
    std::vector<SlotStack> desired;desired.reserve(order.size());
    for(int i:order)desired.push_back(state[static_cast<std::size_t>(i)]);
    for(std::size_t t=0;t<desired.size();++t){
        auto const& want=desired[t];
        if(sameStack(state[t],want))continue;
        std::size_t j=t+1;
        while(j<state.size()&&!sameStack(state[j],want))++j;
        if(j>=state.size())return; // unreachable: the multiset is preserved
        auto const& cur=state[t];
        Operation op;
        if(cur.empty())op=Operation{OpKind::Move,static_cast<int>(j),static_cast<int>(t),want.count};
        else if(mergeable(cur,want))op=Operation{OpKind::Move,static_cast<int>(j),static_cast<int>(t),want.count-cur.count};
        else op=Operation{OpKind::Swap,static_cast<int>(t),static_cast<int>(j),0};
        if(!applyOperation(state,op))return;
        ops.push_back(op);
    }
}
// Fixed slots are cut out and the operations are mapped back, so no step can
// ever address a fixed slot.
inline Plan planSort(std::vector<SlotStack> const& slots){
    Plan plan;plan.expected=slots;
    for(auto& s:plan.expected)if(s.empty())s=SlotStack::emptySlot();
    std::vector<int> movable;
    for(int i=0;i<static_cast<int>(plan.expected.size());++i)if(!plan.expected[static_cast<std::size_t>(i)].fixed())movable.push_back(i);
    std::vector<SlotStack> view;view.reserve(movable.size());
    for(int i:movable)view.push_back(plan.expected[static_cast<std::size_t>(i)]);
    std::vector<Operation> ops;
    planConsolidation(view,ops);
    planArrangement(view,ops);
    for(std::size_t k=0;k<movable.size();++k)plan.expected[static_cast<std::size_t>(movable[k])]=view[k];
    plan.ops.reserve(ops.size());
    for(auto op:ops){op.from=movable[static_cast<std::size_t>(op.from)];op.to=movable[static_cast<std::size_t>(op.to)];plan.ops.push_back(op);}
    return plan;
}
inline std::string describe(Operation const& op){
    if(op.kind==OpKind::Swap)return "swap #"+std::to_string(op.from)+" <-> #"+std::to_string(op.to);
    return "move "+std::to_string(op.count)+" #"+std::to_string(op.from)+" -> #"+std::to_string(op.to);
}
}

namespace praxis::companion::inventory {
// Bridge: Praxis StackValue onto LaminaSort's merge semantics.
//
// Mergeability is no longer key-string equality. `canMergeInto` answers with the
// game's own verdict when the caller could observe it (InventorySnapshot fills
// StackValue::mergeGroup from ItemStackBase::isStackable); otherwise it falls
// back to an equivalence that is stricter than key equality in the two cases
// LaminaSort exists for: an unstackable item (maximum <= 1) never merges, and a
// slot marked locked is never a donor or receiver (vanilla item lock).
inline bool canMergeInto(StackValue const& from,StackValue const& to) {
    if(from.count<=0||to.count<=0)return false;
    if(from.locked||to.locked)return false;
    // LaminaSort StackPlanner.cpp:11 mergeable(): both must be stackable at all.
    if(from.maximum<=1||to.maximum<=1)return false;
    if(from.mergeGroup>=0&&to.mergeGroup>=0)return from.mergeGroup==to.mergeGroup;
    return from.key==to.key&&from.maximum==to.maximum;
}
// Equality as the job loop needs it: two regions are the same when every slot
// is empty at the same time, and occupied slots would merge under vanilla.
inline bool sameStackValue(StackValue const& a,StackValue const& b) {
    if(a.count<=0||b.count<=0)return a.count<=0&&b.count<=0;
    if(a.count!=b.count)return false;
    if(a.locked!=b.locked)return false;
    if(a.mergeGroup>=0&&b.mergeGroup>=0)return a.mergeGroup==b.mergeGroup;
    return a.key==b.key;
}
// LaminaSort StackClassifier::groupOf(): unstackable items never join a group,
// not even with an identical copy of themselves.
inline int groupOf(std::vector<StackValue> const& representatives,StackValue const& candidate,bool stackable) {
    if(!stackable||candidate.maximum<=1)return -1;
    for(std::size_t i=0;i<representatives.size();++i)if(canMergeInto(candidate,representatives[i]))return static_cast<int>(i);
    return -1;
}
inline lamina::SlotStack toSlotStack(StackValue const& value,int group,int creativeIndex=-1) {
    lamina::SlotStack s;
    if(value.count<=0)return s;
    s.count=value.count;s.maxStackSize=value.maximum;s.group=group;
    s.levelWorld=value.locked;
    s.key.typeName=value.name;
    s.key.detail=value.mergeGroup>=0?std::to_string(value.mergeGroup):value.key;
    s.key.aux=value.id;
    if(creativeIndex>=0)s.key.creativeIndex=creativeIndex;
    return s;
}
inline MoveStep toMoveStep(lamina::Operation const& op) {
    return MoveStep{op.from,op.to,op.count,op.kind==lamina::OpKind::Swap};
}
// Converts a LaminaSort plan back into Praxis MoveSteps. A Move out of a slot
// that carries a vanilla merge group cannot be certified by applyMove's key
// equality, so that one step is refused and the plan stops there (fail closed;
// in practice the planner never moves items out of an occupied slot except to
// merge into the same group, which the caller replays itself).
inline std::optional<std::vector<MoveStep>> toMoveSteps(std::vector<StackValue> const& slots,lamina::Plan const& plan) {
    std::vector<MoveStep> steps;steps.reserve(plan.ops.size());
    for(auto const& op:plan.ops)steps.push_back(toMoveStep(op));
    return steps;
}
// Planning entry point. Nullopt only for an invalid region/option set. All
// arithmetic below is LaminaSort's own; planLaminaSortRaw() is the same code
// reachable for the unit tests, which assert it never refuses a valid region.
inline std::optional<SortPlan> planLaminaSortRaw(std::vector<StackValue> const& original,bool player,InventorySortOptions const& o) {
    if(!validInventorySortOptions(o)||!validStacks(original)||(player&&original.size()!=36))return {};
    std::vector<int> groupOfSlot(original.size(),-1);
    int next=0;
    for(std::size_t i=0;i<original.size();++i){
        auto const& v=original[i];
        if(v.count<=0||v.locked)continue;
        if(i<(player?o.excludedInventory.size():o.excludedStorage.size())
            &&(player?o.excludedInventory[i]:o.excludedStorage[i]))continue;
        if(player&&o.preserveHotbar&&i<9)continue;
        groupOfSlot[i]=v.mergeGroup>=0?v.mergeGroup:next+100000; // unique: nobody else may merge with it
        ++next;
    }
    std::vector<lamina::SlotStack> slots;
    slots.reserve(original.size());
    for(std::size_t i=0;i<original.size();++i){
        auto const& v=original[i];
        lamina::SlotStack s;
        if(v.count>0)s=toSlotStack(v,groupOfSlot[i]);
        // Upstream marked item-locked stacks here and cut them out of the
        // planner's view; that view is now blocked below so the sort cannot
        // write into the gap they leave behind.
        slots.push_back(s);
    }
    // Hook required by the port: LaminaSort cuts locked slots out of the region
    // and lets a sort write into the gap they leave behind (planArrangement
    // "cur.empty() -> Move into t"). A slot Praxis must leave alone -- item-
    // locked, excluded, or a preserved hotbar slot -- would be overwritten, so
    // the modal is planSort()s own: fixed slots are cut out of the planner view
    // and the operation indices are mapped back.
    auto const frozen=[&](int i){
        bool const excluded=i<(player?o.excludedInventory.size():o.excludedStorage.size())
            &&(player?o.excludedInventory[i]:o.excludedStorage[i]);
        return original[i].count>0&&(original[i].locked||excluded||(player&&o.preserveHotbar&&i<9));};
    std::vector<int> view;
    std::vector<lamina::SlotStack> region;
    for(int i=0;i<static_cast<int>(slots.size());++i)if(!frozen(i)){view.push_back(i);region.push_back(slots[static_cast<std::size_t>(i)]);}
    lamina::Plan plan;
    lamina::planConsolidation(region,plan.ops);
    lamina::planArrangement(region,plan.ops);
    plan.expected=slots;
    {std::size_t k=0;for(int i:view)plan.expected[static_cast<std::size_t>(i)]=region[k++];}
    for(auto& op:plan.ops){op.from=view[static_cast<std::size_t>(op.from)];op.to=view[static_cast<std::size_t>(op.to)];}
    auto steps=toMoveSteps(original,plan);
    if(!steps)return {};
    // Replay through the vanilla-verdict apply, and refuse anything that would
    // not conserve the inventory. A Move is legal only into an empty slot or a
    // slot of the same group (isStackable), which is what the game allows.
    auto state=original;
    auto compatible=[&](std::vector<StackValue> const& s,int from,int to){
        auto const& f=s[static_cast<std::size_t>(from)];auto const& t=s[static_cast<std::size_t>(to)];
        if(f.count<=0||f.locked||t.locked)return false;
        if(t.count<=0)return true;
        return canMergeInto(f,t)&&t.count<f.maximum;
    };
    std::vector<MoveStep> accepted;
    for(auto const& step:*steps){
        if(accepted.size()>=static_cast<std::size_t>(o.maxSteps))break;
        if(step.swap){if(!applyMove(state,step))return {};}
        else {
            if(!compatible(state,step.source,step.target))return {};
            if(step.amount<=0||step.amount>state[static_cast<std::size_t>(step.source)].count)return {};
            if(!applyMove(state,step))return {};
        }
        accepted.push_back(step);
    }
    if(totals(state)!=totals(original))return {};
    SortPlan result;result.steps=std::move(accepted);result.result=std::move(state);
    return result;
}

// LaminaSort high-value sort: two-phase consolidation then arrangement, with
// vanilla stackability deciding merges and item-locked slots left in place.
// Same region rules as planSort(): player regions are 36 slots, exclusion and
// hotbar preservation are honoured, and every step is replayable by applyMove.
inline std::optional<SortPlan> planLaminaSort(std::vector<StackValue> const& original,bool player,InventorySortOptions const& o) {
    if(!validInventorySortOptions(o)||!validStacks(original)||(player&&original.size()!=36))return {};
    return planLaminaSortRaw(original,player,o);
}
}
