#pragma once
#include "core/UiLayoutPolicy.h"
#include <array>
namespace praxis::companion {
enum class HudId : unsigned {Information,Equipment,Offhand,Food,Chunks,Inventory,Count};
inline constexpr std::array<char const*,6> hudNames{"Information","Equipment","Offhand","Food","Chunks","Inventory"};
struct HudTransform {
    bool custom{};float x{},y{},scale{1};int anchor{};
    bool operator==(HudTransform const&) const = default;
};
struct HudLayoutOptions {std::array<HudTransform,5> panels{};HudTransform inventory{};bool operator==(HudLayoutOptions const&)const=default;};
inline bool validHudTransform(HudTransform const& t) noexcept {
    return std::isfinite(t.x)&&std::isfinite(t.y)&&std::abs(t.x)<=16384&&std::abs(t.y)<=16384
        &&std::isfinite(t.scale)&&t.scale>=.5f&&t.scale<=3&&t.anchor>=0&&t.anchor<=8;
}
inline bool validHudLayouts(HudLayoutOptions const& o)noexcept {
    return validHudTransform(o.inventory)&&std::all_of(o.panels.begin(),o.panels.end(),validHudTransform);
}
inline HudRect transformHud(HudRect original,HudTransform const& t,float width,float height)noexcept {
    if(!t.custom||!validHudTransform(t)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return original;
    float ax=(t.anchor%3)*.5f,ay=(t.anchor/3)*.5f;
    original.x=ax*(width-original.width)+t.x;original.y=ay*(height-original.height)+t.y;
    return original;
}
inline void moveHud(HudTransform& t,HudRect absolute,float width,float height)noexcept {
    float ax=(t.anchor%3)*.5f,ay=(t.anchor/3)*.5f;
    t.custom=true;t.x=std::clamp(absolute.x-ax*(width-absolute.width),-16384.f,16384.f);
    t.y=std::clamp(absolute.y-ay*(height-absolute.height),-16384.f,16384.f);
}
}
