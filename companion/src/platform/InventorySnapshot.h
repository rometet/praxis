#pragma once
#include "core/HandRestockPolicy.h"
class ItemStack;
class LocalPlayer;
namespace praxis::companion::inventory {
StackValue stackValue(ItemStack const& item);
HandSnapshot handSnapshot(LocalPlayer& player);
bool inventoryFocus(bool gameplay);
}
