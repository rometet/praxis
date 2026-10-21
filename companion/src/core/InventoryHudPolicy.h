#pragma once
#include "core/HudLayoutPolicy.h"
namespace praxis::companion {
inline constexpr unsigned inventoryHudCells=27,inventoryHudFirstSlot=9;
struct InventoryHudOptions {
    bool showCount{true},showSlotNumbers{};
    float x{12},y{480},scale{1};
    bool operator==(InventoryHudOptions const&) const = default;
};
inline bool validInventoryHudOptions(InventoryHudOptions const& o) noexcept {
    return std::isfinite(o.x)&&std::isfinite(o.y)&&o.x>=0&&o.y>=0&&o.x<=16384&&o.y<=16384
        &&std::isfinite(o.scale)&&o.scale>=.5f&&o.scale<=3;
}
inline HudRect inventoryHudLayout(InventoryHudOptions const& o,HudTransform const& t,float width,float height) noexcept {
    if(!validInventoryHudOptions(o)||!validHudTransform(t)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return {};
    float scale=std::min({t.custom?t.scale:o.scale,width/304.f,height/132.f});
    return transformHud({o.x,o.y,304*scale,132*scale},t,width,height);
}
// Only the backpack. Hotbar 0..8, offhand and armor retain their existing HUD owners.
inline constexpr unsigned inventoryHudSlot(unsigned cell) noexcept {return cell<inventoryHudCells?inventoryHudFirstSlot+cell:36;}
}
