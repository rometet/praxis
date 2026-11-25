#include "platform/PickBlockRuntime.h"
#include "core/HotkeyPolicy.h"
#include "core/PickBlockPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemLockHelper.h"
#include "mc/world/item/ItemLockMode.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace praxis::companion {
namespace {
std::array<ll::event::ListenerPtr, 1> listeners;
std::atomic_bool installed{};
std::mutex mutex;

std::atomic<unsigned> boundKey{};          // 0 = unbound
std::atomic<unsigned> boundModifiers{anyModifiers};
std::atomic_bool featureEnabled{};
std::atomic<std::uint64_t> lastPickMs{};
std::atomic<unsigned> selectCount{};
std::atomic<unsigned> swapDeferred{};
std::atomic<unsigned> unsupportedCount{};
bool requireKeyRelease{true};
std::array<bool, 256> previousDown{};

bool foregroundOwned() noexcept {
    DWORD pid{};
    auto hwnd = GetForegroundWindow();
    return hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid == GetCurrentProcessId();
}

unsigned currentModifiers() noexcept {
    return modifierBits((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
                        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0,
                        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
}

// Same read path as InventorySnapshot.cpp (player.getInventory().getItem(slot)).
PickSlot readSlot(LocalPlayer const& player, int slot) {
    ItemStack const& item = player.getInventory().getItem(slot);
    if (item.isNull() || item.mCount == 0) return {};
    bool locked = false;
    if (item.mItem) locked = ItemLockHelper::getItemLockMode(item) != ItemLockMode::None;
    return pickSlotFromStack(item.getTypeName(), int(item.mCount), int(item.getAuxValue()), locked);
}

void logPick(char const* format, int a, int b, std::string const& name) {
    char text[320]{};
    std::snprintf(text, sizeof(text), format, a, b, name.c_str());
    logGuiInfo(text);
}
} // namespace

bool configurePickBlock(unsigned key, unsigned modifiers, bool enabled) {
    if (key > 255 || !validModifiers(modifiers)) return false;
    std::lock_guard lock(mutex);
    boundKey.store(key, std::memory_order_release);
    boundModifiers.store(modifiers, std::memory_order_release);
    featureEnabled.store(enabled && key != 0, std::memory_order_release);
    requireKeyRelease = true;
    char config[128]{};
    std::snprintf(config, sizeof(config), "PICKBLOCK_CONFIG key=%u modifiers=%u enabled=%u",
                  key, modifiers, unsigned(featureEnabled.load()));
    logGuiInfo(config);
    return true;
}

std::string pickBlockStatus() {
    std::lock_guard lock(mutex);
    auto const current = settings();
    int const key = current ? current->pickBlockKey : 0;
    char text[256]{};
    std::snprintf(text, sizeof(text),
                  "PickBlock %s key=%d mods=%u selected=%u swapDeferred=%u unsupported=%u",
                  key ? "enabled" : "disabled (bind a key)", key,
                  current ? current->pickBlockModifiers : anyModifiers, selectCount.load(),
                  swapDeferred.load(), unsupportedCount.load());
    return text;
}

bool tryPickBlock() {
    auto client = ll::service::getClientInstance();
    if (!client) return false;
    LocalPlayer* player = client->getLocalPlayer();
    if (!player || !player->mInventory) return false;

    // 1) crosshair: the exact expression FastBlockPlacement/FlexiblePlacement use.
    auto const& hit = client->getLatestHitResult();
    if (!hit || hit.mType != HitResultType::Tile) return false;

    // 2) block -> item: identity on the type name. No table, no guessing.
    auto const& region = player->getDimensionBlockSource();
    auto const& block = region.getBlock(hit.mBlock);
    if (block.isAir()) return false;
    std::string const blockName = block.getBlockType().getTypeName();
    if (blockName.empty()) return false;

    // 3) throttle: one attempt per 200 ms.
    std::uint64_t const now = GetTickCount64();
    std::uint64_t const last = lastPickMs.load(std::memory_order_relaxed);
    if (last && now >= last && now - last < 200) return false;
    lastPickMs.store(now, std::memory_order_relaxed);

    // 4) snapshot -> plan (pure policy)
    std::vector<PickSlot> slots;
    slots.reserve(pickInventorySlots + 1);
    for (int slot = 0; slot < pickInventorySlots; ++slot) slots.push_back(readSlot(*player, slot));
    slots.push_back({});  // offhand: stage D
    PickOptions options;
    int selectedItem{-1};
    if (resolveSelectedHotbar(int(player->getSelectedItemSlot()), options.selectedHotbar))
        selectedItem = options.selectedHotbar;
    PickAction const action = planPick(slots, PickRequest{blockName, 0}, options);

    switch (action.kind) {
    case PickActionKind::None:
        ++unsupportedCount;
        logPick("PICKBLOCK_NONE selected=%d slot=%d item=%.120s", selectedItem, -1, blockName);
        return false;
    case PickActionKind::SelectHotbar: {
        if (pickActionTouchesSelected(action, selectedItem)) return false;  // already holding it
        if (action.slot < 0 || action.slot >= pickHotbarSlots) return false;
        ItemStack const& item = player->getInventory().getItem(action.slot);
        if (item.isNull() || item.mCount == 0) return false;
        // In-game proof (Be.D.D., 2026-11-24): Player::setSelectedItem(const ItemStack&) const
        // is a read/compare API -- it returns void but leaves the selected slot unchanged.
        // The writable field the client itself uses is PlayerInventory::mSelected, which
        // InventorySnapshot.cpp already reads; write it and let the next tick sync.
        if (!player->mInventory || player->mInventory->mSelectedContainerId != ContainerID::Inventory)
            return false;
        player->mInventory->mSelected = action.slot;
        ++selectCount;
        logPick("PICKBLOCK_SELECT slot=%d selected=%d item=%.120s", action.slot, selectedItem,
                blockName);
        return true;
    }
    case PickActionKind::SwapIntoHotbar:
        // Stage D owns the inventory Job; say so instead of doing nothing silently.
        ++swapDeferred;
        logPick("PICKBLOCK_SWAP_DEFERRED source=%d selected=%d item=%.120s", action.slot,
                selectedItem, blockName);
        return false;
    }
    return false;
}

namespace {
void tick() {
    std::lock_guard lock(mutex);
    if (!installed.load(std::memory_order_acquire)) return;
    // Settings are the source of truth: the GUI edits config, the runtime just polls.
    auto const current = settings();
    unsigned const key = current ? unsigned(current->pickBlockKey) : 0u;
    if (!key || key > 255) return;
    if (!foregroundOwned()) { requireKeyRelease = true; return; }
    bool const down = (GetAsyncKeyState(int(key)) & 0x8000) != 0;
    bool const allowed = modifiersMatch(current ? current->pickBlockModifiers : anyModifiers,
                                        currentModifiers());
    bool const rising = down && !previousDown[key];
    previousDown[key] = down;
    if (!down) { requireKeyRelease = false; return; }
    if (!rising || !allowed || requireKeyRelease) return;
    requireKeyRelease = true;  // one pick per press
    try {
        tryPickBlock();
    } catch (...) {
            logGuiInfo("PICKBLOCK_ERROR swallowed inside tick");
    }
}
} // namespace

bool startPickBlockRuntime() {
    if (installed.exchange(true)) return true;
    try {
        auto& bus = ll::event::EventBus::getInstance();
        listeners[0] = bus.emplaceListener<ll::event::ClientLevelTickEvent>(
            [](ll::event::ClientLevelTickEvent&) { try { tick(); } catch (...) {} });
        if (listeners[0]) {
            logGuiInfo("PICKBLOCK_RUNTIME_READY selection=PlayerInventory::mSelected distance_check=disabled");
            return true;
        }
    } catch (...) {
    }
    stopPickBlockRuntime();
    return false;
}

bool stopPickBlockRuntime() noexcept {
    installed.store(false);
    for (auto& listener : listeners) {
        if (listener) {
            ll::event::EventBus::getInstance().removeListener(listener);
            listener.reset();
        }
    }
    return true;
}
} // namespace praxis::companion
