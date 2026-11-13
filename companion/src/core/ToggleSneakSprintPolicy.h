#pragma once
#include "core/HotkeyPolicy.h"
namespace praxis::companion {
struct ToggleSneakSprintOptions {
    int sneakToggleKey = 'V';
    int sprintToggleKey = 'B';
    unsigned sneakModifiers = anyModifiers;
    unsigned sprintModifiers = anyModifiers;
    bool sneakEnabled = true;
    bool sprintEnabled = true;
    bool operator==(ToggleSneakSprintOptions const&) const = default;
};
inline bool validToggleSneakSprintOptions(ToggleSneakSprintOptions const& o) noexcept {
    auto key = [](int k) { return k >= 0 && k <= 255 && k != 0x79 && k != 0x7A; };
    if (!key(o.sneakToggleKey) || !key(o.sprintToggleKey)) return false;
    if (!validModifiers(o.sneakModifiers) || !validModifiers(o.sprintModifiers)) return false;
    if (o.sneakToggleKey && o.sneakToggleKey == o.sprintToggleKey
        && o.sneakModifiers == o.sprintModifiers) return false;
    return true;
}
struct ToggleStates {
    bool sneak{}, sprint{};
    bool appliedSneak{}, appliedSprint{};
    void clear() noexcept { *this = {}; }
    // A manual press of the vanilla key returns control to the player.
    void manualOverride(bool sneakDown, bool sprintDown) noexcept {
        if (sneakDown) { sneak = false; appliedSneak = false; }
        if (sprintDown) { sprint = false; appliedSprint = false; }
    }
};
} // namespace praxis::companion
