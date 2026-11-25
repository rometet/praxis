#pragma once
// Pick Block runtime (stages B/C).
//
// Stage B: crosshair block -> item type name -> player snapshot -> PickBlockPolicy.
// Stage C: perform the *selection* half with Player::setSelectedItem, which is the
// only slot-selection function exported by LeviLamina.lib (measured with llvm-nm).
//
// Deliberately limited:
//  * no item creation and no server request,
//  * no distance re-check (LocalPlayer::getPickRange is NOT exported),
//  * block type -> item mapping is identity on the type name; anything else is
//    reported as unsupported instead of guessed,
//  * SwapIntoHotbar is planned but not executed until stage D integrates the
//    inventory Job, and that is logged instead of silently doing nothing.
//
// Installed like CommandKeysRuntime: a ClientLevelTickEvent listener that polls the
// configured key, so no game hook and no packet path is invented.
#include <cstdint>
#include <string>

namespace praxis::companion {
bool startPickBlockRuntime();
bool stopPickBlockRuntime();
// Default unbound (0) so a fresh install never steals an existing key.
bool configurePickBlock(unsigned key, unsigned modifiers, bool enabled);
std::string pickBlockStatus();
// Returns true when a hotbar slot selection was performed this tick.
bool tryPickBlock();
} // namespace praxis::companion
