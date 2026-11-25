#include "platform/QuickTradeRuntime.h"
#include "core/ItemScrollerPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "platform/InventoryRequest.h"
#include "platform/InventorySnapshot.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/TradeScreenController.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/controllers/TradeContainerManagerController.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace praxis::companion {
namespace {
std::mutex quickMutex;
QuickTradeOptions options{false, true, 1, 60000};
QuickTradeSession session{options};
ConfirmationState confirmation;
std::string message{"QuickTrade disabled (session opt-in required)"};
std::string planIssue{"Trade has not been checked"};
std::weak_ptr<ContainerScreenController> ownerScreen;
std::uintptr_t ownerView{};
std::uint64_t pendingRequest{}, policyToken{};
bool keyHeld{};

struct TradePlan {
    SlotData source;
    SlotData destination;
    QuickTradeContext context;
    std::string expectedKey;
    int priorCount{};
    int amount{};
};
bool focused() noexcept {
    DWORD pid{};
    auto window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &pid)
        && pid == GetCurrentProcessId()
        && runtimeReady.load(std::memory_order_acquire) && !guiInputCaptured();
}
bool selectedCursorIdle(ContainerScreenController const& screen,
                        ContainerManagerController& manager) {
    auto const& selected = screen.mSelectedSlotData.get();
    if (!selected.mCollectionName.get().empty() || selected.mCollectionIndex >= 0
        || screen.mSplitDraggingToPlace || screen.mStartDeciding
        || !screen.mDraggingCollectionName.get().empty()) return false;
    for (auto const& [name, controller] : manager.mContainers.get())
        if (name.find("cursor") != std::string::npos) {
            if (manager.getContainerSize(name) != 1) return false;
            return inventory::stackValue(manager.getItemStack(name, 0)).count == 0;
        }
    return false; // Unknown cursor collection => do not issue a transaction.
}
std::uint64_t mix(std::uint64_t h, std::string const& s) noexcept {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    h ^= 0xff; return h * 1099511628211ull;
}
std::uint64_t offerFingerprint(MerchantRecipe const& r, int index,
                               std::string const& outputKey) {
    std::uint64_t h = 14695981039346656037ull;
    h = mix(h, r.mSell.get().getTypeName());
    h = mix(h, r.mBuyA.get().getTypeName());
    h = mix(h, r.mBuyB.get().getTypeName());
    h = mix(h, outputKey);
    h = mix(h, std::to_string(index));
    h = mix(h, std::to_string(r.mBuyCountA));
    h = mix(h, std::to_string(r.mBuyCountB));
    return h ? h : 1;
}
std::optional<TradePlan> plan(
    std::shared_ptr<ContainerScreenController> const& screen,
    std::shared_ptr<ContainerManagerController> const& manager,
    std::uintptr_t view, bool tradeView) {
    planIssue = "Trade not ready";
    if (!screen || !manager || !view || !focused()
        || !inventory::inventoryFocus(false)) {
        planIssue = "Not in a focused trade screen";
        return {};
    }
    if (manager->mContainersClosed) {
        planIssue = "The trading container is closing";
        return {};
    }
    if (inventory::requestPathBusy()) {
        planIssue = "Another inventory request is still pending";
        return {};
    }
    // The caller already classified this screen as a trade container from the
    // native container type. Use the static cast: RTTI on a screen that the
    // resource-pack/world update is replacing can abort in dynamic_cast
    // (trace_2026-11-24_00-23-40.log: QuickTradeRuntime.cpp:99).
    if (!tradeView) {
        planIssue = "Not a focused trade screen";
        return {};
    }
    auto tradeScreen = std::static_pointer_cast<TradeScreenController>(screen);
    if (!tradeScreen || !tradeScreen->mTradeContainerManagerController) {
        planIssue = "No active Bedrock trading controller";
        return {};
    }
    auto& trade = *tradeScreen->mTradeContainerManagerController;
    // Bedrock keeps a distinct screen-side ContainerManagerController and a
    // trade-specific TradeContainerManagerController for the same screen
    // (observed: trade=..3F00 screen=..3F10, both ContainerType::Trade). Require
    // the semantic contract instead of pointer identity: the displayed manager
    // must still be a trade container and must expose the trade output slot.
    if (manager->getContainerType() != SharedTypes::Legacy::ContainerType::Trade) {
        planIssue = "The displayed container is not a trade container";
        return {};
    }
    auto* recipe = trade.mResultPreviewRecipe;
    if (!recipe || trade.mCurrentRecipeIndex < 0) {
        planIssue = "No selected, available villager offer";
        return {};
    }
    if (!selectedCursorIdle(*screen, *manager)) {
        planIssue = "Cursor is occupied or could not be safely verified";
        return {};
    }

    SlotData const source = trade.mCreatedItemOutputSlot.get();
    auto const& collection = source.mCollectionName.get();
    if (collection.empty() || !trade.isOutputSlot(collection)
        || !manager->hasContainerController(collection)
        || source.mCollectionIndex < 0
        || source.mCollectionIndex >= manager->getContainerSize(collection)) {
        planIssue = "Trade result slot not exposed by the controller";
        return {};
    }
    auto const& result = manager->getItemStack(collection, source.mCollectionIndex);
    auto const output = inventory::stackValue(result);
    if (output.count <= 0) {
        planIssue = "Trade output is empty; select an affordable offer";
        return {};
    }
    if (output.count > 64 || output.locked
        || output.name != recipe->mSell.get().getTypeName()) {
        planIssue = "Trade output differs from selected recipe";
        return {};
    }

    // Use the game's own 36 player inventory destinations, not container slot guesses.
    for (auto const& candidate : {std::string("hotbar_items"), std::string("inventory_items")}) {
        if (!manager->hasContainerController(candidate)) continue;
        int const size = manager->getContainerSize(candidate);
        if (size != 9 && size != 27 && size != 36) continue;
        for (int i = 0; i < size; ++i) {
            auto const& destStack = manager->getItemStack(candidate, i);
            auto const dest = inventory::stackValue(destStack);
            if (dest.locked || dest.count < 0) continue;
            if (dest.count && (dest.key != output.key || dest.count + output.count > dest.maximum)) continue;
            return TradePlan{
                source,
                SlotData(candidate, i),
                QuickTradeContext{
                    worldGeneration.load(std::memory_order_acquire),
                    offerFingerprint(*recipe, trade.mCurrentRecipeIndex, output.key),
                    trade.mCurrentRecipeIndex,
                    true, true, true, true, false
                },
                output.key, dest.count, output.count
            };
        }
    }
    planIssue = "No unlocked inventory slot can hold this trade result";
    return {};
}
void halt(std::string reason) noexcept {
    if (pendingRequest) inventory::releaseRequest(pendingRequest);
    pendingRequest = policyToken = 0;
    session.cancel();
    confirmation.clear();
    ownerView = 0;
    ownerScreen.reset();
    message = std::move(reason);
}
bool correctOwner(std::shared_ptr<ContainerScreenController> const& screen, std::uintptr_t view) {
    return view && ownerView == view && ownerScreen.lock() == screen;
}
std::string expectedCollection, expectedKey;
int expectedIndex{-1}, priorCount{}, expectedAmount{};
void handleExistingRequest(ContainerManagerController& manager, std::uint64_t now) {
    if (!pendingRequest) return;
    auto const result = inventory::requestResult(pendingRequest);
    if (result == inventory::RequestResult::Waiting) {
        if (!session.tick(now)) halt("QuickTrade timed out waiting for server");
        return;
    }
    bool accepted = result == inventory::RequestResult::Accepted;
    inventory::releaseRequest(pendingRequest);
    pendingRequest = 0;
    if (accepted) {
        // The trusted server's ItemStackResponse has already been applied by vanilla.
        // Do not count a grant as a trade if its predicted destination stayed unchanged.
        accepted = manager.hasContainerController(expectedCollection)
            && inventory::stackValue(manager.getItemStack(expectedCollection, expectedIndex)).key == expectedKey
            && inventory::stackValue(manager.getItemStack(expectedCollection, expectedIndex)).count
                >= priorCount + expectedAmount;
    }
    if (!session.acknowledge(worldGeneration.load(), policyToken, now, accepted)) {
        halt("QuickTrade stopped: rejected or unverified server response");
        return;
    }
    policyToken = 0;
    message = "Server-confirmed trades: " + std::to_string(session.confirmedTrades());
}
} // namespace

QuickTradeOptions quickTradeOptions() {
    std::lock_guard lock(quickMutex);
    return options;
}
bool configureQuickTrade(QuickTradeOptions next) {
    if (!validQuickTradeOptions(next)) return false;
    std::lock_guard lock(quickMutex);
    if (!(options == next)) {
        halt("QuickTrade configuration changed; re-activate manually");
        options = next;
        session.configure(options);
        message = options.enabled ? "QuickTrade armed. Open a villager, press Ctrl+Shift+K twice."
                                  : "QuickTrade disabled";
    }
    return true;
}
std::string quickTradeStatus() {
    std::lock_guard lock(quickMutex);
    return message;
}
void quickTradeReset() noexcept {
    std::lock_guard lock(quickMutex);
    halt("QuickTrade context closed/reset");
    keyHeld = false;
}
void quickTradeFrame(std::shared_ptr<ContainerScreenController> const& screen,
                     std::shared_ptr<ContainerManagerController> const& manager,
                     std::uintptr_t view, bool tradeView, bool competingJob) {
    std::lock_guard lock(quickMutex);
    bool const down = (GetAsyncKeyState('K') & 0x8000) != 0
        && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
        && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0
        && (GetAsyncKeyState(VK_MENU) & 0x8000) == 0;
    bool const edge = down && !keyHeld;
    keyHeld = down;
    if (!options.enabled) return;
    bool const eligible = tradeView && !competingJob && focused() && screen && manager && view;
    if (!eligible) {
        confirmation.clear();
        if (session.active()) halt("QuickTrade stopped: screen/focus/context lost");
        else if (edge) message = tradeView
            ? (competingJob ? "QuickTrade blocked by ItemScroller operation"
                            : "QuickTrade requires a focused, valid trade container")
            : "QuickTrade requires an open villager trading screen";
        return;
    }
    if (session.active() && !correctOwner(screen, view)) {
        halt("QuickTrade stopped: villager screen changed");
        return;
    }
    auto const now = GetTickCount64();
    if (session.active() && pendingRequest) {
        handleExistingRequest(*manager, now);
        if (pendingRequest || !session.active()) return;
    }
    // Query native inventory/trade data only on an activation edge or for an
    // active session; preserve the last useful status for F10 diagnostics.
    if (!session.active() && !edge) return;
    auto current = plan(screen, manager, view, tradeView);
    if (session.active()) {
        if (!current || !session.observe(current->context, now)) {
            halt("QuickTrade stopped: " + planIssue);
            return;
        }
    } else {
        if (!current) { message = "QuickTrade not ready: " + planIssue; return; }
        if (options.requireConfirmation && !confirmation.press(now)) {
            message = "Press Ctrl+Shift+K again within five seconds to confirm";
            return;
        }
        confirmation.clear();
        if (!session.begin(current->context, now, true)) {
            message = "QuickTrade refused to start";
            return;
        }
        ownerScreen = screen;
        ownerView = view;
    }
    if (!session.active()) return;
    if (!current || !session.observe(current->context, now)) {
        halt("QuickTrade offer no longer valid");
        return;
    }
    // Each authorization wraps exactly one vanilla container-controller action.
    auto request = inventory::beginRequest(*manager);
    if (!request) { halt("QuickTrade cannot reserve inventory response barrier"); return; }
    if (!session.beginTrade(now)) {
        inventory::releaseRequest(request);
        halt("QuickTrade trade budget or deadline exceeded");
        return;
    }
    policyToken = session.outstandingToken();
    expectedCollection = current->destination.mCollectionName.get();
    expectedIndex = current->destination.mCollectionIndex;
    expectedKey = current->expectedKey;
    priorCount = current->priorCount;
    expectedAmount = current->amount;
    pendingRequest = request;
    bool const sent = manager->handleTakeAll(current->destination, current->source);
    inventory::finishRequest(*manager, request);
    if (!sent) { halt("QuickTrade vanilla controller rejected take-all"); return; }
    message = "QuickTrade awaiting authoritative server response";
}
} // namespace praxis::companion
