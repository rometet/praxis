#pragma once
#include "core/HotkeyPolicy.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace praxis::companion {
// CommandKeys is an additive, explicitly triggered module. It owns no persisted
// Feature slot; its document lives under the separate "commandKeys" object so
// existing numeric Feature slots and their masks are never renumbered.
inline constexpr unsigned commandKeySchemaVersion = 1;
inline constexpr unsigned commandKeyMacroMax = 32;
inline constexpr unsigned commandKeyPayloadsMax = 8;
inline constexpr unsigned commandKeyPayloadLengthMax = 256;
inline constexpr unsigned commandKeyNameLengthMax = 48;
inline constexpr unsigned commandKeyRepeatMax = 64;
inline constexpr unsigned commandKeyDelayMinMs = 100;
inline constexpr unsigned commandKeyDelayMaxMs = 60000;
inline constexpr unsigned commandKeyRateActionsMax = 256;

enum class CommandKeyMode : unsigned { Send = 0, Cycle = 1, Repeat = 2, Type = 3 };
enum class CommandKeyConflictPolicy : unsigned { Ignore = 0, Both = 1, Suppress = 2 };

inline constexpr bool validCommandKeyMode(CommandKeyMode mode) noexcept {
    return static_cast<unsigned>(mode) <= static_cast<unsigned>(CommandKeyMode::Type);
}
inline constexpr bool validCommandKeyConflictPolicy(CommandKeyConflictPolicy policy) noexcept {
    return static_cast<unsigned>(policy) <= static_cast<unsigned>(CommandKeyConflictPolicy::Suppress);
}
inline constexpr bool commandKeyDelayInRange(unsigned delayMs) noexcept {
    return delayMs >= commandKeyDelayMinMs && delayMs <= commandKeyDelayMaxMs;
}
// Persistence-independent configuration model. The runtime adapter saves these
// under a named "commandKeys" object, not under legacy numbered Feature slots.
struct CommandKeyMacro {
    std::string name;
    HotkeyChord hotkey{};
    CommandKeyMode mode{CommandKeyMode::Send};
    CommandKeyConflictPolicy conflict{CommandKeyConflictPolicy::Ignore};
    std::vector<std::string> payloads;
    unsigned delayMs{250};
    unsigned repeats{1};
    bool repeatWhileHeld{true};
    bool operator==(CommandKeyMacro const&) const = default;
};
inline bool validCommandKeyText(std::string_view text) noexcept {
    if (text.empty() || text.size() > commandKeyPayloadLengthMax) return false;
    for (unsigned char c : text)
        if (c < 0x20 || c == 0x7f) return false; // prevent multiline/control injection
    return true;
}
inline bool validCommandKeyMacro(CommandKeyMacro const& m) noexcept {
    if (m.name.empty() || m.name.size() > commandKeyNameLengthMax
        || !validCommandKeyMode(m.mode) || !validCommandKeyConflictPolicy(m.conflict)
        || !validModifiers(m.hotkey.modifiers) || !m.hotkey.key
        || !m.hotkey.contexts
        || m.payloads.empty() || m.payloads.size() > commandKeyPayloadsMax
        || !commandKeyDelayInRange(m.delayMs)
        || !m.repeats || m.repeats > commandKeyRepeatMax)
        return false;
    for (unsigned char c : m.name) if (c < 0x20 || c == 0x7f) return false;
    for (auto const& p : m.payloads) if (!validCommandKeyText(p)) return false;
    if (m.mode == CommandKeyMode::Type && m.payloads.size() != 1) return false;
    if (m.mode != CommandKeyMode::Repeat && m.repeats != 1) return false;
    return m.payloads.size() * m.repeats <= commandKeyRateActionsMax;
}
inline bool validCommandKeyProfile(std::span<CommandKeyMacro const> macros) noexcept {
    if (macros.size() > commandKeyMacroMax) return false;
    for (std::size_t i=0; i<macros.size(); ++i) {
        if (!validCommandKeyMacro(macros[i])) return false;
        for (std::size_t j=0; j<i; ++j) {
            if (macros[j].name == macros[i].name) return false;
            if (hotkeyConflict(macros[j].hotkey, macros[i].hotkey)
                && (macros[j].conflict != CommandKeyConflictPolicy::Both
                    || macros[i].conflict != CommandKeyConflictPolicy::Both))
                return false;
        }
    }
    return true;
}
} // namespace praxis::companion
