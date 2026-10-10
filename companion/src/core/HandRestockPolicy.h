#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <algorithm>
namespace praxis::companion {
struct HandRestockOptions {
    int threshold=6;
    int quietMs=150;
    bool fromHotbar=false;
    bool replaceRemainders=true;
    bool offhand=false;
    std::array<bool,36> excludedInventory{};
    std::array<std::string,8> excludedItems{}; // Exact SDK identifiers; empty entries are unused.
    bool operator==(HandRestockOptions const&) const=default;
};
inline bool validHandRestockOptions(HandRestockOptions const& o) noexcept {
    return o.threshold>=0&&o.threshold<=16&&o.quietMs>=150&&o.quietMs<=1000
        &&std::all_of(o.excludedItems.begin(),o.excludedItems.end(),[](auto const& name){
            auto const colon=name.find(':');
            return name.empty()||(name.size()<=128&&colon!=std::string::npos&&colon>0&&colon+1<name.size()
                &&name.find(':',colon+1)==std::string::npos&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c==':'||c=='.'||c=='/';}));
        });
}
namespace inventory {
struct StackValue {
    std::string key,name;
    int count{},maximum{};
    bool damageable{},locked{};
    int id{},category{}; // Public typed SDK metadata, never inferred from the identity key.
    int rarity{};
    bool operator==(StackValue const&)const=default;
};
struct HandSnapshot {
    std::array<StackValue,37> slots{}; // offhand participates in every validation
    int selected=-1,dimension{};
    std::uint64_t generation{},player{};
    bool operator==(HandSnapshot const&)const=default;
};
inline std::string remainderFor(std::string const& name) {
    if(name=="minecraft:water_bucket"||name=="minecraft:lava_bucket"||name=="minecraft:milk_bucket"||name=="minecraft:powder_snow_bucket")return "minecraft:bucket";
    if(name=="minecraft:mushroom_stew"||name=="minecraft:rabbit_stew"||name=="minecraft:beetroot_soup"||name=="minecraft:suspicious_stew")return "minecraft:bowl";
    if(name=="minecraft:potion"||name=="minecraft:honey_bottle")return "minecraft:glass_bottle";
    return {};
}
struct RestockStep {int source{},target{},amount{};bool swap{};};
inline std::optional<RestockStep> planRestock(HandSnapshot const& before,HandSnapshot const& after,HandRestockOptions const& o,int target=-1) {
    if(target<0)target=before.selected;
    bool const secondary=target==36;
    if(!validHandRestockOptions(o)||(!secondary&&(target!=before.selected||target<0||target>=9))||(secondary&&!o.offhand)
        ||before.selected<0||before.selected>=9||after.selected!=before.selected||before.generation!=after.generation
        ||before.player!=after.player||before.dimension!=after.dimension)return {};
    for(int i=0;i<37;++i)if(i!=target&&before.slots[i]!=after.slots[i])return {};
    auto const& used=before.slots[target];auto const& now=after.slots[target];
    if((target<36&&o.excludedInventory[target])||std::find(o.excludedItems.begin(),o.excludedItems.end(),used.name)!=o.excludedItems.end())return {};
    if(used.count<=0||used.damageable||used.locked||now.locked||used.maximum<=0)return {};
    bool const depleted=now.count==0;
    bool const same=now.key==used.key&&now.count<used.count;
    auto const remainder=remainderFor(used.name);
    bool const swapped=o.replaceRemainders&&used.count==1&&now.count==1&&!remainder.empty()&&now.name==remainder;
    if(!depleted&&!same&&!swapped)return {};
    // Exactly one correlated use is required. A larger or ambiguous delta cancels.
    if(!swapped&&used.count-now.count!=1)return {};
    if(!swapped&&now.count>std::min(o.threshold,used.maximum-1))return {};
    int source=-1;
    auto scan=[&](int first,int last) {
        for(int i=first;i<last;++i){auto const& c=after.slots[i];
            if(i==target||o.excludedInventory[i]||c.locked||c.key!=used.key||c.count<=0)continue;
            if(source<0||c.count>after.slots[source].count||(c.count==after.slots[source].count&&i>source))source=i;
        }
    };
    scan(9,36);if(source<0&&o.fromHotbar)scan(0,9);
    if(source<0)return {};
    return RestockStep{source,target,swapped?after.slots[source].count:std::min(used.maximum-now.count,after.slots[source].count),swapped};
}
inline std::optional<RestockStep> planOffhandSwap(HandSnapshot const& value) {
    int const source=value.selected;
    if(source<0||source>=9||!value.generation||!value.player)return {};
    auto const& main=value.slots[source];auto const& off=value.slots[36];
    if(main.locked||off.locked||(!main.count&&!off.count)||main==off)return {};
    return RestockStep{source,36,0,true};
}
}
}
