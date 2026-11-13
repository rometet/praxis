#pragma once
#include "core/HotkeyPolicy.h"
#include <array>
#include <cstdint>
#include <array>
#include <cstdint>
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
    // KeyInputEvent can deliver multiple Down events for a single physical hold.
    // Toggle only on a fresh Down edge, never on OS/game auto-repeat.
    std::array<bool, 256> keyHeld{};
    std::array<std::uint64_t, 256> lastAcceptedMs{};
    static constexpr std::uint64_t keyDebounceMs = 160;
    bool consumeKeyEdge(int code, bool down, std::uint64_t nowMs) noexcept {
        if (code <= 0 || code >= static_cast<int>(keyHeld.size())) return false;
        auto const key = static_cast<std::size_t>(code);
        if (!down) { keyHeld[key] = false; return false; }
        if (keyHeld[key]) return false;
        keyHeld[key] = true;
        if (lastAcceptedMs[key] != 0 && nowMs >= lastAcceptedMs[key]
            && nowMs - lastAcceptedMs[key] < keyDebounceMs) return false;
        lastAcceptedMs[key] = nowMs ? nowMs : 1;
        return true;
    }
    void releaseIfPhysicallyUp(int code, bool down) noexcept {
        if (code > 0 && code < static_cast<int>(keyHeld.size()) && !down)
            keyHeld[static_cast<std::size_t>(code)] = false;
    }
    void clear() noexcept { *this = {}; }
    // A manual press of the vanilla key returns control to the player.
    void manualOverride(bool sneakDown, bool sprintDown) noexcept {
        if (sneakDown) sneak = false;
        if (sprintDown) sprint = false;
        // Keep applied* until the runtime clears only the state we installed.
        // Vanilla physical Shift/Ctrl then owns subsequent movement.
    }
};
} // namespace praxis::companion
