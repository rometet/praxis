#include "platform/ItemScrollerExtras.h"
#include "core/InventoryActionPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "platform/InventoryRequest.h"
#include "platform/InventorySnapshot.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/TradeScreenController.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/controllers/TradeContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace praxis::companion {
namespace {
using namespace inventory;
struct CollectionInfo {
    std::string name;
    int size{};
};
struct Context {
    std::shared_ptr<ContainerScreenController> screen;
    std::shared_ptr<ContainerManagerController> manager;
    std::uintptr_t view{};
    SharedTypes::Legacy::ContainerType type{};
    std::vector<CollectionInfo> collections;
    std::string grid;
    int gridSize{};
    std::string output;
};
struct Job {
    std::weak_ptr<ContainerScreenController> screen;
    std::uintptr_t view{};
    std::uint64_t generation{};
    std::string grid;
    int gridSize{};
    std::vector<RecipeMove> moves;
    std::vector<std::string> wanted;
    std::size_t index{};
    std::uint64_t token{}, started{}, lastStep{};
};
std::recursive_mutex mutex;
std::atomic<std::shared_ptr<ItemScrollerObservation const>> observation;
std::atomic_bool resetRequested{};
std::string status{"Not started"};
ll::event::ListenerPtr listener;
InventoryActionEdge applyEdge;
ConfirmationState confirmation;
std::shared_ptr<Job> job;
bool applyKeyEligible(ItemScrollerOptions const&, std::shared_ptr<ContainerScreenController> const& screen,
                      std::uintptr_t view);
void beginJob(Context const& c, std::vector<RecipeMove> moves, std::vector<std::string> wanted);

bool foreground() {
    DWORD process{};
    auto window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}
std::vector<CollectionInfo> collections(ContainerManagerController& manager) {
    std::vector<CollectionInfo> result;
    for (auto const& [name, controller] : manager.mContainers.get())
        result.push_back({name, manager.getContainerSize(name)});
    return result;
}
bool excluded(std::string const& name) {
    return name == "inventory_items" || name == "hotbar_items" || name == "armor_items"
        || name == "offhand_items" || name.find("cursor") != std::string::npos
        || name.find("armor") != std::string::npos || name.find("offhand") != std::string::npos;
}
std::optional<Context> context(ll::event::AfterUIRenderEvent& event) {
    auto& view = event.screenView();
    auto& client = event.uiRenderContext().mClient;
    if (!inventoryFocus(false) || !view.mHasFocus || view.mIsEntering || view.mIsExiting || view.mIsTerminating
        || view.getScreenName() != client.getTopScreenName())
        return {};
    std::shared_ptr<ScreenController> base = view.mController;
    if (!base || !base->_isContainerScreen()) return {};
    auto screen = std::static_pointer_cast<ContainerScreenController>(base);
    auto manager = screen->mContainerManagerController;
    auto player = client.getLocalPlayer();
    if (!manager || !player || manager->mContainersClosed) return {};
    auto model = manager->mContainerManagerModel.lock();
    if (!model || &model->mPlayer != player) return {};
    auto const type = manager->getContainerType();
    if (type != SharedTypes::Legacy::ContainerType::Inventory
        && type != SharedTypes::Legacy::ContainerType::Workbench
        && type != SharedTypes::Legacy::ContainerType::Trade)
        return {};
    Context result;
    result.screen = screen;
    result.manager = manager;
    result.view = reinterpret_cast<std::uintptr_t>(&view);
    result.type = type;
    result.collections = collections(*manager);
    if (type != SharedTypes::Legacy::ContainerType::Trade) {
        for (auto const& value : result.collections) {
            if (excluded(value.name) || (value.size != 4 && value.size != 9)) continue;
            bool const named = value.name.find("craft") != std::string::npos
                || value.name.find("workbench") != std::string::npos
                || value.name.find("grid") != std::string::npos;
            if (named || (result.grid.empty() && value.size == 9)) {
                result.grid = value.name;
                result.gridSize = value.size;
                if (named) break;
            }
        }
        if (result.grid.empty())
            for (auto const& value : result.collections)
                if (!excluded(value.name) && value.size == 4) {
                    result.grid = value.name;
                    result.gridSize = 4;
                    break;
                }
        for (auto const& value : result.collections) {
            if (value.size != 1 || excluded(value.name)) continue;
            if (value.name.find("output") != std::string::npos
                || value.name.find("result") != std::string::npos) {
                result.output = value.name;
                break;
            }
        }
    }
    return result;
}
bool playerSlots(ContainerManagerController& manager, std::vector<SlotData>& slots) {
    if (!manager.hasContainerController("inventory_items")) return false;
    int const inventorySize = manager.getContainerSize("inventory_items");
    slots.clear();
    if (inventorySize == 36) {
        for (int i = 0; i < 36; ++i) slots.emplace_back("inventory_items", i);
        return true;
    }
    if (inventorySize != 27 || !manager.hasContainerController("hotbar_items")
        || manager.getContainerSize("hotbar_items") != 9)
        return false;
    for (int i = 0; i < 9; ++i) slots.emplace_back("hotbar_items", i);
    for (int i = 0; i < 27; ++i) slots.emplace_back("inventory_items", i);
    return true;
}
struct PlayerValues {
    std::array<std::string, 36> keys{};
    std::array<int, 36> counts{};
    std::vector<SlotData> slots;
};
bool readPlayer(ContainerManagerController& manager, PlayerValues& values) {
    return playerSlots(manager, values.slots)
        && values.slots.size() == 36
        && [&] {
            for (int i = 0; i < 36; ++i) {
                auto value = stackValue(manager.getItemStack(values.slots[i].mCollectionName.get(),
                    values.slots[i].mCollectionIndex));
                values.keys[i] = value.key;
                values.counts[i] = value.count;
            }
            return true;
        }();
}
void publish(std::shared_ptr<ItemScrollerObservation const> next) {
    observation.store(std::move(next), std::memory_order_release);
}
void observe(ContainerManagerController& manager, Context const& c) {
    auto next = std::make_shared<ItemScrollerObservation>();
    next->generation = worldGeneration.load();
    next->source = c.grid.empty() ? "trade" : "crafting";
    if (!c.grid.empty()) {
        next->recipeValid = true;
        for (int i = 0; i < c.gridSize && i < static_cast<int>(recipeGridSize); ++i) {
            auto value = stackValue(manager.getItemStack(c.grid, i));
            next->grid[i] = value.key;
            next->locked[i] = value.count > 0;
        }
        if (!c.output.empty()) {
            auto value = stackValue(manager.getItemStack(c.output, 0));
            next->result = value.name.empty() ? "crafting" : value.name;
        } else {
            next->result = "crafting";
        }
    }
    // Trade preview is plain UI observation. No server transaction is created.
    auto base = std::dynamic_pointer_cast<TradeScreenController>(c.screen);
    if (base && base->mTradeContainerManagerController) {
        auto& trade = *base->mTradeContainerManagerController;
        if (auto* recipe = trade.mResultPreviewRecipe) {
            next->tradeValid = true;
            next->tradeSell = recipe->mSell.get().getTypeName();
            next->tradeBuyA = recipe->mBuyA.get().getTypeName();
            next->tradeBuyB = recipe->mBuyB.get().getTypeName();
            next->tradeCountA = recipe->mBuyCountA;
            next->tradeCountB = recipe->mBuyCountB;
            next->tradeIndex = trade.mCurrentRecipeIndex;
        }
    }
    publish(std::move(next));
}
bool cursorIdle(ContainerManagerController& manager, Context const& c) {
    if (!c.screen->mSelectedSlotData.get().mCollectionName.get().empty()
        || c.screen->mSelectedSlotData.get().mCollectionIndex >= 0
        || c.screen->mSplitDraggingToPlace || c.screen->mStartDeciding
        || !c.screen->mDraggingCollectionName.get().empty())
        return false;
    for (auto const& value : c.collections)
        if (value.name.find("cursor") != std::string::npos) {
            if (value.size < 1) return false;
            return stackValue(manager.getItemStack(value.name, 0)).count == 0;
        }
    return false;
}
void cancel(std::string reason) {
    if (job && job->token) releaseRequest(job->token);
    job.reset();
    confirmation.clear();
    status = std::move(reason);
}
void stop(std::string const& reason) {
    cancel(reason);
}
bool gridSlotEmptyOrWanted(ContainerManagerController& manager, Context const& c, int slot,
                           std::string const& wanted) {
    auto value = stackValue(manager.getItemStack(c.grid, slot));
    return value.count == 0 || value.key == wanted;
}
void beginJob(Context const& c, std::vector<RecipeMove> moves, std::vector<std::string> wanted) {
    auto next = std::make_shared<Job>();
    next->screen = c.screen;
    next->view = c.view;
    next->generation = worldGeneration.load();
    next->grid = c.grid;
    next->gridSize = c.gridSize;
    next->moves = std::move(moves);
    next->wanted = std::move(wanted);
    next->started = GetTickCount64();
    job = std::move(next);
    status = "Applying recipe";
}
void tick(ContainerManagerController& manager, Context const& c) {
    if (!job) return;
    auto retained = job;
    auto& op = *retained;
    auto s = settings();
    if (!s->itemScroller.recipeFavorites) {
        stop("Recipe favorites disabled");
        return;
    }
    if (op.generation != worldGeneration.load()
        || op.screen.lock() != c.screen || op.view != c.view || op.grid != c.grid) {
        stop("Crafting screen or settings changed");
        return;
    }
    if (op.token) {
        auto const result = requestResult(op.token);
        if (result == RequestResult::Waiting) return;
        if (result != RequestResult::Accepted) {
            stop("Inventory request rejected, timed out or untracked");
            return;
        }
        releaseRequest(op.token);
        op.token = 0;
    }
    auto const now = GetTickCount64();
    if (now < op.started || now - op.started > 60000) {
        stop("Recipe apply deadline");
        return;
    }
    if (op.index == op.moves.size()) {
        cancel("Recipe applied");
        return;
    }
    if (now < op.lastStep || now - op.lastStep < 50) return;
    PlayerValues values;
    if (!readPlayer(manager, values)) {
        stop("Player inventory unavailable");
        return;
    }
    auto const& move = op.moves[op.index];
    auto const& wanted = op.wanted[op.index];
    if (!gridSlotEmptyOrWanted(manager, c, move.gridSlot, wanted)) {
        stop("Crafting grid changed");
        return;
    }
    int source = -1;
    for (int i = 0; i < 36; ++i)
        if (values.keys[i] == wanted && values.counts[i] > 0) { source = i; break; }
    if (source < 0) {
        stop("Recipe ingredient disappeared");
        return;
    }
    op.token = beginRequest(manager);
    if (!op.token) {
        stop("Inventory request path busy");
        return;
    }
    SlotData const sourceSlot = values.slots[source];
    SlotData const targetSlot(c.grid, move.gridSlot);
    bool const success = manager.handlePlaceAmount(sourceSlot, 1, targetSlot);
    finishRequest(manager, op.token);
    if (job != retained) return;
    if (!success) {
        stop("Vanilla crafting move failed");
        return;
    }
    ++op.index;
    op.lastStep = now;
}
void onFrame(ll::event::AfterUIRenderEvent& event) {
    std::lock_guard lock(mutex);
    if (resetRequested.exchange(false)) {
        if (job && job->token) releaseRequest(job->token);
        job.reset();
        applyEdge = {};
        confirmation.clear();
        observation.store(std::make_shared<ItemScrollerObservation>());
        status = "Not started";
        return;
    }
    auto c = context(event);
    if (!c) {
        if (job) stop("Container screen closed");
        applyEdge = {};
        return;
    }
    observe(*c->manager, *c);
    auto s = settings();
    if (!s->itemScroller.recipeFavorites && !s->itemScroller.tradeFavorites) return;
    if (job) {
        tick(*c->manager, *c);
        return;
    }
    auto const& options = s->itemScroller;
    if (!options.recipeFavorites || c->grid.empty()) {
        applyEdge = {};
        return;
    }
    auto const chord = HotkeyChord{static_cast<unsigned>(options.applyKey), options.applyModifiers, 4};
    auto const mods = modifierBits((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
    bool const eligible = applyKeyEligible(options, c->screen, c->view);
    bool const pressed = applyEdge.sample(chord,
        chord.key > 0 && (GetAsyncKeyState(static_cast<int>(chord.key)) & 0x8000) != 0,
        mods, eligible);
    if (!pressed) return;
    if (!cursorIdle(*c->manager, *c)) {
        status = "Cursor slot is not idle";
        return;
    }
    if (options.requireConfirmation && !confirmation.press(GetTickCount64())) {
        status = "Press the apply key again to confirm";
        return;
    }
    auto const& recipe = options.recipes[options.selectedRecipe];
    if (emptyRecipe(recipe)) {
        status = "No favorite recipe selected";
        return;
    }
    PlayerValues values;
    if (!readPlayer(*c->manager, values)) {
        status = "Player inventory unavailable";
        return;
    }
    std::array<std::string, recipeGridSize> gridItems{};
    std::array<bool, recipeGridSize> locked{};
    for (int i = 0; i < c->gridSize && i < static_cast<int>(recipeGridSize); ++i) {
        auto value = stackValue(c->manager->getItemStack(c->grid, i));
        gridItems[i] = value.key;
        locked[i] = value.count > 0;
    }
    auto moves = planRecipeApply(recipe, values.keys, values.counts, gridItems, locked,
        static_cast<std::size_t>(options.maxSteps));
    if (moves.empty()) {
        status = "Recipe cannot be applied (missing items or occupied grid)";
        return;
    }
    std::vector<std::string> wanted;
    wanted.reserve(moves.size());
    for (auto const& move : moves) wanted.push_back(recipe.grid[move.gridSlot]);
    beginJob(*c, std::move(moves), std::move(wanted));
}
bool applyKeyEligible(ItemScrollerOptions const&, std::shared_ptr<ContainerScreenController> const& screen,
                      std::uintptr_t view) {
    if (!runtimeReady.load() || guiInputCaptured() || !foreground()) return false;
    auto client = ll::service::getClientInstance();
    auto player = client ? client->getLocalPlayer() : nullptr;
    return player && player->isAlive() && !player->isCreative() && !player->isSpectator()
        && screen && view != 0;
}
} // namespace

bool installItemScrollerExtras() {
    if (listener) return true;
    try {
        listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent& event) {
                try { onFrame(event); } catch (...) { std::lock_guard lock(mutex); stop("ItemScroller UI exception"); }
            });
    } catch (...) {
        return false;
    }
    return bool(listener);
}
bool removeItemScrollerExtras() noexcept {
    resetItemScrollerExtras();
    {
        std::lock_guard lock(mutex);
        if (job && job->token) releaseRequest(job->token);
        job.reset();
        applyEdge = {};
        confirmation.clear();
        observation.store(std::make_shared<ItemScrollerObservation>());
        status = "Not started";
        resetRequested.store(false);
    }
    if (listener) {
        bool removed = ll::event::EventBus::getInstance().removeListener(listener);
        listener.reset();
        return removed;
    }
    return true;
}
void resetItemScrollerExtras() noexcept {
    resetRequested.store(true);
}
std::shared_ptr<ItemScrollerObservation const> itemScrollerObservation() noexcept {
    auto current = observation.load(std::memory_order_acquire);
    return current ? current : std::make_shared<ItemScrollerObservation>();
}
std::string itemScrollerStatus() {
    std::lock_guard lock(mutex);
    return status;
}
} // namespace praxis::companion
