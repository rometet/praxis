#pragma once
// Persisted CommandKeys profile: the named "commandKeys" document described in
// CommandKeysOptions.h ("The runtime adapter saves these under a named
// "commandKeys" object, not under legacy numbered Feature slots").
//
// Why a FLAT schema: the pinned SDK serializer/deserializer round-trips nested
// structs (inventorySortOptions) but not a vector of structs -- a populated
// `bindings: [{...}]` makes decodeRuntimeConfig reject the whole file, while an
// empty array loads. Each macro is therefore stored as parallel scalars plus a
// command list, which every other field of this config already proves works.
#include "core/CommandKeysOptions.h"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace praxis::companion {

inline constexpr unsigned commandKeysSlots = 8;          // automated macros per profile
inline constexpr unsigned commandKeysCommandsMax = 4;    // steps per macro

struct CommandKeysConfig {
    // Default off on every process start, like QuickTrade's persisted opt-in.
    bool enableOnStart{};
    // Per macro slot: name, virtual key, modifiers, mode, delay, repeats.
    std::array<std::string, commandKeysSlots> names{};
    std::array<int, commandKeysSlots> keys{};
    std::array<unsigned, commandKeysSlots> modifiers{};
    std::array<unsigned, commandKeysSlots> modes{};
    std::array<unsigned, commandKeysSlots> delays{};
    std::array<unsigned, commandKeysSlots> repeats{};
    // Commands for the same slot, joined by '\n'. One string per slot keeps the
    // document scalar-only; empty means "no macro in this slot".
    std::array<std::string, commandKeysSlots> commands{};
    bool operator==(CommandKeysConfig const&) const = default;
};

inline std::vector<std::string_view> splitCommandKeys(std::string_view text) noexcept {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t next = text.find('\n', start);
        auto piece = text.substr(start, next == std::string_view::npos ? std::string_view::npos
                                                                      : next - start);
        if (!piece.empty()) out.push_back(piece);
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return out;
}

// Bound methods keep the struct comparable and the call sites short.
inline unsigned commandKeysConfigBindingCount(CommandKeysConfig const& c) noexcept {
    unsigned n = 0;
    for (unsigned slot = 0; slot < commandKeysSlots; ++slot)
        if (!c.names[slot].empty() && !c.commands[slot].empty()) ++n;
    return n;
}
inline bool validCommandKeysConfig(CommandKeysConfig const& config) noexcept {
    for (unsigned slot = 0; slot < commandKeysSlots; ++slot) {
        auto const& name = config.names[slot];
        auto const& command = config.commands[slot];
        int const key = config.keys[slot];
        if (name.empty() && command.empty()) continue;       // unused slot
        if (name.empty() || name.size() > commandKeyNameLengthMax) return false;
        if (key <= 0 || key > 255) return false;
        if (!validModifiers(config.modifiers[slot])) return false;
        if (!validCommandKeyMode(static_cast<CommandKeyMode>(config.modes[slot]))) return false;
        unsigned const delay = config.delays[slot] ? config.delays[slot] : 250u;
        if (!commandKeyDelayInRange(delay)) return false;
        unsigned const repeats = config.repeats[slot] ? config.repeats[slot] : 1u;
        if (repeats > commandKeyRepeatMax) return false;
        auto const steps = splitCommandKeys(command);
        if (steps.empty() || steps.size() > commandKeysCommandsMax) return false;
        for (auto step : steps)
            if (!validCommandKeyText(step)) return false;
        for (unsigned char c : name)
            if (c < 0x20 || c == 0x7f) return false;
    }
    return commandKeysConfigBindingCount(config) <= commandKeyMacroMax;
}


inline std::vector<CommandKeyMacro> commandKeysFromConfig(CommandKeysConfig const& config) {
    std::vector<CommandKeyMacro> macros;
    for (unsigned slot = 0; slot < commandKeysSlots; ++slot) {
        if (config.names[slot].empty() || config.commands[slot].empty()) continue;
        CommandKeyMacro m;
        m.name = config.names[slot];
        m.hotkey = HotkeyChord{static_cast<unsigned>(config.keys[slot]),
                               config.modifiers[slot] ? config.modifiers[slot] : anyModifiers, 1};
        m.mode = static_cast<CommandKeyMode>(config.modes[slot]);
        m.conflict = CommandKeyConflictPolicy::Ignore;
        for (auto step : splitCommandKeys(config.commands[slot]))
            m.payloads.emplace_back(step);
        m.delayMs = config.delays[slot] ? config.delays[slot] : 250u;
        m.repeats = config.repeats[slot] ? config.repeats[slot] : 1u;
        m.repeatWhileHeld = false;      // an automated press must fire exactly once
        macros.push_back(std::move(m));
    }
    return macros;
}

} // namespace praxis::companion
