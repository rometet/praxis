#include "render/InventoryHud.h"
#include "render/InformationHud.h"
#include "render/ShulkerPreview.h"
#include "runtime/Runtime.h"
#include "platform/InventorySnapshot.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include <Windows.h>
#include <atomic>
namespace praxis::companion {
namespace {
void drawInventoryHudFrame(ll::event::AfterUIRenderEvent& event) {
    auto config=runtimeConfigSnapshot();auto kind=guiBackendKind();
    if(!runtimeReady.load()||!config.inventoryHud||(kind!=GuiBackendKind::LholoBridge&&kind!=GuiBackendKind::Standalone))return;
    auto screen=event.screenView().getScreenName();
    if(screen!="hud_screen"&&screen!="in_game_play_screen")return;
    auto& ui=event.uiRenderContext();auto& client=ui.mClient;
    auto top=client.getTopScreenName();
    if((top!="hud_screen"&&top!="in_game_play_screen")||guiInputCaptured())return;
    auto* player=client.getLocalPlayer();auto* renderer=client.getItemRenderer();
    if(!player||!player->mInventory||!renderer)return;
    auto const generation=worldGeneration.load();int const dimension=player->getDimensionId();
    // ItemStack copy is the same owned native snapshot contract used by the
    // container preview. Every copy/context dies before this UI callback returns.
    std::array<std::unique_ptr<ItemStack>,inventoryHudCells> owned;
    std::size_t bytes{};unsigned occupied{};
    for(unsigned cell=0;cell<inventoryHudCells;++cell) {
        auto const& stack=player->getInventory().getItem(inventoryHudSlot(cell));
        auto value=inventory::stackValue(stack);bytes+=value.key.size();
        if(bytes>1048576)return;
        if(value.count){owned[cell]=std::make_unique<ItemStack>(stack);++occupied;}
    }
    RECT rect{};DWORD process{};auto window=GetActiveWindow();
    if(!window||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId()||!GetClientRect(window,&rect))return;
    float width=float(rect.right-rect.left),height=float(rect.bottom-rect.top);if(width<=0||height<=0)return;
    auto gui=client.getGuiData();
    auto logical=gui->mScreenSizeData.get().clientUIScreenSize.get();
    if(!std::isfinite(logical.x)||!std::isfinite(logical.y)||logical.x<=0||logical.y<=0)return;
    float lx=logical.x/width,ly=logical.y/height;
    auto const& o=config.inventoryHudOptions;auto const& info=config.informationOptions;
    auto const& transform=info.hudLayouts.inventory;
    auto panel=inventoryHudLayout(o,transform,width,height);
    auto offhand=info.playerHud.offhandHud?transformHud({info.playerHud.offhandX,info.playerHud.offhandY,
        244*info.playerHud.offhandScale,64*info.playerHud.offhandScale},info.hudLayouts.panels[unsigned(HudId::Offhand)],width,height):HudRect{};
    std::array obstacles{paperDollBounds(lx,ly),informationHudBounds(),durabilityHudBounds(),offhand};
    panel=placeHud(panel,width,height,obstacles);float scale=panel.width/304.f;if(scale<.25f)return;
    auto& font=ui.mDebugTextFontHandle->getFont();float lineHeight=font.getWrapHeight();if(!std::isfinite(lineHeight)||lineHeight<=0)return;
    auto area=[&](float x,float y,float w,float h){return RectangleArea{x*lx,(x+w)*lx,y*ly,(y+h)*ly};};
    auto text=[&](float x,float y,float w,std::string value,float size) {
        TextMeasureData measure{nativeTextScale(size*scale,ly,lineHeight),0,false,false,false,ui::TextAlignment::Left};CaretMeasureData caret{0,false};
        ui.drawText(font,area(x,y,w,18*scale),std::move(value),mce::Color{.95f,.95f,.95f},1,ui::TextAlignment::Left,measure,caret);
    };
    if(generation!=worldGeneration.load()||!runtimeReady.load()||client.getLocalPlayer()!=player||dimension!=int(player->getDimensionId()))return;
    ui.fillRectangle(area(panel.x,panel.y,panel.width,panel.height),mce::Color{.07f,.08f,.10f},.85f);
    text(panel.x+8*scale,panel.y+5*scale,288*scale,"所持品: "+std::to_string(occupied)+" / 27",14);
    BaseActorRenderContext context{ui.mScreenContext,client,client.getMinecraftGame_DEPRECATED()};
    for(unsigned cell=0;cell<inventoryHudCells;++cell) {
        float x=panel.x+(8+(cell%9)*32)*scale,y=panel.y+(28+(cell/9)*32)*scale;
        ui.fillRectangle(area(x,y,30*scale,30*scale),mce::Color{.14f,.16f,.19f},.9f);
        if(owned[cell]) {
            renderOwnedHudItem(*renderer,context,*owned[cell],(x+3*scale)*lx,(y+3*scale)*ly,
                std::clamp(24*scale*std::min(lx,ly)/16.f,.1f,6.f));
            if(o.showCount&&owned[cell]->mCount>1)text(x+12*scale,y+16*scale,18*scale,std::to_string(owned[cell]->mCount),10);
        }
        if(o.showSlotNumbers)text(x+scale,y,28*scale,std::to_string(inventoryHudSlot(cell)),9);
    }
    ui.flushText(0,std::nullopt);
}
}
// Retired product entry point. Legacy stored enabled=true must never render.
// Retain the implementation/data schema for compatibility; no event registers it.
void drawNativeInventoryHud(ll::event::AfterUIRenderEvent&) noexcept {}
}
