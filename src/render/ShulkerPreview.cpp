#include "render/ShulkerPreview.h"
#include <Windows.h>
#include <imgui.h>
#include "core/Settings.h"
#include "core/ShulkerPreviewPolicy.h"
#include "gui/GuiBridge.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
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

namespace praxis::companion {
namespace {
struct OwnedSlot {
    // ItemStack copies are only constructed on the Minecraft UI thread during
    // decode. Snapshot copies across the Present thread share the already-built
    // stack instead of invoking ItemStack's copy constructor again.
    std::shared_ptr<ItemStack> icon;
    std::string name;
    std::uint8_t count{};
};
struct Snapshot {
    std::array<OwnedSlot,27> slots;
    ShulkerSlotViews slotViews{};
    std::string title;
    std::string itemType;
    std::string collection;
    int index{-1};
    std::uint64_t tagHash{};
    unsigned long long generation{};
    std::chrono::steady_clock::time_point observed{};
    bool valid{};
    bool fromHotbar{};
};
Snapshot snapshot;
std::mutex snapshotMutex;
std::atomic_bool exactContainerHoverActive{};
std::atomic<unsigned long long> exactContainerHoverAt{};
bool drawLogged{};
int lastHotbarSlot{-1};
std::string lastHotbarType;
std::uint64_t lastHotbarHash{};
unsigned long long lastHotbarGeneration{};
std::chrono::steady_clock::time_point hotbarChangedAt{};
ll::event::ListenerPtr renderListener;
std::mutex hoverMutex;
ContainerScreenController* hoveredController{};
std::string hoveredCollection;
int hoveredIndex{-1};
bool uiEventLogged{};
bool hoverLogged{};
bool decodeSuccessLogged{};
bool decodeFailureLogged{};
bool unhoverLogged{};
bool drawExceptionLogged{};
bool tooltipDrawLogged{};
bool tooltipDrawExceptionLogged{};
bool liveItemCaptureLogged{};
bool hoverHooksInstalled{};
bool itemHookInstalled{};
bool tooltipHookInstalled{};

struct LiveItemRenderContext {
    ItemRenderer* renderer{};
    BaseActorRenderContext* context{};
    ItemRenderChunkType chunkType{};
    int frame{};
    int zOrder{};
    DWORD threadId{};
    ULONGLONG capturedAt{};
};
thread_local LiveItemRenderContext liveItemRenderContext{};
thread_local bool injectingPreview{};

bool decode(ItemStack const& box,std::string const& collection,int index,
            unsigned long long generation,std::uint64_t hash,bool fromHotbar=false);

void clearExactHover() {
    std::lock_guard lock(hoverMutex);
    hoveredController = nullptr;
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
        hoveredController = this;
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

    if (settings()->on(Feature::ShulkerPreview) && index >= 0 && index <= 255) {
        ItemStack const& box = _getItemStack(collectionName, index);
        if (!box.isNull() && shulkerIdentity(box.getTypeName())) {
            auto const generation = worldGeneration.load(std::memory_order_acquire);
            std::uint64_t const hash = box.mUserData ? box.mUserData->hash() : 0;
            if (decode(box, collectionName, index, generation, hash, false)) {
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
    std::lock_guard lock(hoverMutex);
    if (hoveredController == this && hoveredCollection == collectionName && hoveredIndex == index) {
        hoveredController = nullptr;
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
    origin(renderContext, itemRenderChunkType, item, x, y,
           lightMultiplier, alphaMultiplier, scale, frame,
           allowAnimation, zOrder, customIconUVCoords);

    if (injectingPreview || !settings()->on(Feature::ShulkerPreview)) return;
    liveItemRenderContext.renderer = this;
    liveItemRenderContext.context = &renderContext;
    liveItemRenderContext.chunkType = itemRenderChunkType;
    liveItemRenderContext.frame = frame;
    liveItemRenderContext.zOrder = zOrder;
    liveItemRenderContext.threadId = GetCurrentThreadId();
    liveItemRenderContext.capturedAt = GetTickCount64();
    if (!liveItemCaptureLogged) {
        liveItemCaptureLogged = true;
        logGuiInfo("ShulkerPreview live renderGuiItemInChunk context captured");
    }
}

void clear() {
    std::lock_guard lock(snapshotMutex);
    snapshot = {};
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

bool decode(ItemStack const& box,std::string const& collection,int index,
            unsigned long long generation,std::uint64_t hash,bool fromHotbar) {
    Snapshot next;
    next.title=box.getHoverName();
    next.itemType=box.getTypeName();
    next.collection=collection;
    next.index=index;
    next.tagHash=hash;
    next.generation=generation;
    next.observed=std::chrono::steady_clock::now();
    next.valid=true;
    next.fromHotbar=fromHotbar;
    if (box.mUserData) {
        CompoundTag const* root = box.mUserData.get();
        if (root) {
            ListTag const* items = findItemsList(*root);
            if (items) {
                if (items->size()>27) return false;
                for (auto const& wrapped:*items) {
                    auto const& tag=*wrapped;
                    if (tag.getId()!=Tag::Compound) return false;
                    auto const& entry=static_cast<CompoundTag const&>(tag);
                    int const slot=numericTag(entry,"Slot").value_or(
                        numericTag(entry,"slot").value_or(-1));
                    int const count=numericTag(entry,"Count").value_or(
                        numericTag(entry,"count").value_or(0));
                    if (!storeShulkerSlot(next.slotViews,slot,count)) return false;
                    ItemStack item=ItemStack::fromTag(entry);
                    if (item.isNull()) return false;
                    auto& target=next.slots[slot];
                    target.name=item.getHoverName();
                    target.count=static_cast<std::uint8_t>(count);
                    target.icon=std::make_shared<ItemStack>(item);
                }
            }
        }
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
    auto& ui=event.uiRenderContext();
    auto const size=event.screenView().mSize.get();
    auto const pointer=event.screenView().mPointerLocationPrevious.get();
    auto const& options=settings()->shulkerOptions;

    // Legacy Praxis PanelLayout values: 72px cells, 6px gaps, 24px title,
    // user scale with the proven 1.08 readability boost and adaptive fit.
    constexpr float baseWidth=9.0f*72.0f+8.0f*6.0f+16.0f;
    constexpr float baseHeight=3.0f*72.0f+2.0f*6.0f+24.0f+6.0f+18.0f;
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
    float const canvasWidth=9.0f*slotSize+8.0f*gap;
    float const canvasHeight=3.0f*slotSize+2.0f*gap;
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

    for(int slot=0;slot<27;++slot) {
        float const sx=originX+(slot%9)*pitch;
        float const sy=originY+(slot/9)*pitch;
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
        if(!item.icon)continue;

        float const iconInset=6.0f*scale;
        float const iconPhysicalSize=slotSize-iconInset*2.0f;
        float const nativeScale=std::clamp(iconPhysicalSize/16.0f,0.1f,6.0f);
        renderer->renderGuiItemNew(
            actorContext,*item.icon,0,
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
    float previewScale
) {
    LegacyPanelLayout layout{};
    constexpr float baseWidth=9.0f*72.0f+8.0f*6.0f+16.0f;
    constexpr float baseHeight=
        3.0f*72.0f+2.0f*6.0f+24.0f+6.0f+18.0f;
    float fitScale=2.0f;
    if(displayWidth>0.0f&&displayHeight>0.0f) {
        fitScale=std::min(
            displayWidth*0.92f/baseWidth,
            displayHeight*0.86f/baseHeight
        );
    }

    // Exact legacy Praxis layout constants.
    constexpr float visualScaleBoost=1.08f;
    layout.scale=std::clamp(
        std::min(previewScale*visualScaleBoost,fitScale),
        0.75f,2.0f
    );
    layout.slotSize=72.0f*layout.scale;
    layout.gap=6.0f*layout.scale;
    layout.pitch=layout.slotSize+layout.gap;
    layout.titleFontSize=24.0f*layout.scale;
    layout.canvasWidth=9.0f*layout.slotSize+8.0f*layout.gap;
    layout.canvasHeight=3.0f*layout.slotSize+2.0f*layout.gap;
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

bool drawSnapshotAtTooltip(
    MinecraftUIRenderContext& ui,
    IClientInstance& client,
    Snapshot const& current
) {
    TooltipFrameMetrics const metrics=resolveTooltipFrameMetrics(client);
    if(!metrics.valid)return false;

    auto const& options=settings()->shulkerOptions;
    LegacyPanelLayout const layout=buildLegacyPanelLayout(
        metrics.displayWidth,
        metrics.displayHeight,
        metrics.cursorX,
        metrics.cursorY,
        current.fromHotbar,
        options.previewScale
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

    ui.fillRectangle(
        rectPixels(metrics,x,y,panelWidth,panelHeight),
        background,1.0f
    );

    bool const borrowedContextReady=
        liveItemRenderContext.renderer
        && liveItemRenderContext.context
        && liveItemRenderContext.threadId==GetCurrentThreadId()
        && liveItemRenderContext.capturedAt
        && GetTickCount64()>=liveItemRenderContext.capturedAt
        && GetTickCount64()-liveItemRenderContext.capturedAt<=100
        && &liveItemRenderContext.context->mScreenContext==&ui.mScreenContext;

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

    for(int slot=0;slot<27;++slot) {
        float const sx=originX+(slot%9)*pitch;
        float const sy=originY+(slot/9)*pitch;
        ui.fillRectangle(rectPixels(metrics,sx,sy,slotSize,slotSize),cellColor,1.0f);

        auto const& item=current.slots[slot];
        if(!item.icon)continue;

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
            injectingPreview=true;
            // Exact legacy final-pass call. Do not substitute
            // renderGuiItemInChunk here: some flat 2D items vanish on that
            // path while block-model items still render, which matches the
            // partial-icons failure observed on 26.51.
            nativeRenderer->renderGuiItemNew(
                *itemContext,
                *item.icon,
                0,
                lx,
                ly,
                false,
                1.0f,
                1.0f,
                nativeScale,
                17
            );
            injectingPreview=false;
        }


    }
    if(!tooltipDrawLogged) {
        tooltipDrawLogged=true;
        char message[256]{};
        sprintf_s(
            message,
            "ShulkerPreview legacy tooltip-boundary draw reached: borrowedContext=%d ownedFallback=%d scale=%.3f panel=%.1fx%.1f",
            borrowedContextReady?1:0,borrowedContextReady?0:1,scale,panelWidth,panelHeight
        );
        logGuiInfo(message);
    }
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
    if(settings()->on(Feature::ShulkerPreview)) {
        auto const generation=worldGeneration.load(std::memory_order_acquire);
        auto const now=std::chrono::steady_clock::now();
        std::string const& formatted=mFormattedContent.get();
        std::string const& raw=mContent.get();

        std::lock_guard lock(snapshotMutex);
        if(snapshot.valid&&snapshot.generation==generation) {
            auto const age=std::chrono::duration_cast<std::chrono::milliseconds>(
                now-snapshot.observed).count();
            if(!snapshot.fromHotbar) {
                bool const titleMatches=
                    !snapshot.title.empty()
                    && (formatted.find(snapshot.title)!=std::string::npos
                        || raw.find(snapshot.title)!=std::string::npos);
                bool const exactActive=
                    exactContainerHoverActive.load(std::memory_order_acquire);
                if(exactActive||titleMatches) {
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
        return;
    }

    try {
        drawSnapshotAtTooltip(renderContext,client,current);
    } catch (...) {
        injectingPreview=false;
        if(!tooltipDrawExceptionLogged) {
            tooltipDrawExceptionLogged=true;
            logGuiError("ShulkerPreview tooltip-boundary native draw threw an exception");
        }
    }
}

void onUiRender(ll::event::AfterUIRenderEvent& event) {
    ensureGuiBackend();
    if (!uiEventLogged) {
        uiEventLogged = true;
        logGuiInfo("ShulkerPreview AfterUIRenderEvent active");
    }

    auto& client=event.uiRenderContext().mClient;
    bool const backendReady=client.getLocalPlayer()&&client.getItemRenderer();
    if(backendReady)
        availableFeatures.fetch_or(1u<<static_cast<unsigned>(Feature::ShulkerPreview));
    else
        availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::ShulkerPreview)));

    // Container preview rendering deliberately does not happen from
    // AfterUIRenderEvent. Legacy Praxis submits it at HoverTextRenderer's final
    // tooltip boundary using the live ItemRenderer context captured earlier in
    // the same UI frame.
    if(!backendReady)return;
    if(!settings()->on(Feature::ShulkerPreview)) {
        exactContainerHoverActive.store(false,std::memory_order_release);
        exactContainerHoverAt.store(0,std::memory_order_release);
        clear();
        lastHotbarSlot=-1;
        lastHotbarType.clear();
        return;
    }

    auto const screen=event.screenView().getScreenName();
    if(screen!="hud_screen"&&screen!="in_game_play_screen")return;
    if(!settings()->shulkerOptions.showInHotbar)return;

    auto* player=client.getLocalPlayer();
    if(!player)return;
    ItemStack const& selected=player->getSelectedItem();
    int const slot=player->getSelectedItemSlot();
    auto const type=selected.isNull()?std::string{}:selected.getTypeName();
    if(selected.isNull()||!shulkerIdentity(type)) {
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
    std::uint64_t const hash=selected.mUserData?selected.mUserData->hash():0;
    if(slot!=lastHotbarSlot||type!=lastHotbarType||hash!=lastHotbarHash
       ||generation!=lastHotbarGeneration) {
        lastHotbarSlot=slot;
        lastHotbarType=type;
        lastHotbarHash=hash;
        lastHotbarGeneration=generation;
        hotbarChangedAt=std::chrono::steady_clock::now();
        if(!decode(selected,"hotbar_items",slot,generation,hash,true)) {
            clear();
            return;
        }
    }

    auto const now=std::chrono::steady_clock::now();
    auto const elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        now-hotbarChangedAt).count();
    if(elapsed>=0&&hotbarPreviewActive(static_cast<std::uint64_t>(elapsed))) {
        std::lock_guard lock(snapshotMutex);
        if(snapshot.valid&&snapshot.fromHotbar)snapshot.observed=now;
    }
}
} // namespace

bool shulkerPreviewNeeded() noexcept {
    try {
        if(!settings()->on(Feature::ShulkerPreview))return false;
        auto const generation=worldGeneration.load(std::memory_order_acquire);
        auto const now=std::chrono::steady_clock::now();
        std::lock_guard lock(snapshotMutex);
        if(!snapshot.valid||snapshot.generation!=generation)return false;
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
    if(!context)return;
    ImGuiContext* const target=static_cast<ImGuiContext*>(context);
    ImGuiContext* const previous=ImGui::GetCurrentContext();
    if(previous!=target)ImGui::SetCurrentContext(target);

    try {
        Snapshot current;
        {
            auto const generation=worldGeneration.load(std::memory_order_acquire);
            auto const now=std::chrono::steady_clock::now();
            std::lock_guard lock(snapshotMutex);
            if(!snapshot.valid||snapshot.generation!=generation) {
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

        auto const options=settings()->shulkerOptions;
        LegacyPanelLayout const layout=buildLegacyPanelLayout(
            display.x,
            display.y,
            cursor.x,
            cursor.y,
            current.fromHotbar,
            options.previewScale
        );

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
            ImVec2 const titlePosition=ImGui::GetCursorScreenPos();
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
            ImVec2 const origin=ImGui::GetCursorScreenPos();
            ImVec2 mouse=ImGui::GetMousePos();
            if(!current.fromHotbar) {
                ImVec2 clientCursor{};
                if(readClientCursorPixelsForImGui(display,clientCursor))
                    mouse=clientCursor;
            }
            int hoveredSlot=-1;

            for(int slot=0;slot<27;++slot) {
                int const column=slot%9;
                int const row=slot/9;
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
    if (ShulkerSlotHoveredHook::hook() != 0) return false;
    if (ShulkerSlotUnhoveredHook::hook() != 0) {
        ShulkerSlotHoveredHook::unhook();
        return false;
    }
    hoverHooksInstalled = true;

    if (ShulkerInventoryItemRenderHook::hook() != 0) {
        ShulkerSlotUnhoveredHook::unhook();
        ShulkerSlotHoveredHook::unhook();
        hoverHooksInstalled = false;
        return false;
    }
    itemHookInstalled = true;

    if (ShulkerTooltipRenderHook::hook() != 0) {
        ShulkerInventoryItemRenderHook::unhook();
        itemHookInstalled = false;
        ShulkerSlotUnhoveredHook::unhook();
        ShulkerSlotHoveredHook::unhook();
        hoverHooksInstalled = false;
        return false;
    }
    tooltipHookInstalled = true;

    renderListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
        [](ll::event::AfterUIRenderEvent& event) {
            try { onUiRender(event); }
            catch (...) {
                if(!drawExceptionLogged) {
                    drawExceptionLogged=true;
                    logGuiError("ShulkerPreview observation event threw an exception");
                }
            }
        });
    if(!renderListener) {
        ShulkerTooltipRenderHook::unhook();
        tooltipHookInstalled = false;
        ShulkerInventoryItemRenderHook::unhook();
        itemHookInstalled = false;
        ShulkerSlotUnhoveredHook::unhook();
        ShulkerSlotHoveredHook::unhook();
        hoverHooksInstalled = false;
        return false;
    }

    logGuiInfo("ShulkerPreview legacy hook chain installed: hover + inventory item + tooltip boundary");
    return true;
}
bool removeShulkerPreview() noexcept {
    bool ok = true;
    if(renderListener) {
        bool const removed=ll::event::EventBus::getInstance().removeListener<ll::event::AfterUIRenderEvent>(renderListener);
        if(removed) renderListener.reset();
        else ok = false;
    }
    if (tooltipHookInstalled) {
        if (ShulkerTooltipRenderHook::unhook() != 0) ok = false;
        else tooltipHookInstalled = false;
    }
    if (itemHookInstalled) {
        if (ShulkerInventoryItemRenderHook::unhook() != 0) ok = false;
        else itemHookInstalled = false;
    }
    if (hoverHooksInstalled) {
        if (ShulkerSlotUnhoveredHook::unhook() != 0) ok = false;
        if (ShulkerSlotHoveredHook::unhook() != 0) ok = false;
        if (ok) hoverHooksInstalled = false;
    }
    liveItemRenderContext = {};
    exactContainerHoverActive.store(false, std::memory_order_release);
    exactContainerHoverAt.store(0, std::memory_order_release);
    clearExactHover();
    clear();
    availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::ShulkerPreview)));
    return ok;
}
} // namespace praxis::companion
