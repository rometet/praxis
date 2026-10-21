#pragma once
#include <array>
#include <cmath>
#include <string_view>
#include <tuple>
namespace praxis::companion {
enum class WeaponFamily { None, Sword, Axe, Mace, Trident };
struct WeaponSwitchOptions {
    int priority = 0; // 0 damage, 1 sword, 2 axe, 3 mace
    bool switchBack = false;
    bool includeAxes = true;
    bool includeTridents = true;
    bool skipLowDurability = true;
    bool operator==(WeaponSwitchOptions const&) const = default;
};
inline bool validWeaponSwitchOptions(WeaponSwitchOptions const& o) noexcept { return o.priority >= 0 && o.priority <= 3; }
inline WeaponFamily weaponFamily(std::string_view name) noexcept {
    if (name.ends_with("_sword")) return WeaponFamily::Sword;
    if (name.ends_with("_axe")) return WeaponFamily::Axe;
    if (name == "minecraft:mace") return WeaponFamily::Mace;
    if (name == "minecraft:trident") return WeaponFamily::Trident;
    return WeaponFamily::None;
}
struct WeaponCandidate {
    WeaponFamily family{};
    float damage{};
    int remaining{};
    bool protectedItem{};
};
inline int chooseWeapon(std::array<WeaponCandidate, 9> const& slots, int current, WeaponSwitchOptions const& o) noexcept {
    if (current < 0 || current >= 9 || !validWeaponSwitchOptions(o)) return -1;
    int best = -1;
    auto rank = [&](int slot) {
        auto const& c = slots[slot];
        bool const preferred = o.priority != 0 && static_cast<int>(c.family) == o.priority;
        return std::tuple{preferred, c.damage, slot == current, c.remaining, -slot};
    };
    for (int slot = 0; slot < 9; ++slot) {
        auto const& c = slots[slot];
        if (c.family == WeaponFamily::None || !std::isfinite(c.damage) || c.damage <= 0
            || (!o.includeAxes && c.family == WeaponFamily::Axe)
            || (!o.includeTridents && c.family == WeaponFamily::Trident)
            || (o.skipLowDurability && c.protectedItem)) continue;
        if (best < 0 || rank(slot) > rank(best)) best = slot;
    }
    return best;
}
}
