#include "core/PickBlockPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace praxis::companion;
namespace {
PickSlot stack(std::string item, int count, bool locked = false) {
    PickSlot s;
    s.item = std::move(item);
    s.count = count;
    s.locked = locked;
    return s;
}
std::vector<PickSlot> emptyPlayer() { return std::vector<PickSlot>(pickInventorySlots + 1); }
}
int main() {
    unsigned checks = 0;
    auto require = [&](bool good, char const* why) {
        ++checks;
        if (!good) throw std::runtime_error(why);
    };
    try {
        // 1) selecting an existing hotbar stack, left to right, larger stack wins
        {
            auto slots = emptyPlayer();
            slots[2] = stack("minecraft:stone", 12);
            slots[5] = stack("minecraft:stone", 40);
            PickAction a = planPick(slots, {"minecraft:stone", 0}, {});
            require(a.kind == PickActionKind::SelectHotbar, "hotbar selection kind");
            require(a.slot == 5, "larger hotbar stack wins");
            PickOptions smaller;
            smaller.preferLargerStack = false;
            require(planPick(slots, {"minecraft:stone", 0}, smaller).slot == 2, "first hotbar slot when ties");
        }
        // 2) selection can be disabled; the inventory swap is then used
        {
            auto slots = emptyPlayer();
            slots[1] = stack("minecraft:stone", 3);
            slots[20] = stack("minecraft:stone", 30);
            PickOptions opts;
            opts.selectFromHotbar = false;
            PickAction a = planPick(slots, {"minecraft:stone", 0}, opts);
            require(a.kind == PickActionKind::SwapIntoHotbar, "swap kind");
            require(a.slot == 20, "inventory source slot");
            require(a.amount == 30, "swap amount equals source count");
        }
        // 3) nothing in hand -> no action, never invent items
        {
            auto slots = emptyPlayer();
            slots[6] = stack("minecraft:dirt", 64);
            PickAction a = planPick(slots, {"minecraft:stone", 0}, {});
            require(a.kind == PickActionKind::None, "no matching item means no action");
            require(a.slot == -1, "no slot touched");
        }
        // 4) locked stacks are never used
        {
            auto slots = emptyPlayer();
            slots[3] = stack("minecraft:stone", 10, true);
            slots[15] = stack("minecraft:stone", 5, true);
            require(planPick(slots, {"minecraft:stone", 0}, {}).kind == PickActionKind::None,
                    "locked stacks are ignored");
        }
        // 5) swap disabled -> choose hotbar or nothing
        {
            auto slots = emptyPlayer();
            slots[25] = stack("minecraft:stone", 9);
            PickOptions opts;
            opts.allowInventorySwap = false;
            require(planPick(slots, {"minecraft:stone", 0}, opts).kind == PickActionKind::None,
                    "swap disabled means no movement");
        }
        // 6) offhand is a last resort
        {
            auto slots = emptyPlayer();
            slots[pickOffhandIndex] = stack("minecraft:stone", 7);
            PickAction a = planPick(slots, {"minecraft:stone", 0}, {});
            require(a.kind == PickActionKind::SwapIntoHotbar, "offhand swap");
            require(a.slot == pickOffhandIndex, "offhand index");
        }
        // 7) invalid inputs fail closed
        {
            auto slots = emptyPlayer();
            slots[0] = stack("minecraft:stone", 5);
            PickOptions bad;
            bad.selectedHotbar = pickHotbarSlots;
            require(!validPickOptions(bad), "out of range selected slot rejected");
            require(planPick(slots, {"minecraft:stone", 0}, bad).kind == PickActionKind::None,
                    "invalid options produce no action");
            require(planPick(slots, {"", 0}, {}).kind == PickActionKind::None, "empty request");
            auto broken = emptyPlayer();
            broken[4].count = -3;
            require(planPick(broken, {"minecraft:stone", 0}, {}).kind == PickActionKind::None,
                    "corrupt snapshot fails closed");
        }
        // 8) text validation for the search box
        {
            require(validPickText("stone", 16), "plain text accepted");
            require(!validPickText("", 16), "empty text rejected");
            require(!validPickText(std::string(17, 'a'), 16), "too long rejected");
            require(!validPickText(std::string_view("a\x01b", 3), 16), "control char rejected");
        }
        // 9) snapshot helpers: game side only fills PickSlot values
        {
            PickSlot s = pickSlotFromStack("minecraft:stone", 40, 0, false);
            require(s.item == "minecraft:stone" && s.count == 40 && !s.locked, "slot from stack");
            PickSlot clamped = pickSlotFromStack("minecraft:stone", 500, 0, false);
            require(clamped.count == 99, "count clamped to 99");
            PickSlot negative = pickSlotFromStack("minecraft:stone", -4, 0, false);
            require(negative.count == 0, "negative count becomes 0");
            PickSlot bad = pickSlotFromStack("", 5, 0, false);
            require(bad.locked && bad.count == 0, "empty identifier becomes a locked empty slot");
            int selected = -1;
            require(!resolveSelectedHotbar(-1, selected), "unknown selection rejected");
            require(!resolveSelectedHotbar(9, selected), "out of range selection rejected");
            require(resolveSelectedHotbar(4, selected) && selected == 4, "valid selection resolved");
            PickAction select;
            select.kind = PickActionKind::SelectHotbar;
            select.slot = 4;
            require(pickActionTouchesSelected(select, 4), "selecting the held slot is detected");
            require(!pickActionTouchesSelected(select, 3), "other slot is fine");
            PickAction swap;
            swap.kind = PickActionKind::SwapIntoHotbar;
            swap.slot = 20;
            require(!pickActionTouchesSelected(swap, 4), "a swap is not a held-slot change");
            PickAction none;
            require(!pickActionTouchesSelected(none, 4), "no action touches nothing");
        }
        std::cout << checks << " PickBlockPolicy checks passed\n";
    } catch (std::exception const& e) {
        std::cerr << "PickBlockPolicy FAILED after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
