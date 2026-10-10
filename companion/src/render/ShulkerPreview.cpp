#include "render/ShulkerPreview.h"
#include "render/NativeDurabilityHud.h"
#include "render/PlayerHud.h"
#include <Windows.h>
#include <imgui.h>
#include "core/Settings.h"
#include "core/ShulkerPreviewPolicy.h"
#include "core/ContainerPreviewPolicy.h"
#include "core/UiLayoutPolicy.h"
#include "core/NativePreviewClipScope.h"
#include "runtime/InformationRuntime.h"
#include "mc/client/gui/screens/controllers/BundleHelper.h"
#include "mc/world/item/Item.h"
#include "mc/world/containers/managers/models/HudContainerManagerModel.h"
#include "mc/world/containers/IContainerRegistryAccess.h"
#include "mc/world/containers/models/StorageItemContainerModel.h"
#include "mc/deps/nbt/Int64Tag.h"
#include "gui/GuiBridge.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HoverTextRenderer.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/ViewportInfo.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/containers/SlotData.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sstream>

namespace praxis::companion {
namespace {
struct OwnedSlot {
    // Publication is plain metadata only. Native ItemStack ownership stays on its UI thread.
    bool icon{};
    std::string name;
    std::uint8_t count{};
};
struct Snapshot {
    std::array<OwnedSlot,64> slots;
    PreviewFamily family{PreviewFamily::Shulker};
    PreviewGrid grid{9,3,27};
    ShulkerSlotViews slotViews{};
    std::string title;
    std::string itemType;
    std::string collection;
    int index{-1};
    std::uint64_t tagHash{},revision{};
    unsigned long long generation{};
    std::chrono::steady_clock::time_point observed{};
    bool valid{};
    bool fromHotbar{};
};
Snapshot snapshot;
struct InspectionValue { std::string name,text; unsigned long long generation{},observed{}; };
InspectionValue durabilityInspection;
InspectionValue itemInspection(ItemStack const& item) {
    auto o=informationOptions();InspectionValue value;
    if(item.isNull()||!previewItemWithinBudget(item)||!item.mItem)return value;
    if(o->advancedTooltips)value.text=advancedItemText(item.getTypeName(),item.getAuxValue(),item.mCount,
        item.isDamageableItem()?item.getDamageValue():0,item.isDamageableItem()?item.mItem->getMaxDamage():0);
    else if(o->durabilityInformation&&item.isDamageableItem())
        if(auto durability=durabilityValue({},item.getDamageValue(),item.mItem->getMaxDamage()))value.text=durabilityText(*durability);
    if(!value.text.empty()){value.name=boundedInspectorText(item.getHoverName(),1024);value.generation=worldGeneration.load();value.observed=GetTickCount64();}
    return value;
}

bool previewEnabledFor(std::string_view type) {
    if(!runtimeReady.load(std::memory_order_acquire))return false;
    auto family=previewFamily(type);
    return family==PreviewFamily::Shulker?settings()->on(Feature::ShulkerPreview):
        family==PreviewFamily::Bundle&&informationOptions()->bundlePreview
            &&(!informationOptions()->bundleRequireShift||(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);
}
bool inspectionEnabled() {
    auto o=informationOptions();return runtimeReady.load(std::memory_order_acquire)
        &&(settings()->on(Feature::ShulkerPreview)||o->bundlePreview||o->durabilityInformation||o->advancedTooltips);
}
ShulkerPreviewOptions previewOptionsFor(Snapshot const& current) {
    auto result=settings()->shulkerOptions;
    if(current.family==PreviewFamily::Bundle) {
        auto o=informationOptions();result.previewScale=o->bundleScale;
        result.showItemCount=o->bundleShowItemCount;result.showSlotNumbers=o->bundleShowSlotNumbers;
    }
    return result;
}
std::mutex snapshotMutex;
std::mutex nativeIconMutex;
std::array<std::unique_ptr<ItemStack>,64> nativeIcons;
DWORD nativeIconThread{};
unsigned long long nativeIconGeneration{};
std::uint64_t nativeIconHash{},nativeIconRevision{},publicationRevision{};
std::string nativeIconCollection;
int nativeIconIndex{-1};
std::atomic_bool nativeRetirementRequested{};
std::atomic_uint nativeIconReaders{};
struct NativeIconReadScope {
    NativeIconReadScope(){nativeIconReaders.fetch_add(1);}
    ~NativeIconReadScope(){nativeIconReaders.fetch_sub(1);}
};
bool retireNativeIcons() {
    std::array<std::unique_ptr<ItemStack>,64> retired;
    {
        std::lock_guard lock(nativeIconMutex);
        bool any=std::any_of(nativeIcons.begin(),nativeIcons.end(),[](auto const& icon){return static_cast<bool>(icon);});
        if(any&&(!previewIconThreadAllowed(nativeIconThread,GetCurrentThreadId())||nativeIconReaders.load()!=0)) {
            nativeRetirementRequested=true;return false;
        }
        retired=std::move(nativeIcons);
        nativeIconThread=0;nativeIconGeneration=0;nativeIconHash=0;nativeIconRevision=0;nativeIconCollection.clear();nativeIconIndex=-1;
        nativeRetirementRequested=false;
    }
    // Native destructors can call engine code; execute them on the UI owner without a publication lock.
    return true;
}
void serviceNativeIconRetirement() {
    bool obsolete=false;{std::lock_guard lock(nativeIconMutex);obsolete=nativeIconGeneration!=worldGeneration.load();}
    if(nativeRetirementRequested.load()||obsolete)retireNativeIcons();
}
ItemStack const* nativePreviewIcon(Snapshot const& current,unsigned slot) {
    std::lock_guard lock(nativeIconMutex);
    if(slot>=64||nativeRetirementRequested.load()||nativeIconThread!=GetCurrentThreadId()
        ||nativeIconGeneration!=current.generation||current.generation!=worldGeneration.load()||nativeIconRevision!=current.revision||nativeIconHash!=current.tagHash
        ||nativeIconIndex!=current.index||nativeIconCollection!=current.collection)return nullptr;
    return nativeIcons[slot].get();
}
std::atomic_bool exactContainerHoverActive{};
std::atomic<unsigned long long> exactContainerHoverAt{};
std::atomic_bool drawLogged{};
int lastHotbarSlot{-1};
std::string lastHotbarType;
std::uint64_t lastHotbarHash{};
unsigned long long lastHotbarGeneration{};
std::chrono::steady_clock::time_point hotbarChangedAt{};
ll::event::ListenerPtr renderListener;
ll::event::ListenerPtr beforeRenderListener;
std::mutex hoverMutex;
std::weak_ptr<ContainerScreenController> hoveredController;
unsigned long long hoveredGeneration{};
std::atomic<ULONGLONG> lastHoverRefresh{},lastBundleRefresh{};
std::string hoveredCollection;
int hoveredIndex{-1};
std::atomic_bool uiEventLogged{};
std::atomic_bool hoverLogged{};
std::atomic_bool decodeSuccessLogged{};
std::atomic_bool decodeFailureLogged{};
std::atomic_bool unhoverLogged{};
std::atomic_bool drawExceptionLogged{};
std::atomic_uint nativeDrawLogged{};
std::atomic_bool otherViewLogged{};
std::atomic_bool tooltipDrawExceptionLogged{};
std::atomic_bool liveItemCaptureLogged{};
bool hoverHookInstalled{},unhoverHookInstalled{};
bool itemHookInstalled{};
bool tooltipHookInstalled{};

struct LiveItemRenderContext {
    void const* screenContextIdentity{};
    ItemRenderChunkType chunkType{};
    int frame{};
    int zOrder{};
    DWORD threadId{};
    ULONGLONG capturedAt{};
    float light{},alpha{},scale{};
};
thread_local LiveItemRenderContext liveItemRenderContext{};
thread_local bool injectingPreview{};

std::string stripMinecraftFormatting(std::string_view text);
void clear();
std::optional<std::uint64_t> previewTagHash(ItemStack const& item);

bool decode(ItemStack const& box,std::string const& collection,int index,
            unsigned long long generation,std::uint64_t hash,bool fromHotbar=false,
            ContainerScreenController const* controller=nullptr,
            std::shared_ptr<ContainerModel> const& bundleModel={});

void clearExactHover() {
    std::lock_guard lock(hoverMutex);
    hoveredController.reset();
    hoveredCollection.clear();
    hoveredIndex = -1;
}

LL_TYPE_INSTANCE_HOOK(
    ShulkerSlotHoveredHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_onContainerSlotHovered,
    ui::ViewRequest,
    std::string const& collectionName,
    int index
) {
    auto result = origin(collectionName, index);
    {
        std::lock_guard lock(hoverMutex);
        auto base=this->weak_from_this().lock();
        hoveredController=base?std::static_pointer_cast<ContainerScreenController>(base):std::weak_ptr<ContainerScreenController>{};
        hoveredGeneration=worldGeneration.load();lastHoverRefresh=0;
        hoveredCollection = collectionName;
        hoveredIndex = index;
    }
    if (!hoverLogged) {
        hoverLogged = true;
        char message[256]{};
        sprintf_s(message, "ShulkerPreview exact hover captured: collection=%s index=%d",
                  collectionName.c_str(), index);
        logGuiInfo(message);
    }

    if (inspectionEnabled() && index >= 0 && index <= 255) {
        ItemStack const& box = _getItemStack(collectionName, index);
        {
            auto value=itemInspection(box);
            value.name=stripMinecraftFormatting(value.name);
            std::lock_guard lock(snapshotMutex);durabilityInspection=std::move(value);
        }
        if (!box.isNull() && previewEnabledFor(box.getTypeName())) {
            auto const generation = worldGeneration.load(std::memory_order_acquire);
            auto const hash=previewTagHash(box);
            if (hash&&decode(box, collectionName, index, generation, *hash, false, this)) {
                exactContainerHoverAt.store(GetTickCount64(), std::memory_order_release);
                exactContainerHoverActive.store(true, std::memory_order_release);
                if (!decodeSuccessLogged) {
                    decodeSuccessLogged = true;
                    char message[256]{};
                    sprintf_s(message,
                              "ShulkerPreview exact decode PASS: type=%s collection=%s index=%d",
                              box.getTypeName().c_str(), collectionName.c_str(), index);
                    logGuiInfo(message);
                }
            } else {
                clear();
                exactContainerHoverActive.store(false, std::memory_order_release);
                exactContainerHoverAt.store(0, std::memory_order_release);
                if (!decodeFailureLogged) {
                    decodeFailureLogged = true;
                    char message[256]{};
                    sprintf_s(message,
                              "ShulkerPreview exact decode FAIL: type=%s collection=%s index=%d userData=%d",
                              box.getTypeName().c_str(), collectionName.c_str(), index,
                              box.mUserData ? 1 : 0);
                    logGuiInfo(message);
                }
            }
        } else {
            exactContainerHoverActive.store(false, std::memory_order_release);
            exactContainerHoverAt.store(0, std::memory_order_release);
        }
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    ShulkerSlotUnhoveredHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_onContainerSlotUnhovered,
    ui::ViewRequest,
    std::string const& collectionName,
    int index
) {
    auto result = origin(collectionName, index);
    {std::lock_guard lock(snapshotMutex);durabilityInspection={};}
    std::lock_guard lock(hoverMutex);
    if (hoveredController.lock().get() == this && hoveredCollection == collectionName && hoveredIndex == index) {
        hoveredController.reset();
        hoveredCollection.clear();
        hoveredIndex = -1;
    }
    exactContainerHoverActive.store(false, std::memory_order_release);
    exactContainerHoverAt.store(0, std::memory_order_release);
    if(!unhoverLogged) {
        unhoverLogged=true;
        logGuiInfo("ShulkerPreview exact unhover captured; legacy 220ms residency retained");
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    ShulkerInventoryItemRenderHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemInChunk,
    void,
    BaseActorRenderContext& renderContext,
    ItemRenderChunkType itemRenderChunkType,
    ItemStack const& item,
    float x,
    float y,
    float lightMultiplier,
    float alphaMultiplier,
    float scale,
    int frame,
    bool allowAnimation,
    int zOrder,
    std::optional<TextureUVCoordinateSet> const& customIconUVCoords
) {
    bool const effectiveAnimation=previewAllowsAnimation(injectingPreview,allowAnimation);
    if(injectingPreview) {
        static thread_local unsigned traces{};
        if(traces<12) {
            ++traces;
            char message[448]{};
            std::snprintf(message,sizeof(message),"Preview nested native item: block=%.80s type=%d position=(%.3f,%.3f) light=%.3f alpha=%.3f scale=%.3f frame=%d z=%d requestedAnimation=%d effectiveAnimation=%d pickupPop=%.5f",item.getTypeName().c_str(),int(itemRenderChunkType),x,y,lightMultiplier,alphaMultiplier,scale,frame,zOrder,allowAnimation,effectiveAnimation,item.getPickupPopPercentage());
            logGuiInfo(message);
        }
    }
    origin(renderContext, itemRenderChunkType, item, x, y,
           lightMultiplier, alphaMultiplier, scale, frame,
           effectiveAnimation, zOrder, customIconUVCoords);

    if (injectingPreview || !inspectionEnabled()) return;
    liveItemRenderContext.screenContextIdentity = &renderContext.mScreenContext;
    liveItemRenderContext.chunkType = itemRenderChunkType;
    liveItemRenderContext.frame = frame;
    liveItemRenderContext.zOrder = zOrder;
    liveItemRenderContext.threadId = GetCurrentThreadId();
    liveItemRenderContext.capturedAt = GetTickCount64();
    liveItemRenderContext.light=lightMultiplier;
    liveItemRenderContext.alpha=alphaMultiplier;
    liveItemRenderContext.scale=scale;
    if (!liveItemCaptureLogged) {
        liveItemCaptureLogged = true;
        logGuiInfo("ShulkerPreview live renderGuiItemInChunk context captured");
    }
}

void clear() {
    {std::lock_guard lock(snapshotMutex);snapshot={};durabilityInspection={};}
    retireNativeIcons();
}

std::string stripMinecraftFormatting(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        auto const current = static_cast<unsigned char>(text[i]);
        // UTF-8 section sign (?) is C2 A7. Minecraft formatting codes are
        // encoded as ? followed by one code byte (e.g. ?f, ?r, ?l).
        if (current == 0xC2 && i + 1 < text.size()
            && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            i += 2;
            if (i < text.size()) ++i;
            continue;
        }
        // Keep compatibility with strings that expose the section sign as a
        // single legacy byte instead of UTF-8.
        if (current == 0xA7) {
            ++i;
            if (i < text.size()) ++i;
            continue;
        }
        clean.push_back(text[i]);
        ++i;
    }
    return clean;
}

std::optional<int> numericTag(CompoundTag const& tag, std::string_view key) {
    if (!tag.contains(key)) return std::nullopt;
    auto const& value=tag.at(key);
    switch(value.index()) {
    case Tag::Byte: return static_cast<unsigned char>(value.get<ByteTag>().data);
    case Tag::Short: return value.get<ShortTag>().data;
    case Tag::Int: return value.get<IntTag>().data;
    default: return std::nullopt;
    }
}

ListTag const* findItemsList(CompoundTag const& tag, int depth=0) {
    if (depth > 3) return nullptr;
    for (auto const* key : {"Items", "items"}) {
        if (tag.contains(key)) {
            auto const& value = tag.at(key);
            if (value.index() == Tag::List) return &value.get<ListTag>();
        }
    }
    for (auto const& [name, value] : tag) {
        (void)name;
        if (value.index() != Tag::Compound) continue;
        if (auto const* nested = findItemsList(value.get<CompoundTag>(), depth + 1)) return nested;
    }
    return nullptr;
}

bool budgetTag(Tag const& tag,PreviewNbtBudget& budget,unsigned depth=0) {
    if(!budget.consume(depth,8))return false;
    switch(tag.getId()) {
    case Tag::Compound:
        for(auto const& [key,value]:static_cast<CompoundTag const&>(tag)) {
            if(!budget.arrayBytes(key.size(),1)||!budgetTag(value.get(),budget,depth+1))return false;
        }
        return true;
    case Tag::List: {
        auto const& list=static_cast<ListTag const&>(tag);
        if(list.size()>PreviewNbtBudget::maximumNodes-budget.nodes)return false;
        for(auto const& value:list)if(!value||!budgetTag(*value,budget,depth+1))return false;
        return true;
    }
    case Tag::String:return budget.arrayBytes(static_cast<StringTag const&>(tag).size(),1);
    case Tag::ByteArray:return budget.arrayBytes(static_cast<ByteArrayTag const&>(tag).size(),1);
    case Tag::IntArray:return budget.arrayBytes(static_cast<IntArrayTag const&>(tag).size(),sizeof(int));
    default:return tag.getId()<Tag::NumTagTypes;
    }
}
std::optional<std::uint64_t> previewTagHash(ItemStack const& item) {
    if(!item.mUserData)return 0;
    PreviewNbtBudget budget;if(!budgetTag(*item.mUserData,budget))return std::nullopt;
    return item.mUserData->hash();
}
std::optional<std::uint32_t> readBundleId(ItemStack const& item) {
    if(!item.mUserData||!item.mUserData->contains("bundle_id"))return std::nullopt;
    auto const& value=item.mUserData->at("bundle_id");
    if(value.index()==Tag::Int)return bundleDynamicId(value.get<IntTag>().data);
    if(value.index()==Tag::Int64)return bundleDynamicId(value.get<Int64Tag>().data);
    return std::nullopt;
}
std::shared_ptr<ContainerModel> hotbarBundleModel(LocalPlayer& player,ItemStack const& bundle) {
    auto id=readBundleId(bundle);if(!id)return {};
    auto hud=player.mHudContainerManagerModel.get();if(!hud)return {};
    auto registry=hud->mRegistryAccess.get().lock();if(!registry)return {};
    FullContainerName name{};name.mName=ContainerEnumName::DynamicContainer;name.mDynamicId=std::optional<uint>{*id};
    auto model=registry->getModel(name);
    if(!model||!model->isValid()||model->getContainerSize()<0||model->getContainerSize()>64)return {};
    // The registry/model shared references exist only in this live UI callback.
    return model;
}
bool decode(ItemStack const& box,std::string const& collection,int index,
            unsigned long long generation,std::uint64_t hash,bool fromHotbar,
            ContainerScreenController const* controller,std::shared_ptr<ContainerModel> const& bundleModel) {
    if(generation!=worldGeneration.load()||!runtimeReady.load()||nativeIconReaders.load()!=0)return false;
    {std::lock_guard lock(nativeIconMutex);if(!previewIconThreadAllowed(nativeIconThread,GetCurrentThreadId()))return false;}
    PreviewNbtBudget nbtBudget;if(box.mUserData&&!budgetTag(*box.mUserData,nbtBudget))return false;
    // Old Praxis retains the native pool throughout an unchanged continuous
    // Shulker hover. Rebuilding every 100ms restarted native pickup state.
    // Bundles use a dynamic container model and cannot reuse their tag hash.
    if(previewFamily(box.getTypeName())==PreviewFamily::Shulker) {
        std::scoped_lock lock(snapshotMutex,nativeIconMutex);
        if(snapshot.valid&&snapshot.family==PreviewFamily::Shulker
           &&snapshot.generation==generation&&snapshot.tagHash==hash
           &&snapshot.itemType==box.getTypeName()&&snapshot.collection==collection
           &&snapshot.index==index&&snapshot.fromHotbar==fromHotbar
           &&nativeIconGeneration==generation&&nativeIconHash==hash
           &&nativeIconCollection==collection&&nativeIconIndex==index
           &&nativeIconRevision==snapshot.revision&&!nativeRetirementRequested.load()) {
            snapshot.observed=std::chrono::steady_clock::now();
            return true;
        }
    }
    std::array<std::unique_ptr<ItemStack>,64> nextIcons;
    Snapshot next;
    next.title=stripMinecraftFormatting(box.getHoverName());
    next.itemType=box.getTypeName();
    next.collection=collection;
    next.index=index;
    next.tagHash=hash;
    next.generation=generation;
    next.observed=std::chrono::steady_clock::now();
    next.valid=true;
    next.fromHotbar=fromHotbar;
    next.family=previewFamily(next.itemType);
    if(next.family==PreviewFamily::None)return false;
    std::array<bool,64> seen{};unsigned entries=0;
    if(next.family==PreviewFamily::Bundle&&(controller||bundleModel)) {
        // Borrow only in this live UI callback; controller/model ownership never enters publication.
        int const slotCount=controller?64:bundleModel->getContainerSize();
        for(int slot=0;slot<slotCount;++slot) {
            ItemStack const& item=controller?BundleHelper::getItemStackFromBundle(*controller,box,slot):bundleModel->getItemStack(slot);
            if(item.isNull())continue;
            if(!validatePreviewEntry(seen,slot,item.mCount))return false;
            if(item.mUserData&&!budgetTag(*item.mUserData,nbtBudget))return false;
            unsigned const packed=entries++;auto& target=next.slots[packed];target.name=item.getHoverName();target.count=item.mCount;
            nextIcons[packed]=std::make_unique<ItemStack>(item);nextIcons[packed]->mShowPickUp=false;target.icon=true;
        }
    }
    if (box.mUserData) {
        CompoundTag const* root = box.mUserData.get();
        if (root) {
            ListTag const* items = findItemsList(*root);
            if (items && entries==0) {
                if (items->size()>(next.family==PreviewFamily::Shulker?27u:64u)) return false;
                for (auto const& wrapped:*items) {
                    if(!wrapped)return false;
                    auto const& tag=*wrapped;
                    if (tag.getId()!=Tag::Compound) return false;
                    auto const& entry=static_cast<CompoundTag const&>(tag);
                    int const slot=numericTag(entry,"Slot").value_or(
                        numericTag(entry,"slot").value_or(-1));
                    int const count=numericTag(entry,"Count").value_or(
                        numericTag(entry,"count").value_or(0));
                    if(next.family==PreviewFamily::Shulker) {
                        if(!storeShulkerSlot(next.slotViews,slot,count))return false;
                    } else if(!validatePreviewEntry(seen,slot,count))return false;
                    ItemStack item=ItemStack::fromTag(entry);
                    if (item.isNull()) return false;
                    auto& target=next.slots[next.family==PreviewFamily::Shulker?static_cast<unsigned>(slot):entries++];
                    target.name=item.getHoverName();
                    target.count=static_cast<std::uint8_t>(count);
                    target.icon=true;nextIcons[&target-next.slots.data()]=std::make_unique<ItemStack>(item);
                }
            }
        }
    }
    if(next.family==PreviewFamily::Bundle) {
        if(!controller&&!bundleModel&&entries==0&&box.mUserData&&box.mUserData->contains("bundle_id"))return false;
        if(entries==0&&!informationOptions()->bundleShowEmpty)return false;
        next.grid=bundleGrid(entries);
    }
    if(generation!=worldGeneration.load()||!runtimeReady.load())return false;
    std::array<std::unique_ptr<ItemStack>,64> retired;
    {std::lock_guard lock(nativeIconMutex);
        if(nativeIconReaders.load()!=0||!previewIconThreadAllowed(nativeIconThread,GetCurrentThreadId()))return false;
        next.revision=++publicationRevision;nativeIconRevision=next.revision;
        retired=std::move(nativeIcons);nativeIcons=std::move(nextIcons);nativeIconThread=GetCurrentThreadId();nativeIconGeneration=generation;
        nativeIconHash=hash;nativeIconCollection=collection;nativeIconIndex=index;nativeRetirementRequested=false;
    }
    {
        std::lock_guard lock(snapshotMutex);
        snapshot=std::move(next);
    }
    return true;
}

RectangleArea rect(float x,float y,float width,float height) {
    return RectangleArea{x,x+width,y,y+height};
}

void drawText(MinecraftUIRenderContext& ui,float x,float y,float width,std::string value,
              mce::Color const& color,float size=10.0f) {
    TextMeasureData const measure{size,0.0f,false,false,false,ui::TextAlignment::Left};
    CaretMeasureData const caret{0,false};
    ui.drawText(ui.mDebugTextFontHandle->getFont(),
                rect(x,y,width,std::max(16.0f,size+4.0f)),std::move(value),
                color,1.0f,ui::TextAlignment::Left,measure,caret);
}

void drawSnapshot(ll::event::AfterUIRenderEvent& event, Snapshot const& current) {
    NativeIconReadScope nativeRead;
    auto& ui=event.uiRenderContext();
    auto const size=event.screenView().mSize.get();
    auto const pointer=event.screenView().mPointerLocationPrevious.get();
    auto const options=previewOptionsFor(current);

    // Legacy Praxis PanelLayout values: 72px cells, 6px gaps, 24px title,
    // user scale with the proven 1.08 readability boost and adaptive fit.
    float const baseWidth=current.grid.columns*72.0f+(current.grid.columns-1)*6.0f+16.0f;
    float const baseHeight=current.grid.rows*72.0f+(current.grid.rows-1)*6.0f+24.0f+6.0f+18.0f;
    float fitScale=2.0f;
    if(size.x>0.0f&&size.y>0.0f)
        fitScale=std::min(size.x*0.92f/baseWidth,size.y*0.86f/baseHeight);
    constexpr float visualScaleBoost=1.08f;
    float const scale=std::clamp(
        std::min(options.previewScale*visualScaleBoost,fitScale),0.75f,2.0f);
    float const slotSize=72.0f*scale;
    float const gap=6.0f*scale;
    float const pitch=slotSize+gap;
    float const titleFontSize=24.0f*scale;
    float const canvasWidth=current.grid.columns*slotSize+(current.grid.columns-1)*gap;
    float const canvasHeight=current.grid.rows*slotSize+(current.grid.rows-1)*gap;
    float const panelWidth=canvasWidth+16.0f;
    float const panelHeight=canvasHeight+titleFontSize+6.0f*scale+18.0f;

    float x{},y{};
    if(current.fromHotbar) {
        x=size.x*0.5f-panelWidth*0.5f;
        y=size.y-92.0f-panelHeight;
    } else {
        constexpr float margin=8.0f;
        float const rightX=pointer.x+14.0f;
        float const leftX=pointer.x-panelWidth-14.0f;
        if(rightX+panelWidth<=size.x-margin)x=rightX;
        else if(leftX>=margin)x=leftX;
        else x=(size.x-panelWidth)*0.5f;

        float const belowY=pointer.y+14.0f;
        float const aboveY=pointer.y-panelHeight-14.0f;
        if(belowY+panelHeight<=size.y-margin)y=belowY;
        else if(aboveY>=margin)y=aboveY;
        else y=(size.y-panelHeight)*0.5f;
    }
    x=std::clamp(x,4.0f,std::max(4.0f,size.x-panelWidth-4.0f));
    y=std::clamp(y,4.0f,std::max(4.0f,size.y-panelHeight-4.0f));

    float const originX=x+8.0f;
    float const originY=y+7.0f+titleFontSize+6.0f*scale+4.0f;

    auto const color=options.backgroundColor;
    mce::Color const background{
        static_cast<float>(color&255)/255.0f,
        static_cast<float>((color>>8)&255)/255.0f,
        static_cast<float>((color>>16)&255)/255.0f};
    // Legacy final compositor is fully opaque regardless of saved alpha.
    ui.fillRectangle(rect(x,y,panelWidth,panelHeight),background,1.0f);
    drawText(ui,x+8.0f,y+7.0f,panelWidth-16.0f,current.title,
             mce::Color{245.0f/255.0f,245.0f/255.0f,250.0f/255.0f},
             titleFontSize);

    auto& client=ui.mClient;
    auto* renderer=client.getItemRenderer();
    if(!renderer)return;
    BaseActorRenderContext actorContext{
        ui.mScreenContext,client,client.getMinecraftGame_DEPRECATED()};

    mce::Color const cellColor{18.0f/255.0f,20.0f/255.0f,24.0f/255.0f};
    mce::Color const outlineColor{105.0f/255.0f,108.0f/255.0f,118.0f/255.0f};

    for(int slot=0;slot<current.grid.cells;++slot) {
        float const sx=originX+(slot%current.grid.columns)*pitch;
        float const sy=originY+(slot/current.grid.columns)*pitch;
        ui.fillRectangle(rect(sx,sy,slotSize,slotSize),cellColor,1.0f);
        ui.drawRectangle(rect(sx,sy,slotSize,slotSize),outlineColor,1.0f,
                         std::max(1,static_cast<int>(2.0f*scale)));

        auto const& item=current.slots[slot];
        if(options.showSlotNumbers) {
            drawText(ui,sx+2.0f*scale,sy+1.0f*scale,slotSize-4.0f*scale,
                     std::to_string(slot+1),
                     mce::Color{185.0f/255.0f,188.0f/255.0f,198.0f/255.0f},
                     14.0f*scale);
        }
        auto* icon=nativePreviewIcon(current,slot);if(!icon)continue;

        float const iconInset=6.0f*scale;
        float const iconPhysicalSize=slotSize-iconInset*2.0f;
        float const nativeScale=std::clamp(iconPhysicalSize/16.0f,0.1f,6.0f);
        renderer->renderGuiItemNew(
            actorContext,*icon,0,
            sx+iconInset,sy+iconInset,false,
            1.0f,1.0f,nativeScale,0);

        if(options.showItemCount&&item.count>1) {
            std::string const count=std::to_string(item.count);
            float const countFontSize=24.0f*scale;
            // Legacy count placement: lower-right with a dark backing/shadow.
            float const textWidth=static_cast<float>(count.size())*countFontSize*0.55f;
            float const tx=sx+slotSize-textWidth-2.0f*scale;
            float const ty=sy+slotSize-countFontSize-1.0f*scale;
            ui.fillRectangle(
                rect(tx-6.0f*scale,ty-3.0f*scale,
                     textWidth+8.0f*scale,countFontSize+5.0f*scale),
                cellColor,1.0f);
            drawText(ui,tx+1.0f,ty+1.0f,textWidth+4.0f*scale,count,
                     mce::Color{0.0f,0.0f,0.0f},countFontSize);
            drawText(ui,tx,ty,textWidth+4.0f*scale,count,
                     mce::Color{1.0f,1.0f,1.0f},countFontSize);
        }
    }

    ui.flushText(0.0f,std::nullopt);
    if(!drawLogged) {
        drawLogged=true;
        char message[192]{};
        sprintf_s(message,
                  "ShulkerPreview final UI draw reached: scale=%.3f panel=%.1fx%.1f",
                  scale,panelWidth,panelHeight);
        logGuiInfo(message);
    }
}

struct LegacyPanelLayout {
    float scale{};
    float slotSize{};
    float gap{};
    float pitch{};
    float titleFontSize{};
    float canvasWidth{};
    float canvasHeight{};
    float panelWidth{};
    float panelHeight{};
    float x{};
    float y{};
    float originX{};
    float originY{};
};

LegacyPanelLayout buildLegacyPanelLayout(
    float displayWidth,
    float displayHeight,
    float cursorX,
    float cursorY,
    bool fromHotbar,
    float previewScale,
    int columns=9, int rows=3
) {
    LegacyPanelLayout layout{};
    float const baseWidth=columns*72.0f+(columns-1)*6.0f+16.0f;
    float const baseHeight=rows*72.0f+(rows-1)*6.0f+24.0f+6.0f+18.0f;
    float fitScale=2.0f;
    if(displayWidth>0.0f&&displayHeight>0.0f) {
        fitScale=std::min(
            displayWidth*0.92f/baseWidth,
            displayHeight*0.86f/baseHeight
        );
    }

    // Exact legacy Praxis layout constants.
    constexpr float visualScaleBoost=1.08f;
    layout.scale=containerPreviewScale({columns,rows,columns*rows},displayWidth,displayHeight,previewScale);
    layout.slotSize=72.0f*layout.scale;
    layout.gap=6.0f*layout.scale;
    layout.pitch=layout.slotSize+layout.gap;
    layout.titleFontSize=24.0f*layout.scale;
    layout.canvasWidth=columns*layout.slotSize+(columns-1)*layout.gap;
    layout.canvasHeight=rows*layout.slotSize+(rows-1)*layout.gap;
    layout.panelWidth=layout.canvasWidth+16.0f;
    layout.panelHeight=
        layout.canvasHeight+layout.titleFontSize+
        6.0f*layout.scale+18.0f;

    if(fromHotbar) {
        layout.x=displayWidth*0.5f-layout.panelWidth*0.5f;
        layout.y=displayHeight-92.0f-layout.panelHeight;
    } else {
        constexpr float margin=8.0f;
        float const rightX=cursorX+14.0f;
        float const leftX=cursorX-layout.panelWidth-14.0f;
        if(rightX+layout.panelWidth<=displayWidth-margin)
            layout.x=rightX;
        else if(leftX>=margin)
            layout.x=leftX;
        else
            layout.x=(displayWidth-layout.panelWidth)*0.5f;

        float const belowY=cursorY+14.0f;
        float const aboveY=cursorY-layout.panelHeight-14.0f;
        if(belowY+layout.panelHeight<=displayHeight-margin)
            layout.y=belowY;
        else if(aboveY>=margin)
            layout.y=aboveY;
        else
            layout.y=(displayHeight-layout.panelHeight)*0.5f;
    }

    layout.x=std::clamp(
        layout.x,4.0f,
        std::max(4.0f,displayWidth-layout.panelWidth-4.0f)
    );
    layout.y=std::clamp(
        layout.y,4.0f,
        std::max(4.0f,displayHeight-layout.panelHeight-4.0f)
    );

    // Old onRender() used WindowPadding=(8,7), title dummy height,
    // and default ItemSpacing.y (4px in the legacy style).
    layout.originX=layout.x+8.0f;
    layout.originY=
        layout.y+7.0f+layout.titleFontSize+
        6.0f*layout.scale+4.0f;
    return layout;
}

struct NativeDrawReceipt {
    LegacyPanelLayout layout;
    std::uint64_t generation{},revision{},observedMs{};
};
std::atomic<std::shared_ptr<NativeDrawReceipt const>> nativeDrawReceipt;

bool readClientCursorPixelsForImGui(ImVec2 const& display, ImVec2& output) {
    output={};
    if(display.x<=0.0f||display.y<=0.0f)return false;
    POINT cursor{};
    HWND window=GetForegroundWindow();
    DWORD processId{};
    if(!window
       ||!GetWindowThreadProcessId(window,&processId)
       ||processId!=GetCurrentProcessId()
       ||!GetCursorPos(&cursor)
       ||!ScreenToClient(window,&cursor)) {
        return false;
    }
    RECT clientRect{};
    if(!GetClientRect(window,&clientRect))return false;
    float const clientWidth=
        static_cast<float>(clientRect.right-clientRect.left);
    float const clientHeight=
        static_cast<float>(clientRect.bottom-clientRect.top);
    if(clientWidth<=0.0f||clientHeight<=0.0f)return false;
    output.x=static_cast<float>(cursor.x)*display.x/clientWidth;
    output.y=static_cast<float>(cursor.y)*display.y/clientHeight;
    return std::isfinite(output.x)&&std::isfinite(output.y);
}

struct TooltipFrameMetrics {
    float displayWidth{};
    float displayHeight{};
    float cursorX{};
    float cursorY{};
    float logicalPerPixelX{1.0f};
    float logicalPerPixelY{1.0f};
    bool valid{};
};

TooltipFrameMetrics resolveTooltipFrameMetrics(IClientInstance& client) {
    TooltipFrameMetrics metrics{};
    HWND window = GetForegroundWindow();
    DWORD processId{};
    POINT cursor{};
    RECT clientRect{};
    if (!window
        || !GetWindowThreadProcessId(window, &processId)
        || processId != GetCurrentProcessId()
        || !GetCursorPos(&cursor)
        || !ScreenToClient(window, &cursor)
        || !GetClientRect(window, &clientRect)) {
        return metrics;
    }

    float const pixelWidth = static_cast<float>(clientRect.right-clientRect.left);
    float const pixelHeight = static_cast<float>(clientRect.bottom-clientRect.top);
    if (pixelWidth <= 0.0f || pixelHeight <= 0.0f) return metrics;

    metrics.displayWidth = pixelWidth;
    metrics.displayHeight = pixelHeight;
    metrics.cursorX = static_cast<float>(cursor.x);
    metrics.cursorY = static_cast<float>(cursor.y);

    auto gui = client.getGuiData();
    auto const& screenSize = gui->mScreenSizeData.get();
    Vec2 const logical = screenSize.clientUIScreenSize.get();
    if (logical.x > 0.0f && logical.y > 0.0f) {
        metrics.logicalPerPixelX = logical.x / pixelWidth;
        metrics.logicalPerPixelY = logical.y / pixelHeight;
    }
    if (!std::isfinite(metrics.logicalPerPixelX)
        || !std::isfinite(metrics.logicalPerPixelY)
        || metrics.logicalPerPixelX <= 0.0f
        || metrics.logicalPerPixelY <= 0.0f) {
        metrics.logicalPerPixelX = metrics.logicalPerPixelY = 1.0f;
    }
    metrics.valid = true;
    return metrics;
}

RectangleArea rectPixels(
    TooltipFrameMetrics const& metrics,
    float x,
    float y,
    float width,
    float height
) {
    return rect(
        x * metrics.logicalPerPixelX,
        y * metrics.logicalPerPixelY,
        width * metrics.logicalPerPixelX,
        height * metrics.logicalPerPixelY
    );
}

void drawTextPixels(
    MinecraftUIRenderContext& ui,
    TooltipFrameMetrics const& metrics,
    float x,
    float y,
    float width,
    std::string value,
    mce::Color const& color,
    float physicalPixelSize
) {
    // TextMeasureData uses a Minecraft font scale (1.0 is normal), not a
    // physical pixel size. Legacy Praxis' ImGui font sizes were 24px/14px.
    // Convert those to the native UI scale instead of passing 24 as a scale.
    float const nativeTextScale=std::clamp(physicalPixelSize/24.0f,0.50f,2.0f);
    drawText(
        ui,
        x * metrics.logicalPerPixelX,
        y * metrics.logicalPerPixelY,
        width * metrics.logicalPerPixelX,
        std::move(value),
        color,
        nativeTextScale
    );
}

void logPreviewNativeState(MinecraftUIRenderContext &ui, char const *phase) {
    auto const &screen=ui.mScreenContext;
    auto const &viewport=screen.viewport;
    auto const &camera=screen.camera;
    auto const &clip=screen.normalizedClipRegion.get();
    char message[1536]{};
    int used=std::snprintf(message,sizeof(message),"Preview state %s: viewport=(%.1f,%.1f)+(%.1f,%.1f) clip=%d(%.4g,%.4g,%.4g,%.4g) ui=%d persistent=%d",
        phase,viewport.size->x,viewport.size->y,viewport.offset->x,viewport.offset->y,bool(clip),
        clip?clip->x:0,clip?clip->y:0,clip?clip->z:0,clip?clip->w:0,screen.isDrawingUI,screen.isDrawingPersistentUIElement);
    std::array<MatrixStack const*,3> stacks{&camera.projectionMatrixStack.get(),&camera.viewMatrixStack.get(),&camera.worldMatrixStack.get()};
    std::array labels{" P"," V"," W"};
    for(std::size_t i=0;i<stacks.size();++i) {
        if(used<0||std::size_t(used)>=sizeof(message)-1)break;
        used+=std::snprintf(message+used,sizeof(message)-std::size_t(used),"%s=[",labels[i]);
        if(stacks[i]->stack->empty())continue;
        auto const &matrix=stacks[i]->top()._m.get();
        for(int column=0;column<4;++column)for(int row=0;row<4;++row) {
            if(used<0||std::size_t(used)>=sizeof(message)-1)break;
            used+=std::snprintf(message+used,sizeof(message)-std::size_t(used),"%.4g%s",matrix[column][row],column==3&&row==3?"]":",");
        }
    }
    logGuiInfo(message);
}

bool drawSnapshotAtTooltip(
    MinecraftUIRenderContext& ui,
    IClientInstance& client,
    Snapshot const& current
) {
    NativeIconReadScope nativeRead;
    {std::lock_guard lock(nativeIconMutex);
        if(nativeRetirementRequested.load()||nativeIconThread!=GetCurrentThreadId()
            ||nativeIconGeneration!=current.generation||current.generation!=worldGeneration.load()
            ||nativeIconRevision!=current.revision||nativeIconHash!=current.tagHash||nativeIconIndex!=current.index||nativeIconCollection!=current.collection)return false;
    }
    TooltipFrameMetrics const metrics=resolveTooltipFrameMetrics(client);
    if(!metrics.valid)return false;

    unsigned const route=(current.family==PreviewFamily::Bundle?2:0)+(current.fromHotbar?1:0);
    struct TraceLimit { unsigned count{}; ULONGLONG at{}; };
    thread_local std::array<TraceLimit,4> traces{};
    thread_local unsigned long long traceGeneration{};
    if(traceGeneration!=current.generation){traceGeneration=current.generation;traces={};}
    auto &traceLimit=traces[route];
    auto const traceAt=GetTickCount64();
    bool const trace=traceLimit.count<2&&traceAt>=traceLimit.at;
    if(trace){++traceLimit.count;traceLimit.at=traceAt+1000;}

    auto const options=previewOptionsFor(current);
    LegacyPanelLayout const layout=buildLegacyPanelLayout(
        metrics.displayWidth,
        metrics.displayHeight,
        metrics.cursorX,
        metrics.cursorY,
        current.fromHotbar,
        options.previewScale,current.grid.columns,current.grid.rows
    );
    float const scale=layout.scale;
    float const slotSize=layout.slotSize;
    float const pitch=layout.pitch;
    float const panelWidth=layout.panelWidth;
    float const panelHeight=layout.panelHeight;
    float const x=layout.x;
    float const y=layout.y;
    float const originX=layout.originX;
    float const originY=layout.originY;

    auto const color=options.backgroundColor;
    mce::Color const background{
        static_cast<float>(color&255)/255.0f,
        static_cast<float>((color>>8)&255)/255.0f,
        static_cast<float>((color>>16)&255)/255.0f
    };
    mce::Color const cellColor{18.0f/255.0f,20.0f/255.0f,24.0f/255.0f};

    if(trace)logPreviewNativeState(ui,"before-full-clip/fill");
    NativePreviewClipScope clipScope(ui);
    ui.fillRectangle(
        rectPixels(metrics,x,y,panelWidth,panelHeight),
        background,1.0f
    );

    bool const borrowedContextReady=
        liveItemRenderContext.screenContextIdentity
        && liveItemRenderContext.threadId==GetCurrentThreadId()
        && liveItemRenderContext.capturedAt
        && GetTickCount64()>=liveItemRenderContext.capturedAt
        && GetTickCount64()-liveItemRenderContext.capturedAt<=100
        && liveItemRenderContext.screenContextIdentity==&ui.mScreenContext;

    auto* nativeRenderer=client.getItemRenderer();
    // Match legacy Praxis' final native pass: prefer a freshly constructed
    // BaseActorRenderContext at the tooltip boundary instead of reusing the
    // inventory's private render-chunk state. This is what keeps both flat
    // icons and 3D block-model items stable.
    BaseActorRenderContext ownedContext{
        ui.mScreenContext,
        client,
        client.getMinecraftGame_DEPRECATED()
    };
    BaseActorRenderContext* itemContext=&ownedContext;
    bool const nativeItemReady=nativeRenderer&&itemContext;

    // Complete the opaque panel and every cell before submitting any native
    // model item. Interleaving later UI fills with 3D item submissions departed
    // from the working final-pass contract and shares camera/batch state.
    for(int slot=0;slot<current.grid.cells;++slot) {
        float const sx=originX+(slot%current.grid.columns)*pitch;
        float const sy=originY+(slot/current.grid.columns)*pitch;
        ui.fillRectangle(rectPixels(metrics,sx,sy,slotSize,slotSize),cellColor,1.0f);
    }
    if(trace)logPreviewNativeState(ui,"after-all-fills/before-icons");
    unsigned nativeCalls=0;
    for(int slot=0;slot<current.grid.cells;++slot) {
        float const sx=originX+(slot%current.grid.columns)*pitch;
        float const sy=originY+(slot/current.grid.columns)*pitch;

        auto const& item=current.slots[slot];
        auto* icon=nativePreviewIcon(current,slot);if(!icon)continue;

        if(nativeItemReady) {
            float const iconInset=6.0f*scale;
            float const iconPhysicalSize=slotSize-iconInset*2.0f;
            float const lx=(sx+iconInset)*metrics.logicalPerPixelX;
            float const ly=(sy+iconInset)*metrics.logicalPerPixelY;
            float const nativeScale=std::clamp(
                std::min(
                    iconPhysicalSize*metrics.logicalPerPixelX,
                    iconPhysicalSize*metrics.logicalPerPixelY
                )/16.0f,
                0.1f,6.0f
            );
            struct InjectionGuard {
                bool previous{injectingPreview};
                InjectionGuard(){injectingPreview=true;}
                ~InjectionGuard(){injectingPreview=previous;}
            } injectionGuard;
            // Verified 26.51 final-pass call. Do not substitute
            // renderGuiItemInChunk here: some flat 2D items vanish on that
            // path while block-model items still render, which matches the
            // partial-icons failure observed on 26.51.
            renderPreviewNativeIcon(*nativeRenderer,*itemContext,*icon,lx,ly,nativeScale);
            ++nativeCalls;
            if(trace&&nativeCalls==1)logPreviewNativeState(ui,"after-first-icon");
        }


    }
    if(!clipScope.restore()) {
        logGuiError("Preview native clipping restoration failed; no Present receipt published");
        return false;
    }
    if(trace) {
        logPreviewNativeState(ui,"after-icons/clip-restored");
        auto const logical=client.getGuiData()->mScreenSizeData->clientUIScreenSize.get();
        char message[512]{};
        std::snprintf(message,sizeof(message),"Preview contract: family=%s source=%s gen=%llu revision=%llu nativeCalls=%u display=%.1fx%.1f logical=%.1fx%.1f logicalPerPx=(%.5f,%.5f) iconScale=%.4f recentVanillaContext=%d vanilla=(light=%.3f alpha=%.3f scale=%.3f type=%d frame=%d z=%d) preview=(light=1 alpha=1 z=17 nestedAnimation=0)",current.family==PreviewFamily::Bundle?"Bundle":"Shulker",current.fromHotbar?"hotbar":"inventory",current.generation,static_cast<unsigned long long>(current.revision),nativeCalls,metrics.displayWidth,metrics.displayHeight,logical.x,logical.y,metrics.logicalPerPixelX,metrics.logicalPerPixelY,(slotSize-12*scale)*std::min(metrics.logicalPerPixelX,metrics.logicalPerPixelY)/16,borrowedContextReady,liveItemRenderContext.light,liveItemRenderContext.alpha,liveItemRenderContext.scale,int(liveItemRenderContext.chunkType),liveItemRenderContext.frame,liveItemRenderContext.zOrder);
        logGuiInfo(message);
    }
    if(!(nativeDrawLogged.fetch_or(1u<<route)&(1u<<route))) {
        char message[256]{};
        unsigned icons=0;for(int i=0;i<current.grid.cells;++i)icons+=current.slots[i].icon;
        sprintf_s(message,"Preview native draw: family=%s source=%s icons=%u cells=%d revision=%llu scale=%.3f panel=%.1fx%.1f",
            current.family==PreviewFamily::Bundle?"Bundle":"Shulker",current.fromHotbar?"hotbar":"inventory",icons,current.grid.cells,
            static_cast<unsigned long long>(current.revision),scale,panelWidth,panelHeight);
        logGuiInfo(message);
    }
    nativeDrawReceipt.store(std::make_shared<NativeDrawReceipt const>(NativeDrawReceipt{layout,current.generation,current.revision,GetTickCount64()}));
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    ShulkerTooltipRenderHook,
    ll::memory::HookPriority::Normal,
    HoverTextRenderer,
    &HoverTextRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance& client,
    UIControl& owner,
    int layer
) {
    Snapshot current;
    bool preview=false;
    if(informationOptions()->durabilityInformation||informationOptions()->advancedTooltips) {
        auto const& formatted=mFormattedContent.get();auto const& raw=mContent.get();
        std::lock_guard lock(snapshotMutex);
        if(!durabilityInspection.name.empty()&&durabilityInspection.generation==worldGeneration.load()
            &&(formatted.find(durabilityInspection.name)!=std::string::npos||raw.find(durabilityInspection.name)!=std::string::npos))
            { /* Sampling owns the freshness timestamp; tooltip matches cannot renew stale data. */ }
        else durabilityInspection={};
    }
    if(inspectionEnabled()) {
        auto const generation=worldGeneration.load(std::memory_order_acquire);
        auto const now=std::chrono::steady_clock::now();
        std::string const& formatted=mFormattedContent.get();
        std::string const& raw=mContent.get();

        std::lock_guard lock(snapshotMutex);
        if(snapshot.valid&&snapshot.generation==generation&&previewEnabledFor(snapshot.itemType)) {
            auto const age=std::chrono::duration_cast<std::chrono::milliseconds>(
                now-snapshot.observed).count();
            if(!snapshot.fromHotbar) {
                bool const titleMatches=
                    !snapshot.title.empty()
                    && (formatted.find(snapshot.title)!=std::string::npos
                        || raw.find(snapshot.title)!=std::string::npos);
                bool const exactActive=
                    exactContainerHoverActive.load(std::memory_order_acquire);
                bool const exactFresh=exactActive&&GetTickCount64()>=exactContainerHoverAt.load()
                    &&GetTickCount64()-exactContainerHoverAt.load()<=220;
                if(exactFresh&&titleMatches) {
                    // Same behavior as legacy observeTooltipText(): a live
                    // shulker tooltip continuously renews preview residency.
                    snapshot.observed=now;
                    current=snapshot;
                    preview=true;
                } else if(age>=0&&age<=220) {
                    current=snapshot;
                    preview=true;
                }
            } else if(age>=0&&hotbarPreviewActive(
                static_cast<std::uint64_t>(age))) {
                current=snapshot;
                preview=true;
            }
        }
    }

    // Match old Praxis: for an active shulker preview the expanded vanilla
    // tooltip is replaced by the preview panel at the tooltip render boundary.
    if(!preview) {
        origin(renderContext,client,owner,layer);
        if(informationOptions()->advancedTooltips)try {
            InspectionValue value;{std::lock_guard lock(snapshotMutex);value=durabilityInspection;}
            auto now=GetTickCount64();
            if(runtimeReady.load()&&value.generation==worldGeneration.load()&&!value.text.empty()
                &&now>=value.observed&&now-value.observed<=220) {
                auto logical=client.getGuiData()->mScreenSizeData.get().clientUIScreenSize.get();
                auto const cursor=mCursorPosition.get()+mOffset.get();auto const box=mBoxDimensions.get();
                auto& font=renderContext.mDebugTextFontHandle->getFont();float rowHeight=font.getWrapHeight();
                if(std::isfinite(logical.x)&&std::isfinite(logical.y)&&logical.x>16&&logical.y>16
                    &&std::isfinite(cursor.x)&&std::isfinite(cursor.y)&&std::isfinite(box.y)
                    &&std::isfinite(rowHeight)&&rowHeight>0) {
                    std::vector<std::string> lines;std::istringstream stream(value.text);std::string line;
                    while(std::getline(stream,line))lines.push_back(std::move(line));
                    float w=std::min(300.f,logical.x-4),h=std::min(8+rowHeight*lines.size(),logical.y-4);
                    float x=std::clamp(cursor.x,2.f,logical.x-w-2),y=cursor.y+box.y+3;
                    if(y+h>logical.y-2)y=cursor.y-h-3;
                    y=std::clamp(y,2.f,logical.y-h-2);
                    NativePreviewClipScope clip(renderContext);
                    renderContext.fillRectangle(RectangleArea{x,x+w,y,y+h},mce::Color{.07f,.08f,.10f},.95f);
                    for(unsigned row=0;row<lines.size();++row) {
                        if(8+(row+1)*rowHeight>h)break;
                        auto& text=lines[row];while(!text.empty()&&font.getLineLength(text,1,false)>w-8) {
                            auto end=text.size()-1;while(end&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;text.resize(end);
                        }
                        TextMeasureData measure{1,0,false,false,false,ui::TextAlignment::Left};CaretMeasureData caret{0,false};
                        renderContext.drawText(font,RectangleArea{x+4,x+w-4,y+4+row*rowHeight,y+4+(row+1)*rowHeight},
                            std::move(text),mce::Color{.95f,.95f,.95f},1,ui::TextAlignment::Left,measure,caret);
                    }
                    renderContext.flushText(0,std::nullopt);clip.restore();
                }
            }
        }catch(...){logGuiError("Advanced tooltip owned native text draw failed");}
        return;
    }

    try {
        if(!drawSnapshotAtTooltip(renderContext,client,current))origin(renderContext,client,owner,layer);
    } catch (...) {
        injectingPreview=false;
        origin(renderContext,client,owner,layer);
        if(!tooltipDrawExceptionLogged) {
            tooltipDrawExceptionLogged=true;
            logGuiError("ShulkerPreview tooltip-boundary native draw threw an exception");
        }
    }
}

void refreshExactInspection(ll::event::UIRenderEvent& event) {
    if(!inspectionEnabled()||!inspectionRefreshDue(lastHoverRefresh.load(),GetTickCount64()))return;
    std::shared_ptr<ContainerScreenController> controller;std::string collection;int index=-1;
    {std::lock_guard lock(hoverMutex);
        if(hoveredGeneration!=worldGeneration.load())return;
        controller=hoveredController.lock();collection=hoveredCollection;index=hoveredIndex;
    }
    if(index<0)return; // Preserve the established short unhover residency.
    auto invalidate=[] {
        clearExactHover();exactContainerHoverActive=false;exactContainerHoverAt=0;
        bool retire=false;{std::lock_guard lock(snapshotMutex);durabilityInspection={};
            if(snapshot.valid&&!snapshot.fromHotbar){snapshot={};retire=true;}}
        if(retire)retireNativeIcons();
    };
    // UI events visit every active ScreenView, including the HUD beneath an
    // inventory. A different view is not evidence that this hover ended.
    bool const sameScreen=controller&&event.screenView().mController.get()==static_cast<ScreenController*>(controller.get());
    bool slotMatches=false,hoverShown=false;
    if(sameScreen) {
        auto const& slot=controller->mHoveredSlotData.get();
        slotMatches=index>=0&&index<=255&&slot.mCollectionIndex==index&&slot.mCollectionName.get()==collection;
        hoverShown=controller->mShowHoverText;
    }
    auto visit=inspectionVisit(bool(controller),sameScreen,slotMatches,hoverShown);
    if(visit==InspectionVisit::Defer) {
        if(!otherViewLogged.exchange(true))logGuiInfo("Preview inspection: other ScreenView deferred; container hover retained");
        return;
    }
    if(visit==InspectionVisit::Retire){invalidate();return;}
    lastHoverRefresh=GetTickCount64();
    auto const& item=controller->_getItemStack(collection,index);
    auto durability=itemInspection(item);
    durability.name=stripMinecraftFormatting(durability.name);
    {std::lock_guard lock(snapshotMutex);durabilityInspection=std::move(durability);}
    if(item.isNull()||!previewEnabledFor(item.getTypeName())) {
        exactContainerHoverActive=false;exactContainerHoverAt=0;
        // Clear only the container preview; the independent durability value remains valid.
        {std::lock_guard lock(snapshotMutex);if(snapshot.valid&&!snapshot.fromHotbar)snapshot={};}
        return;
    }
    auto hash=previewTagHash(item);
    if(!hash||!decode(item,collection,index,worldGeneration.load(),*hash,false,controller.get())) {
        clear();exactContainerHoverActive=false;exactContainerHoverAt=0;return;
    }
    exactContainerHoverActive=true;exactContainerHoverAt=GetTickCount64();
}
void onUiRender(ll::event::AfterUIRenderEvent& event) {
    serviceNativeIconRetirement();
    ensureGuiBackend();
    drawNativeDurabilityHud(event);
    drawNativeOffhandHud(event);
    refreshExactInspection(event);
    if (!uiEventLogged) {
        uiEventLogged = true;
        logGuiInfo("ShulkerPreview AfterUIRenderEvent active");
    }

    auto& client=event.uiRenderContext().mClient;
    bool const backendReady=client.getLocalPlayer()&&client.getItemRenderer();
    if(backendReady)
        availableFeatures.fetch_or((1u<<static_cast<unsigned>(Feature::ShulkerPreview)));
    else
        availableFeatures.fetch_and(~((1u<<static_cast<unsigned>(Feature::ShulkerPreview))));

    // Container preview rendering deliberately does not happen from
    // AfterUIRenderEvent. Legacy Praxis submits it at HoverTextRenderer's final
    // tooltip boundary using the live ItemRenderer context captured earlier in
    // the same UI frame.
    if(!backendReady)return;
    if(!inspectionEnabled()) {
        exactContainerHoverActive.store(false,std::memory_order_release);
        exactContainerHoverAt.store(0,std::memory_order_release);
        clear();
        lastHotbarSlot=-1;
        lastHotbarType.clear();
        return;
    }

    auto const screen=event.screenView().getScreenName();
    if(screen!="hud_screen"&&screen!="in_game_play_screen")return;

    {
        auto now=std::chrono::steady_clock::now();
        std::lock_guard lock(snapshotMutex);
        auto age=std::chrono::duration_cast<std::chrono::milliseconds>(now-snapshot.observed).count();
        if(preferContainerPreview(snapshot.valid,snapshot.fromHotbar,snapshot.generation==worldGeneration.load(),age))return;
    }
    auto* player=client.getLocalPlayer();
    if(!player)return;
    ItemStack const& selected=player->getSelectedItem();
    int const slot=player->getSelectedItemSlot();
    auto const type=selected.isNull()?std::string{}:selected.getTypeName();
    if(previewFamily(type)==PreviewFamily::Bundle) {
        if(!informationOptions()->bundleShowInHotbar)return;
    } else if(!settings()->shulkerOptions.showInHotbar)return;
    if(selected.isNull()||!previewEnabledFor(type)) {
        bool clearHotbar=false;
        {
            std::lock_guard lock(snapshotMutex);
            clearHotbar=snapshot.valid&&snapshot.fromHotbar;
        }
        if(clearHotbar)clear();
        lastHotbarSlot=-1;
        lastHotbarType.clear();
        return;
    }

    auto const generation=worldGeneration.load(std::memory_order_acquire);
    auto safeHash=previewTagHash(selected);if(!safeHash){clear();return;}
    std::uint64_t const hash=*safeHash;
    auto bundleModel=previewFamily(type)==PreviewFamily::Bundle?hotbarBundleModel(*player,selected):std::shared_ptr<ContainerModel>{};
    bool hotbarSnapshotMatches=false;
    {std::lock_guard lock(snapshotMutex);hotbarSnapshotMatches=snapshot.valid&&snapshot.fromHotbar
        &&snapshot.generation==generation&&snapshot.tagHash==hash&&snapshot.index==slot;}
    if(slot!=lastHotbarSlot||type!=lastHotbarType||hash!=lastHotbarHash
       ||generation!=lastHotbarGeneration||!hotbarSnapshotMatches) {
        if(!decode(selected,"hotbar_items",slot,generation,hash,true,nullptr,bundleModel)) {
            clear();return; // Retry on the next live UI callback; do not commit a failed request.
        }
        lastHotbarSlot=slot;
        lastHotbarType=type;
        lastHotbarHash=hash;
        lastHotbarGeneration=generation;
        hotbarChangedAt=std::chrono::steady_clock::now();
        lastBundleRefresh=GetTickCount64();
    }

    auto const now=std::chrono::steady_clock::now();
    auto const elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        now-hotbarChangedAt).count();
    if(elapsed>=0&&hotbarPreviewActive(static_cast<std::uint64_t>(elapsed))) {
        if(previewFamily(type)==PreviewFamily::Bundle&&inspectionRefreshDue(lastBundleRefresh.load(),GetTickCount64())) {
            lastBundleRefresh=GetTickCount64();
            if(!decode(selected,"hotbar_items",slot,generation,hash,true,nullptr,bundleModel)){clear();return;}
        }
        Snapshot current;
        {std::lock_guard lock(snapshotMutex);
            if(snapshot.valid&&snapshot.fromHotbar){snapshot.observed=now;current=snapshot;}
        }
        if(current.valid)drawSnapshotAtTooltip(event.uiRenderContext(),client,current);
    }
}
} // namespace

void renderOwnedHudItem(ItemRenderer& renderer,BaseActorRenderContext& context,ItemStack const& item,float x,float y,float scale) {
    struct Guard {
        bool previous{injectingPreview};
        Guard(){injectingPreview=true;}
        ~Guard(){injectingPreview=previous;}
    } guard;
    renderPreviewNativeIcon(renderer,context,item,x,y,scale);
}
bool previewItemWithinBudget(ItemStack const& item) noexcept {
    try { PreviewNbtBudget budget;return !item.mUserData||budgetTag(*item.mUserData,budget); }
    catch(...){return false;}
}
bool shulkerPreviewNeeded() noexcept {
    try {
        if(!inspectionEnabled())return false;
        auto const generation=worldGeneration.load(std::memory_order_acquire);
        auto const now=std::chrono::steady_clock::now();
        std::lock_guard lock(snapshotMutex);
        if(informationOptions()->durabilityInformation&&!informationOptions()->advancedTooltips&&!durabilityInspection.text.empty()
            &&durabilityInspection.generation==generation&&GetTickCount64()>=durabilityInspection.observed
            &&GetTickCount64()-durabilityInspection.observed<=220)return true;
        if(!snapshot.valid||snapshot.generation!=generation||!previewEnabledFor(snapshot.itemType))return false;
        auto const age=std::chrono::duration_cast<std::chrono::milliseconds>(
            now-snapshot.observed).count();
        // Old Praxis needsImGuiRender() kept the final compositor resident
        // slightly longer than the 220 ms preview lifetime.
        return age>=0&&age<=450;
    } catch (...) {
        return false;
    }
}

void drawShulkerPreviewContent(void* context) noexcept {
    if(!context||!runtimeReady.load(std::memory_order_acquire))return;
    ImGuiContext* const target=static_cast<ImGuiContext*>(context);
    ImGuiContext* const previous=ImGui::GetCurrentContext();
    if(previous!=target)ImGui::SetCurrentContext(target);

    try {
        if(informationOptions()->durabilityInformation&&!informationOptions()->advancedTooltips) {
            InspectionValue value;{std::lock_guard lock(snapshotMutex);value=durabilityInspection;}
            auto now=GetTickCount64();
            if(value.generation==worldGeneration.load()&&!value.text.empty()&&now>=value.observed&&now-value.observed<=220) {
                // Preserve the vanilla name/lore tooltip. This read-only information card uses the selected host.
                auto display=ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos({std::max(4.0f,display.x-380),std::max(4.0f,display.y-90)},ImGuiCond_Always);
                auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings
                    |ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav;
                if(ImGui::Begin("Durability information##praxis_hover_durability",nullptr,flags)) {
                    ImGui::TextUnformatted(value.name.c_str());ImGui::Text("耐久値: %s",value.text.c_str());
                }
                ImGui::End();
            }
        }
        Snapshot current;
        {
            auto const generation=worldGeneration.load(std::memory_order_acquire);
            auto const now=std::chrono::steady_clock::now();
            std::lock_guard lock(snapshotMutex);
            if(!snapshot.valid||snapshot.generation!=generation||!previewEnabledFor(snapshot.itemType)) {
                if(previous!=target)ImGui::SetCurrentContext(previous);
                return;
            }
            auto const age=std::chrono::duration_cast<std::chrono::milliseconds>(
                now-snapshot.observed).count();
            if(age<0||age>220) {
                if(previous!=target)ImGui::SetCurrentContext(previous);
                return;
            }
            current=snapshot;
        }

        ImVec2 const display=ImGui::GetIO().DisplaySize;
        if(display.x<=0.0f||display.y<=0.0f) {
            if(previous!=target)ImGui::SetCurrentContext(previous);
            return;
        }

        ImVec2 cursor=ImGui::GetMousePos();
        if(!current.fromHotbar) {
            ImVec2 clientCursor{};
            if(readClientCursorPixelsForImGui(display,clientCursor))
                cursor=clientCursor;
        }

        auto const options=previewOptionsFor(current);
        auto receipt=nativeDrawReceipt.load();
        if(!receipt||!currentPreviewDraw(current.generation,current.revision,receipt->generation,receipt->revision,receipt->observedMs,GetTickCount64())) {
            if(previous!=target)ImGui::SetCurrentContext(previous);return;
        }
        LegacyPanelLayout const layout=receipt->layout;

        ImGui::SetNextWindowPos(
            ImVec2(layout.x,layout.y),
            ImGuiCond_Always
        );
        ImGui::SetNextWindowSize(
            ImVec2(layout.panelWidth,layout.panelHeight),
            ImGuiCond_Always
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(8.0f,7.0f)
        );
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,4.0f);
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            ImVec4(0.0f,0.0f,0.0f,0.0f)
        );

        ImGuiWindowFlags const flags=
            ImGuiWindowFlags_NoDecoration|
            ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoBringToFrontOnFocus|
            ImGuiWindowFlags_NoNav|
            ImGuiWindowFlags_NoInputs|
            ImGuiWindowFlags_NoBackground;

        if(ImGui::Begin(
            "Shulker Preview##ani_shulker_preview",
            nullptr,
            flags
        )) {
            ImDrawList* const draw=ImGui::GetWindowDrawList();

            // This is copied from old Praxis' final Present compositor.
            ImVec2 const titlePosition{layout.x+8,layout.y+7};
            draw->AddText(
                ImGui::GetFont(),
                layout.titleFontSize,
                titlePosition,
                IM_COL32(245,245,250,255),
                current.title.c_str()
            );

            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    layout.titleFontSize+6.0f*layout.scale
                )
            );
            ImVec2 const origin{layout.originX,layout.originY};
            ImVec2 mouse=ImGui::GetMousePos();
            if(!current.fromHotbar) {
                ImVec2 clientCursor{};
                if(readClientCursorPixelsForImGui(display,clientCursor))
                    mouse=clientCursor;
            }
            int hoveredSlot=-1;

            for(int slot=0;slot<current.grid.cells;++slot) {
                int const column=slot%current.grid.columns;
                int const row=slot/current.grid.columns;
                ImVec2 const min(
                    origin.x+column*layout.pitch,
                    origin.y+row*layout.pitch
                );
                ImVec2 const max(
                    min.x+layout.slotSize,
                    min.y+layout.slotSize
                );
                auto const& item=current.slots[slot];

                // Background and actual item are already rendered at the
                // tooltip boundary. Present contributes the exact old Praxis
                // foreground border and text only, so it cannot cover icons.
                draw->AddRect(
                    min,
                    max,
                    IM_COL32(105,108,118,255),
                    2.0f*layout.scale
                );

                if(item.icon&&options.showItemCount&&item.count>1) {
                    std::string const count=std::to_string(item.count);
                    float const countFontSize=24.0f*layout.scale;
                    ImVec2 const textSize=ImGui::GetFont()->CalcTextSizeA(
                        countFontSize,
                        FLT_MAX,
                        0.0f,
                        count.c_str()
                    );
                    ImVec2 const textPos(
                        max.x-textSize.x-2.0f*layout.scale,
                        max.y-textSize.y-1.0f*layout.scale
                    );
                    draw->AddRectFilled(
                        ImVec2(
                            textPos.x-6.0f*layout.scale,
                            textPos.y-3.0f*layout.scale
                        ),
                        max,
                        IM_COL32(18,20,24,255),
                        2.0f*layout.scale
                    );
                    draw->AddText(
                        ImGui::GetFont(),
                        countFontSize,
                        ImVec2(textPos.x+1.0f,textPos.y+1.0f),
                        IM_COL32(0,0,0,240),
                        count.c_str()
                    );
                    draw->AddText(
                        ImGui::GetFont(),
                        countFontSize,
                        textPos,
                        IM_COL32(255,255,255,255),
                        count.c_str()
                    );
                }

                if(options.showSlotNumbers) {
                    std::string const number=std::to_string(slot+1);
                    draw->AddText(
                        ImGui::GetFont(),
                        14.0f*layout.scale,
                        ImVec2(min.x+2.0f,min.y+1.0f),
                        IM_COL32(185,188,198,220),
                        number.c_str()
                    );
                }

                if(mouse.x>=min.x&&mouse.x<=max.x
                   &&mouse.y>=min.y&&mouse.y<=max.y) {
                    hoveredSlot=slot;
                }
            }

            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(
                ImVec2(layout.canvasWidth,layout.canvasHeight)
            );

            if(hoveredSlot>=0&&current.slots[hoveredSlot].icon) {
                auto const& item=current.slots[hoveredSlot];
                if(options.showItemCount)
                    ImGui::SetTooltip(
                        "%s x%d",
                        item.name.c_str(),
                        static_cast<int>(item.count)
                    );
                else
                    ImGui::SetTooltip("%s",item.name.c_str());
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        static bool logged{};
        if(!logged) {
            logged=true;
            char message[224]{};
            sprintf_s(
                message,
                "ShulkerPreview legacy Present compositor active: scale=%.3f panel=%.1fx%.1f",
                layout.scale,
                layout.panelWidth,
                layout.panelHeight
            );
            logGuiInfo(message);
        }
    } catch (...) {
        logGuiError("ShulkerPreview legacy Present compositor faulted");
    }

    if(previous!=target)ImGui::SetCurrentContext(previous);
}

bool installShulkerPreview() {
    if(hoverHookInstalled||unhoverHookInstalled||itemHookInstalled||tooltipHookInstalled||renderListener||beforeRenderListener)return false;
    auto fail=[] { removeShulkerPreview();return false; };
    if(ShulkerSlotHoveredHook::hook()!=0)return false;
    hoverHookInstalled=true;
    if(ShulkerSlotUnhoveredHook::hook()!=0)return fail();
    unhoverHookInstalled=true;
    if(ShulkerInventoryItemRenderHook::hook()!=0)return fail();
    itemHookInstalled=true;
    if(ShulkerTooltipRenderHook::hook()!=0)return fail();
    tooltipHookInstalled=true;
    try {
        beforeRenderListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::BeforeUIRenderEvent>(
            [](ll::event::BeforeUIRenderEvent& event) {
                try {serviceNativeIconRetirement();refreshExactInspection(event);}
                catch(...) {if(!drawExceptionLogged.exchange(true))logGuiError("Preview before-UI inspection threw");}
            });
        if(!beforeRenderListener)return fail();
        renderListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent& event) {
                try { onUiRender(event); }
                catch (...) {
                    if(!drawExceptionLogged.exchange(true))
                        logGuiError("ShulkerPreview observation event threw an exception");
                }
            });
    } catch(...) {return fail();}
    if(!renderListener)return fail();
    logGuiInfo("ShulkerPreview legacy hook chain installed: hover + inventory item + tooltip boundary");
    return true;
}
bool removeShulkerPreview() noexcept {
    bool ok = retireNativeIcons();
    // Retain the UI listener until its native ItemStack resources are retired on their owning thread.
    if(renderListener&&ok) {
        bool const removed=ll::event::EventBus::getInstance().removeListener<ll::event::AfterUIRenderEvent>(renderListener);
        if(removed) renderListener.reset();
        else ok = false;
    }
    if(beforeRenderListener&&ok) {
        if(ll::event::EventBus::getInstance().removeListener(beforeRenderListener))beforeRenderListener.reset();
        else ok=false;
    }
    nativeDrawReceipt.store({});
    if (tooltipHookInstalled) {
        if (!ShulkerTooltipRenderHook::unhook()) ok = false;
        else tooltipHookInstalled = false;
    }
    if (itemHookInstalled) {
        if (!ShulkerInventoryItemRenderHook::unhook()) ok = false;
        else itemHookInstalled = false;
    }
    if(unhoverHookInstalled) {
        if(!ShulkerSlotUnhoveredHook::unhook())ok=false;
        else unhoverHookInstalled=false;
    }
    if(hoverHookInstalled) {
        if(!ShulkerSlotHoveredHook::unhook())ok=false;
        else hoverHookInstalled=false;
    }
    liveItemRenderContext = {};
    exactContainerHoverActive.store(false, std::memory_order_release);
    exactContainerHoverAt.store(0, std::memory_order_release);
    clearExactHover();
    clear();
    availableFeatures.fetch_and(~((1u<<static_cast<unsigned>(Feature::ShulkerPreview))));
    return ok;
}
} // namespace praxis::companion
