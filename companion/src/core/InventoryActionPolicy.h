#pragma once
#include "core/RuntimePolicy.h"
namespace praxis::companion {
enum class InventoryAction : unsigned {Sort,SameType,All,Cancel,Count};
inline HotkeyChord inventoryActionBinding(Settings const& s,InventoryAction action) noexcept {
    auto const& o=s.inventoryTransfer;
    switch(action){
    case InventoryAction::Sort:return {unsigned(s.inventorySort.actionKey),s.inventorySort.actionModifiers,4};
    case InventoryAction::SameType:return {unsigned(o.sameKey),o.sameModifiers,4};
    case InventoryAction::All:return {unsigned(o.allKey),o.allModifiers,4};
    case InventoryAction::Cancel:return {unsigned(o.cancelKey),o.cancelModifiers,4};
    default:return {};
    }
}
inline bool inventoryActionConflict(Settings const& s,InventoryAction action) noexcept {
    auto const own=inventoryActionBinding(s,action);
    for(unsigned i=0;i<unsigned(InventoryAction::Count);++i)
        if(i!=unsigned(action)&&hotkeyConflict(own,inventoryActionBinding(s,InventoryAction(i))))return true;
    return false;
}
// Changing a binding or entering a GUI with a held key cannot start an action.
struct InventoryActionEdge {
    HotkeyChord previous{};bool held{};
    bool sample(HotkeyChord chord,bool down,unsigned modifiers,bool eligible) noexcept {
        bool const edge=previous.key==chord.key&&previous.modifiers==chord.modifiers&&down&&!held;
        previous=chord;held=down;
        return eligible&&edge&&modifiersMatch(chord.modifiers,modifiers);
    }
};
}
