#include "platform/WeaponSwitch.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include <Windows.h>
namespace praxis::companion {
namespace {
bool allowed(Player const& player) {
    if (!runtimeReady.load() || !inputAllowed.load() || guiInputCaptured()) return false;
    DWORD process{}; auto window = GetForegroundWindow();
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) return false;
    auto client = ll::service::getClientInstance();
    return client && client->getLocalPlayer() == &player && client->isInGameInputEnabled()
        && player.isAlive() && !player.isCreative() && !player.isSpectator() && player.mInventory;
}
}
WeaponAttackScope::WeaponAttackScope(Player& player) noexcept {
    try {
        auto s = settings();
        if (!s->on(Feature::WeaponSwitch) || !allowed(player)) return;
        auto& supplies = *player.mInventory;
        if (supplies.mSelectedContainerId != ContainerID::Inventory) return;
        int const current = supplies.mSelected;
        std::array<WeaponCandidate, 9> candidates{};
        for (int slot = 0; slot < 9; ++slot) {
            auto const& item = player.getInventory().getItem(slot);
            if (item.isNull() || !item.mItem || !previewItemWithinBudget(item)) continue;
            int const maximum = item.mItem->getMaxDamage(), damage = item.getDamageValue();
            candidates[slot] = {weaponFamily(item.getTypeName()), static_cast<float>(item.mItem->getAttackDamage()),
                std::max(0, maximum-damage), toolAtThreshold(maximum, damage, s->toolProtection)};
        }
        int const chosen = chooseWeapon(candidates, current, s->weaponSwitch);
        if (chosen < 0 || chosen == current) return;
        auto const generation = worldGeneration.load();
        supplies.selectSlot(chosen, ContainerID::Inventory);
        if (supplies.mSelected != chosen) return;
        if (s->weaponSwitch.switchBack) { player_ = &player; previous_ = current; selected_ = chosen; generation_ = generation; }
    } catch (...) {}
}
WeaponAttackScope::~WeaponAttackScope() {
    try {
        if (!player_ || worldGeneration.load() != generation_ || !allowed(*player_)) return;
        auto& supplies = *player_->mInventory;
        // Another callback/manual selection wins. Never restore over its choice.
        if (supplies.mSelectedContainerId == ContainerID::Inventory && supplies.mSelected == selected_)
            supplies.selectSlot(previous_, ContainerID::Inventory);
    } catch (...) {}
}
}
