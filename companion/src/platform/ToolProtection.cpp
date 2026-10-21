#include "platform/ToolProtection.h"
#include "platform/WeaponSwitch.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "render/ShulkerPreview.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/deps/nbt/CompoundTag.h"
#include <Windows.h>
#include <mutex>
#include <optional>

namespace praxis::companion {
namespace {
bool localGameplay(Player const& source) {
    if (!runtimeReady.load() || !inputAllowed.load() || guiInputCaptured()) return false;
    DWORD process{};
    auto window = GetForegroundWindow();
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) return false;
    auto client = ll::service::getClientInstance();
    auto player = client ? client->getLocalPlayer() : nullptr;
    return player == &source && player->isAlive() && !player->isCreative() && !player->isSpectator()
        && client->isInGameInputEnabled() && player->mInventory;
}
bool blockUse(Player const& source, bool attacking = false) {
    auto s = settings();
    if (!s->on(Feature::ToolProtection) || (attacking && !s->toolProtection.protectAttacks)
        || !localGameplay(source)) return false;
    auto& inventory = *source.mInventory;
    if (inventory.mSelectedContainerId != ContainerID::Inventory) return false;
    int const slot = inventory.mSelected;
    if (slot < 0 || slot >= 9) return false;
    auto const& item = source.getInventory().getItem(slot);
    if (item.isNull() || !item.mItem) return false;
    if (!previewItemWithinBudget(item)) return true;
    return protectTool(toolAtThreshold(item.mItem->getMaxDamage(),item.getDamageValue(),s->toolProtection));
}
LL_TYPE_INSTANCE_HOOK(ProtectedStart, ll::memory::HookPriority::High, GameMode,
    &GameMode::$startDestroyBlock, bool, BlockPos const& pos, uchar face, bool& destroyed) {
    try { if (blockUse(mPlayer)) { destroyed = false; return false; } } catch (...) { destroyed = false; return false; }
    return origin(pos, face, destroyed);
}
LL_TYPE_INSTANCE_HOOK(ProtectedContinue, ll::memory::HookPriority::High, GameMode,
    &GameMode::$continueDestroyBlock, bool, BlockPos const& pos, uchar face, Vec3 const& position, bool& destroyed) {
    try { if (blockUse(mPlayer)) { destroyed = false; return false; } } catch (...) { destroyed = false; return false; }
    return origin(pos, face, position, destroyed);
}
LL_TYPE_INSTANCE_HOOK(ProtectedAttack, ll::memory::HookPriority::High, GameMode,
    &GameMode::$attack, bool, Actor& target, Vec3 const& position) {
    WeaponAttackScope weapon(mPlayer);
    try { if (blockUse(mPlayer,true)) return false; } catch (...) { return false; }
    return origin(target, position);
}
struct Hook { int (*add)(); bool (*remove)(); bool installed{}; };
template<class T> Hook hook() { return {[] { return T::hook(); }, [] { return T::unhook(); }, false}; }
Hook hooks[]{hook<ProtectedStart>(), hook<ProtectedContinue>(), hook<ProtectedAttack>()};
}
void resetToolProtection() noexcept {}
void observeToolProtection(LocalPlayer&) noexcept {}
bool installToolProtection() {
    for (auto& h : hooks) if (!h.installed) {
        if (h.add() != 0) { removeToolProtection(); return false; }
        h.installed = true;
    }
    availableFeatures.fetch_or(1u << static_cast<unsigned>(Feature::ToolProtection));
    availableFeatures.fetch_or(1u << static_cast<unsigned>(Feature::WeaponSwitch));
    return true;
}
bool removeToolProtection() noexcept {
    availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::ToolProtection)));
    availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::WeaponSwitch)));
    bool ok = true;
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it) if (it->installed) {
        if (it->remove()) it->installed = false; else ok = false;
    }
    resetToolProtection(); return ok;
}
}
