#pragma once
// Pick Block (Java "pick block" equivalent) policy -- pure logic, no game objects.
//
// Java's pick block cycles: existing hotbar slot -> inventory -> empty hotbar slot,
// optionally placing a creative copy. Bedrock Mods must not invent items, so this
// policy only ever *selects* a hotbar slot or *moves* an existing stack inside the
// player inventory, and never more than one step per activation.
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace praxis::companion {
inline constexpr int pickHotbarSlots = 9;
inline constexpr int pickInventorySlots = 36;   // 0..8 hotbar, 9..35 inventory
inline constexpr int pickOffhandIndex = 36;

struct PickSlot {
    std::string item;                 // exact SDK identifier, e.g. "minecraft:stone"
    int count{};
    int aux{};                        // server-reported aux/variant, informative only
    bool locked{};                    // protected by the user (never move)
    bool operator==(PickSlot const&) const = default;
};

struct PickRequest {
    std::string item;                 // what the crosshair points at
    int aux{};
    bool operator==(PickRequest const&) const = default;
};

struct PickOptions {
    bool selectFromHotbar = true;     // pick an existing hotbar stack first
    bool allowInventorySwap = true;   // one swap from inventory into the hotbar
    bool preferLargerStack = true;    // when two candidates tie, take the larger stack
    int selectedHotbar = 0;           // slot whose contents the user is holding
    bool operator==(PickOptions const&) const = default;
};

enum class PickActionKind { None, SelectHotbar, SwapIntoHotbar };

struct PickAction {
    PickActionKind kind{PickActionKind::None};
    int slot{-1};                     // slot to select, or source slot for the swap
    int amount{};                     // amount for the swap (== source count)
    bool operator==(PickAction const&) const = default;
};

inline bool validPickOptions(PickOptions const& o) noexcept {
    return o.selectedHotbar >= 0 && o.selectedHotbar < pickHotbarSlots;
}

inline bool validPickSlot(PickSlot const& s) noexcept {
    return s.count >= 0 && s.count <= 99 && s.item.size() <= 128;
}

// Choose the action for one pick request over a complete player snapshot.
// The snapshot is the *authoritative* server view: unknown items are never guessed.
inline PickAction planPick(std::vector<PickSlot> const& slots, PickRequest const& request,
                           PickOptions const& options) {
    if (request.item.empty() || !validPickOptions(options)) return {};
    for (auto const& slot : slots)
        if (!validPickSlot(slot)) return {};

    auto matches = [&](std::size_t index) {
        return index < slots.size() && !slots[index].locked && slots[index].count > 0
            && slots[index].item == request.item;
    };
    auto better = [&](std::size_t a, std::size_t b) {
        if (!options.preferLargerStack) return a < b;
        return slots[a].count > slots[b].count || (slots[a].count == slots[b].count && a < b);
    };

    // 1) an already-held hotbar stack
    if (options.selectFromHotbar) {
        int best = -1;
        for (int i = 0; i < pickHotbarSlots && i < static_cast<int>(slots.size()); ++i) {
            if (!matches(static_cast<std::size_t>(i))) continue;
            if (best < 0 || better(static_cast<std::size_t>(i), static_cast<std::size_t>(best))) best = i;
        }
        if (best >= 0) {
            PickAction action;
            action.kind = PickActionKind::SelectHotbar;
            action.slot = best;
            action.amount = slots[static_cast<std::size_t>(best)].count;
            return action;
        }
    }

    // 2) one stack from the inventory (or offhand) swapped into the selected slot
    if (!options.allowInventorySwap) return {};
    for (int i = pickHotbarSlots; i < pickInventorySlots && i < static_cast<int>(slots.size()); ++i) {
        if (!matches(static_cast<std::size_t>(i))) continue;
        PickAction action;
        action.kind = PickActionKind::SwapIntoHotbar;
        action.slot = i;
        action.amount = slots[static_cast<std::size_t>(i)].count;
        return action;
    }
    if (slots.size() > static_cast<std::size_t>(pickOffhandIndex)
        && matches(static_cast<std::size_t>(pickOffhandIndex))) {
        PickAction action;
        action.kind = PickActionKind::SwapIntoHotbar;
        action.slot = pickOffhandIndex;
        action.amount = slots[static_cast<std::size_t>(pickOffhandIndex)].count;
        return action;
    }

    // 3) nothing in hand: do nothing (never invent items)
    return {};
}


// Text helpers used by the favourites/search boxes; kept tiny on purpose.
inline bool validPickText(std::string_view text, std::size_t maximum) noexcept {
    if (text.empty() || text.size() > maximum) return false;
    for (char c : text)
        if (static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Snapshot helpers: the game side only has to fill PickSlot values, everything
// else stays here and is testable without a running client.
// ---------------------------------------------------------------------------

// One PickSlot per player slot (0..8 hotbar, 9..35 inventory, 36 offhand).
inline PickSlot pickSlotFromStack(std::string_view item, int count, int aux,
                                  bool locked) noexcept {
    if (!validPickText(item, 128)) return PickSlot{std::string(item), 0, aux, true};
    PickSlot slot;
    slot.item = std::string(item);
    slot.count = count < 0 ? 0 : (count > 99 ? 99 : count);
    slot.aux = aux;
    slot.locked = locked;
    return slot;
}

// The client reports `-1` while no slot is selected (e.g. before the world is
// ready). Returning false keeps the caller from picking against an unknown state.
inline bool resolveSelectedHotbar(int reported, int& out) noexcept {
    if (reported < 0 || reported >= pickHotbarSlots) return false;
    out = reported;
    return true;
}

// True when the action would touch the slot the user is currently holding. The
// caller must not swap *into* that slot unless the plan explicitly selects it.
inline bool pickActionTouchesSelected(PickAction const& action, int selected) noexcept {
    if (selected < 0) return false;
    return action.kind == PickActionKind::SelectHotbar ? action.slot == selected
                                                       : false;
}
} // namespace praxis::companion
