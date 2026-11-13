#include "runtime/GameplayRuntime.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "map/MapDataSource.h"
#include "render/ShulkerPreview.h"
#include "runtime/DeathLog.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/HitResult.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <mutex>

namespace praxis::companion {
namespace {
std::mutex stateMutex;
std::array<ll::event::ListenerPtr, 5> listeners;
std::atomic_bool active{};
MiningSwitchState mining;
ToggleStates toggles;
bool leftDown{};
bool previousAlive{};
bool aliveKnown{};
std::uint64_t observedGeneration{};

bool foreground() noexcept {
    DWORD process{};
    auto window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}
bool gameplayAllowed() noexcept {
    if (!runtimeReady.load(std::memory_order_acquire) || !inputAllowed.load(std::memory_order_acquire)
        || guiInputCaptured() || !foreground())
        return false;
    auto client = ll::service::getClientInstance();
    if (!client) return false;
    auto* player = client->getLocalPlayer();
    return player && player->isAlive() && client->isInGameInputEnabled();
}
unsigned currentModifiers() noexcept {
    return modifierBits((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
}
void releaseMining(LocalPlayer& player) {
    try {
        if (mining.previousSlot >= 0 && mining.selectedSlot >= 0 && player.mInventory
            && mining.generation == worldGeneration.load()
            && player.mInventory->mSelectedContainerId == ContainerID::Inventory
            && player.mInventory->mSelected == mining.selectedSlot) {
            player.mInventory->selectSlot(mining.previousSlot, ContainerID::Inventory);
        }
    } catch (...) {}
    mining.selectedSlot = mining.previousSlot = -1;
    mining.release();
}
void autoToolTick(IClientInstance& client, LocalPlayer& player) {
    try {
        auto s = settings();
        if (!s->on(Feature::AutoTool) || !leftDown || !gameplayAllowed()) {
            if (mining.hasTarget) releaseMining(player);
            return;
        }
        auto const& hit = client.getLatestHitResult();
        if (hit.mIndirectHit || hit.mType != HitResultType::Tile) {
            releaseMining(player);
            return;
        }
        auto& source = player.getDimension().getBlockSourceFromMainChunkSource();
        if (!source.getChunkAt(hit.mBlock)) {
            releaseMining(player);
            return;
        }
        auto& inventory = *player.mInventory;
        if (inventory.mSelectedContainerId != ContainerID::Inventory
            || inventory.mSelected < 0 || inventory.mSelected >= 9) {
            releaseMining(player);
            return;
        }
        MiningTargetKey const target{hit.mBlock.x, hit.mBlock.y, hit.mBlock.z,
            static_cast<int>(player.getDimensionId()), worldGeneration.load()};
        std::uint64_t const now = GetTickCount64();
        if (mining.hasTarget && !(mining.target == target)) releaseMining(player);
        if (!mining.observe(target, now, static_cast<std::uint32_t>(s->autoTool.cooldownMs))) return;
        auto const& block = source.getBlock(hit.mBlock);
        std::array<MiningCandidate, 9> candidates{};
        for (int slot = 0; slot < 9; ++slot) {
            auto const& item = player.getInventory().getItem(slot);
            if (item.isNull() || !item.mItem || !previewItemWithinBudget(item)) continue;
            auto& candidate = candidates[slot];
            candidate.empty = false;
            candidate.speed = item.mItem->getDestroySpeed(item, block);
            int const maximum = item.mItem->getMaxDamage();
            int const damage = item.getDamageValue();
            candidate.maximum = maximum;
            candidate.remaining = maximum > 0 ? std::max(0, maximum - damage) : 0;
            candidate.protectedItem = maximum > 0
                && (candidate.remaining <= 0
                    || (s->on(Feature::ToolProtection)
                        && toolAtThreshold(maximum, damage, s->toolProtection)));
        }
        int const current = inventory.mSelected;
        int const chosen = chooseMiningTool(candidates, current, s->autoTool);
        if (chosen < 0 || chosen == current) return;
        auto const generation = worldGeneration.load();
        inventory.selectSlot(chosen, ContainerID::Inventory);
        if (inventory.mSelected != chosen) return;
        mining.previousSlot = s->autoTool.switchBack ? current : -1;
        mining.selectedSlot = chosen;
        mining.generation = generation;
        mining.lastSwitchMs = now;
    } catch (...) {}
}
void releaseToggle(LocalPlayer& player) {
    try {
        if (toggles.appliedSneak && player.isSneaking()) player.setSneaking(false);
        if (toggles.appliedSprint && player.isSprinting()) player.setSprinting(false);
    } catch (...) {}
    toggles.appliedSneak = toggles.appliedSprint = false;
}
void toggleTick(LocalPlayer& player) {
    try {
        auto s = settings();
        if (!s->on(Feature::ToggleSneakSprint) || !gameplayAllowed()) {
            releaseToggle(player);
            toggles.clear();
            return;
        }
        auto const& o = s->toggleSneakSprint;
        // A swallowed key-up (focus changes or other listeners) must not leave
        // the edge detector permanently latched. Win32 state is only sampled.
        toggles.releaseIfPhysicallyUp(o.sneakToggleKey,
            (GetAsyncKeyState(o.sneakToggleKey) & 0x8000) != 0);
        toggles.releaseIfPhysicallyUp(o.sprintToggleKey,
            (GetAsyncKeyState(o.sprintToggleKey) & 0x8000) != 0);
        if (o.sneakEnabled) {
            if (toggles.sneak && !player.isSneaking()) {
                player.setSneaking(true);
                toggles.appliedSneak = true;
            } else if (!toggles.sneak && toggles.appliedSneak && player.isSneaking()) {
                player.setSneaking(false);
                toggles.appliedSneak = false;
            }
        } else if (toggles.appliedSneak) {
            if (player.isSneaking()) player.setSneaking(false);
            toggles.appliedSneak = false;
        }
        if (o.sprintEnabled) {
            if (toggles.sprint && !player.isSprinting()) {
                player.setSprinting(true);
                toggles.appliedSprint = true;
            } else if (!toggles.sprint && toggles.appliedSprint && player.isSprinting()) {
                player.setSprinting(false);
                toggles.appliedSprint = false;
            }
        } else if (toggles.appliedSprint) {
            if (player.isSprinting()) player.setSprinting(false);
            toggles.appliedSprint = false;
        }
    } catch (...) {}
}
void recordPlayerDeath(LocalPlayer& local) {
    try {
        auto const& position = local.getPosition();
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        map::DeathHistoryEntry entry;
        entry.unixSeconds = static_cast<std::uint64_t>(seconds);
        entry.dimension = static_cast<int>(local.getDimensionId());
        entry.x = position.x;
        entry.y = position.y;
        entry.z = position.z;
        auto session = map::captureSession(local, worldGeneration.load());
        (void)recordDeath(session, entry);
    } catch (...) {}
}
void tick() {
    if (!active.load(std::memory_order_acquire) || !runtimeReady.load(std::memory_order_acquire)) return;
    std::lock_guard lock(stateMutex);
    auto client = ll::service::getClientInstance();
    if (!client) { leftDown = false; mining.clear(); toggles.clear(); aliveKnown = false; return; }
    auto* player = client->getLocalPlayer();
    if (!player) { leftDown = false; mining.clear(); toggles.clear(); aliveKnown = false; return; }
    auto const generation = worldGeneration.load(std::memory_order_acquire);
    if (observedGeneration != generation) {
        observedGeneration = generation;
        mining.clear();
        toggles.clear();
        leftDown = false;
        aliveKnown = false;
    }
    bool const alive = player->isAlive();
    if (aliveKnown && previousAlive && !alive) recordPlayerDeath(*player);
    previousAlive = alive;
    aliveKnown = true;
    autoToolTick(*client, *player);
    toggleTick(*player);
    // AngleSnap's rotational step snapping has no exported, safe writable
    // rotation channel in the pinned LeviLamina SDK. Only the public writable
    // mouse-input axis locks are applied; the limitation is reported in F10.
}
void mouse(ll::event::MouseInputEvent& event) {
    std::lock_guard lock(stateMutex);
    if (event.actionButtonId() == MouseAction::ActionLeft)
        leftDown = event.buttonData() == MouseAction::DataDown;
    if (event.actionButtonId() == MouseAction::ActionMoveRelative && !event.isCancelled()) {
        auto const s = settings();
        if (s->on(Feature::AngleSnap) && gameplayAllowed())
            applyAngleLocks(event.dx(), event.dy(), s->angleSnap);
    }
}
void key(ll::event::KeyInputEvent& event) {
    auto const s = settings();
    if (!s->on(Feature::ToggleSneakSprint)) return;
    int const code = event.keyCode();
    if (event.isDown()) {
        if (code == VK_SHIFT || code == VK_LSHIFT || code == VK_RSHIFT) {
            std::lock_guard lock(stateMutex);
            toggles.manualOverride(true, false);
        } else if (code == VK_CONTROL || code == VK_LCONTROL || code == VK_RCONTROL) {
            std::lock_guard lock(stateMutex);
            toggles.manualOverride(false, true);
        }
    }
    if (!event.isDown()) {
        // Preserve key-up edges even if a GUI consumed the key-down event.
        std::lock_guard lock(stateMutex);
        toggles.consumeKeyEdge(code, false, GetTickCount64());
        return;
    }
    if (event.isCancelled() || !gameplayAllowed()) return;
    auto const& o = s->toggleSneakSprint;
    auto const modifiers = currentModifiers();
    std::lock_guard lock(stateMutex);
    if (o.sneakEnabled && code == o.sneakToggleKey && modifiersMatch(o.sneakModifiers, modifiers)) {
        if (toggles.consumeKeyEdge(code, true, GetTickCount64()))
            toggles.sneak = !toggles.sneak;
        event.cancel();
    } else if (o.sprintEnabled && code == o.sprintToggleKey && modifiersMatch(o.sprintModifiers, modifiers)) {
        if (toggles.consumeKeyEdge(code, true, GetTickCount64()))
            toggles.sprint = !toggles.sprint;
        event.cancel();
    }
}
void worldExit(ll::event::ClientExitLevelEvent&) {
    std::lock_guard lock(stateMutex);
    mining.clear();
    toggles.clear();
    leftDown = false;
    aliveKnown = false;
    observedGeneration = 0;
    resetDeathLog();
}
void playerDied(ll::event::PlayerDieEvent& event) {
    try {
        auto client = ll::service::getClientInstance();
        if (!client) return;
        auto* local = client->getLocalPlayer();
        if (!local || static_cast<Player*>(local) != &event.self()) return;
        recordPlayerDeath(*local);
    } catch (...) {}
}
} // namespace

bool startGameplayRuntime() {
    if (active.exchange(true)) return true;
    try {
        auto& bus = ll::event::EventBus::getInstance();
        listeners[0] = bus.emplaceListener<ll::event::ClientLevelTickEvent>(
            [](ll::event::ClientLevelTickEvent&) { try { tick(); } catch (...) {} });
        listeners[1] = bus.emplaceListener<ll::event::MouseInputEvent>(
            [](ll::event::MouseInputEvent& event) { try { mouse(event); } catch (...) {} });
        listeners[2] = bus.emplaceListener<ll::event::KeyInputEvent>(
            [](ll::event::KeyInputEvent& event) { try { key(event); } catch (...) {} });
        listeners[3] = bus.emplaceListener<ll::event::ClientExitLevelEvent>(
            [](ll::event::ClientExitLevelEvent& event) { try { worldExit(event); } catch (...) {} });
        listeners[4] = bus.emplaceListener<ll::event::PlayerDieEvent>(
            [](ll::event::PlayerDieEvent& event) { try { playerDied(event); } catch (...) {} });
        for (auto const& listener : listeners)
            if (!listener) { stopGameplayRuntime(); return false; }
        return true;
    } catch (...) {
        active = false;
        return false;
    }
}

bool stopGameplayRuntime() noexcept {
    active = false;
    bool ok = true;
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : listeners) {
        if (!listener) continue;
        if (bus.removeListener(listener)) listener.reset();
        else ok = false;
    }
    std::lock_guard lock(stateMutex);
    mining.clear();
    toggles.clear();
    leftDown = false;
    observedGeneration = 0;
    return ok;
}

void resetGameplayRuntime() noexcept {
    std::lock_guard lock(stateMutex);
    mining.clear();
    toggles.clear();
    leftDown = false;
    observedGeneration = 0;
}
} // namespace praxis::companion
