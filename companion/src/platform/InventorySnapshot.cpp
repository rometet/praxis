#include "platform/InventorySnapshot.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemLockHelper.h"
#include "mc/world/item/ItemLockMode.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorRuntimeID.h"
#include <Windows.h>
#include <stdexcept>
namespace praxis::companion::inventory {
StackValue stackValue(ItemStack const& item) {
    if(item.isNull()||item.mCount==0)return {};
    if(!item.mItem||!previewItemWithinBudget(item))throw std::runtime_error("Unreadable inventory item");
    StackValue result;
    result.name=item.getTypeName();result.count=item.mCount;result.maximum=item.getMaxStackSize();
    result.id=item.getId();result.category=static_cast<int>(item.mItem->mCreativeCategory);
    result.rarity=static_cast<int>(item.mItem->getRarity(item));
    result.damageable=item.mItem->getMaxDamage()>0;
    result.locked=ItemLockHelper::getItemLockMode(item)!=ItemLockMode::None;
    result.key=std::to_string(item.getId())+":"+std::to_string(item.getAuxValue())+":"+result.name+":";
    if(item.mUserData)result.key+=item.mUserData->toSnbt();
    if(result.key.size()>524288)throw std::runtime_error("Oversized inventory identity");
    return result;
}
HandSnapshot handSnapshot(LocalPlayer& player) {
    HandSnapshot s;s.generation=worldGeneration.load();s.player=player.getRuntimeID().rawID;
    s.dimension=player.getDimensionId();s.selected=player.mInventory?static_cast<int>(player.mInventory->mSelected):-1;
    if(!player.mInventory||player.mInventory->mSelectedContainerId!=ContainerID::Inventory)throw std::runtime_error("No selected inventory");
    std::size_t bytes{};
    for(int slot=0;slot<36;++slot){s.slots[slot]=stackValue(player.getInventory().getItem(slot));bytes+=s.slots[slot].key.size();}
    s.slots[36]=stackValue(player.getOffhandSlot());bytes+=s.slots[36].key.size();
    if(bytes>1048576)throw std::runtime_error("Inventory snapshot budget");
    return s;
}
bool inventoryFocus(bool gameplay) {
    if(!runtimeReady.load()||guiInputCaptured())return false;
    DWORD process{};auto window=GetForegroundWindow();
    if(!window||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId())return false;
    auto client=ll::service::getClientInstance();auto player=client?client->getLocalPlayer():nullptr;
    if(!player||!player->isAlive()||player->isCreative()||player->isSpectator())return false;
    if(gameplay)return inputAllowed.load()&&client->isInGameInputEnabled();
    // Container callers must independently prove the current top view,
    // controller type, cursor and text focus on their UI callback.
    return true;
}
}
