#pragma once
#include "core/UiLayoutPolicy.h"
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <cstdio>
#include <cstdint>
#include <utility>

namespace praxis::companion {
struct PlayerHudOptions {
    bool foodHud{}, offhandHud{}, offhandShowEmpty{true}, offhandShowCount{true}, offhandShowDurability{true};
    float foodX{12}, foodY{220}, foodScale{1};
    float offhandX{12}, offhandY{580}, offhandScale{1};
    bool operator==(PlayerHudOptions const&) const = default;
};
inline bool validPlayerHudOptions(PlayerHudOptions const& o) noexcept {
    auto position=[](float v){return std::isfinite(v)&&v>=0&&v<=16384;};
    auto scale=[](float v){return std::isfinite(v)&&v>=.5f&&v<=3;};
    return position(o.foodX)&&position(o.foodY)&&position(o.offhandX)&&position(o.offhandY)
        &&scale(o.foodScale)&&scale(o.offhandScale);
}
enum class FoodSource { Unknown, ServerObserved, AuthoritativeLocal, ClientAttribute };
struct FoodValue { std::optional<float> value; FoodSource source{FoodSource::Unknown}; };
inline std::optional<float> exactFoodValue(FoodValue const& v,float maximum) noexcept {
    if(v.source==FoodSource::Unknown||!v.value||!std::isfinite(*v.value)||*v.value<0||*v.value>maximum)return {};
    return v.value;
}
inline std::string foodText(FoodValue const& v,float maximum) {
    auto value=exactFoodValue(v,maximum);
    if(!value)return "Unknown";
    char number[32]{};std::snprintf(number,sizeof(number),"%.2f",*value);return number;
}
// Held-food restoration is derived from the SDK's live IFoodItemComponent
// nutrition/saturation modifier. Unknown components never invent a preview.
struct HeldFoodPreview {
    bool present{};
    std::string name;
    int nutrition{};
    float saturationModifier{};
    bool canAlwaysEat{};
    bool operator==(HeldFoodPreview const&) const = default;
};
struct FoodRestoration {
    int hunger{}; float saturation{};
    bool operator==(FoodRestoration const&) const = default;
};
struct FoodSnapshot { FoodValue hunger,saturation,exhaustion; HeldFoodPreview held; };
inline std::optional<FoodRestoration> heldFoodRestoration(FoodSnapshot const& food, HeldFoodPreview const& held) noexcept {
    auto hunger = exactFoodValue(food.hunger, 20);
    if(!held.present || !hunger || !std::isfinite(held.saturationModifier)
        || held.saturationModifier < 0 || held.saturationModifier > 20
        || held.nutrition < 0 || held.nutrition > 20) return {};
    int const hungerRestored = std::min(20 - static_cast<int>(*hunger), held.nutrition);
    float const saturationGain = static_cast<float>(held.nutrition) * held.saturationModifier * 2.0f;
    float const saturationRestored = std::min(static_cast<float>(hungerRestored), saturationGain);
    return FoodRestoration{hungerRestored, std::min(20.0f, std::max(0.0f, saturationRestored))};
}
inline std::array<std::string,5> foodHudLines(FoodSnapshot const& food) {
    std::array<std::string,5> lines{
        "Hunger (client): "+foodText(food.hunger,20),
        "Saturation (client): "+foodText(food.saturation,20),
        "Exhaustion: "+foodText(food.exhaustion,5),
        "", ""};
    if(food.held.present) {
        auto const restoration = heldFoodRestoration(food, food.held);
        if(restoration) {
            char number[96]{};
            std::snprintf(number,sizeof(number),"Held food: +%d hunger, +%.2f saturation",
                restoration->hunger, restoration->saturation);
            lines[3]=number;
        } else {
            lines[3]="Held food: "+food.held.name+" (restoration unavailable)";
        }
        lines[4]=food.held.name.size()>64?food.held.name.substr(0,64):food.held.name;
    }
    return lines;
}
struct HitboxOptions {
    bool enabled{}, transparent{}, showUnclassified{true};
    float range{64}; int maxActors{96};
    std::uint32_t color{0xFFFFFFFF};
    bool operator==(HitboxOptions const&) const = default;
};
inline bool validHitboxOptions(HitboxOptions const& o) noexcept {
    return std::isfinite(o.range)&&o.range>=8&&o.range<=192&&o.maxActors>=8&&o.maxActors<=256;
}
struct HitboxPoint {float x{},y{},z{};};
struct HitboxBounds {HitboxPoint min,max;};
inline bool validHitboxBounds(HitboxBounds const& b) noexcept {
    for(auto pair:std::array<std::pair<float,float>,3>{{{b.min.x,b.max.x},{b.min.y,b.max.y},{b.min.z,b.max.z}}})
        if(!std::isfinite(pair.first)||!std::isfinite(pair.second)||pair.second<=pair.first||pair.second-pair.first>128)return false;
    return true;
}
inline std::array<std::pair<HitboxPoint,HitboxPoint>,12> hitboxEdges(HitboxBounds const& b) noexcept {
    std::array<HitboxPoint,8> p;
    for(unsigned i=0;i<8;++i)p[i]={i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z};
    std::array<std::pair<HitboxPoint,HitboxPoint>,12> edges;unsigned n=0;
    for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit))edges[n++]={p[i],p[i|bit]};
    return edges;
}
}
