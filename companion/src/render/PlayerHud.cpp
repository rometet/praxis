#include "render/PlayerHud.h"
#include "render/InformationHud.h"
#include "render/ShulkerPreview.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include "gui/JapaneseText.h"
#include "gui/GuiBridge.h"
#ifndef PRAXIS_FOOD_HUD_TEST
#include "ll/api/event/render/UIRenderEvent.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/core/math/Color.h"
#endif
#include <Windows.h>
#include <imgui.h>

namespace praxis::companion {
bool foodHudNeeded() noexcept {
    return runtimeReady.load()&&informationOptions()->playerHud.foodHud
        &&informationCurrent(*informationSnapshot(),worldGeneration.load(),GetTickCount64());
}
void drawFoodHud(void* context) noexcept {
    if(!context||!foodHudNeeded())return;
    auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    try {
        auto const o=informationOptions();auto h=o->playerHud;auto const sample=informationSnapshot();
        auto const& transform=o->hudLayouts.panels[static_cast<unsigned>(HudId::Food)];
        if(transform.custom)h.foodScale=transform.scale;
        auto display=ImGui::GetIO().DisplaySize;
        if(!std::isfinite(display.x)||!std::isfinite(display.y)||display.x<=0||display.y<=0){ImGui::SetCurrentContext(previous);return;}
        auto lines=foodHudLinesJa(sample->food);
        float width=0;for(auto const& line:lines)width=std::max(width,ImGui::CalcTextSize(line.c_str()).x*h.foodScale);
        float height=3*ImGui::GetTextLineHeightWithSpacing()*h.foodScale;
        auto padding=ImGui::GetStyle().WindowPadding;width+=padding.x*2;height+=padding.y*2;
        std::array obstacles{informationHudBounds(),durabilityHudBounds(),paperDollBounds(sample->logicalWidth/display.x,sample->logicalHeight/display.y)};
        auto placed=placeHud(transformHud({h.foodX,h.foodY,width,height},transform,display.x,display.y),display.x,display.y,obstacles);
        ImGui::SetNextWindowPos({placed.x,placed.y},ImGuiCond_Always);ImGui::SetNextWindowSize({placed.width,placed.height},ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.6f);
        if(ImGui::Begin("Food##praxis_food",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings
            |ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav)) {
            ImGui::SetWindowFontScale(h.foodScale);ImGui::PushStyleColor(ImGuiCol_Text,o->textColor);
            for(auto const& line:lines)ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    } catch(...) {}
    ImGui::SetCurrentContext(previous);
}
#ifndef PRAXIS_FOOD_HUD_TEST
void drawNativeOffhandHud(ll::event::AfterUIRenderEvent& event) {
    auto const options=informationOptions();auto o=options->playerHud;
    auto const& transform=options->hudLayouts.panels[static_cast<unsigned>(HudId::Offhand)];
    if(transform.custom)o.offhandScale=transform.scale;
    auto kind=guiBackendKind();
    if(!runtimeReady.load()||!o.offhandHud||(kind!=GuiBackendKind::LholoBridge&&kind!=GuiBackendKind::Standalone))return;
    auto const screen=event.screenView().getScreenName();if(screen!="hud_screen"&&screen!="in_game_play_screen")return;
    auto& ui=event.uiRenderContext();auto& client=ui.mClient;
    auto* player=client.getLocalPlayer();auto* renderer=client.getItemRenderer();if(!player||!renderer)return;
    auto const generation=worldGeneration.load();auto const& stack=player->getOffhandSlot();
    bool const empty=stack.isNull();if((empty&&!o.offhandShowEmpty)||(!empty&&!previewItemWithinBudget(stack)))return;
    RECT rect{};HWND window=GetActiveWindow();DWORD process{};
    if(!window||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId()||!GetClientRect(window,&rect))return;
    float width=float(rect.right-rect.left),height=float(rect.bottom-rect.top);if(width<=0||height<=0)return;
    auto gui=client.getGuiData();
    auto logical=gui->mScreenSizeData.get().clientUIScreenSize.get();
    if(!std::isfinite(logical.x)||!std::isfinite(logical.y)||logical.x<=0||logical.y<=0)return;
    float lx=logical.x/width,ly=logical.y/height,scale=std::min({o.offhandScale,width/244.f,height/64.f});
    if(scale<.25f)return;
    std::array obstacles{paperDollBounds(lx,ly),informationHudBounds(),durabilityHudBounds()};
    auto panel=placeHud(transformHud({o.offhandX,o.offhandY,244*scale,64*scale},transform,width,height),width,height,obstacles);
    auto area=[&](float x,float y,float w,float h){return RectangleArea{x*lx,(x+w)*lx,y*ly,(y+h)*ly};};
    auto& font=ui.mDebugTextFontHandle->getFont();float lineHeight=font.getWrapHeight();if(!std::isfinite(lineHeight)||lineHeight<=0)return;
    auto text=[&](float y,std::string value) {
        float ts=nativeTextScale(14*scale,ly,lineHeight);
        if(value.size()>1024){std::size_t end=1024;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;value.resize(end);}
        while(!value.empty()&&font.getLineLength(value,ts,false)>188*scale*lx){auto end=value.size()-1;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;value.resize(end);}
        auto color=options->textColor;TextMeasureData measure{ts,0,false,false,false,ui::TextAlignment::Left};CaretMeasureData caret{0,false};
        ui.drawText(font,area(panel.x+48*scale,y,188*scale,18*scale),std::move(value),
            mce::Color{(color&255)/255.f,((color>>8)&255)/255.f,((color>>16)&255)/255.f},1,ui::TextAlignment::Left,measure,caret);
    };
    if(generation!=worldGeneration.load()||!runtimeReady.load()||client.getLocalPlayer()!=player)return;
    ui.fillRectangle(area(panel.x,panel.y,panel.width,panel.height),mce::Color{.07f,.08f,.10f},.85f);
    text(panel.y+6*scale,empty?"オフハンド: 空":"オフハンド: "+stack.getHoverName());
    if(!empty) {
        BaseActorRenderContext context{ui.mScreenContext,client,client.getMinecraftGame_DEPRECATED()};
        renderer->renderGuiItemNew(context,stack,0,(panel.x+8*scale)*lx,(panel.y+12*scale)*ly,false,1,1,
            std::clamp(28*scale*std::min(lx,ly)/16.f,.1f,6.f),17);
        if(o.offhandShowCount)text(panel.y+23*scale,"個数: "+std::to_string(stack.mCount));
        if(o.offhandShowDurability&&stack.isDamageableItem()&&stack.mItem)
            if(auto value=durabilityValue({},stack.getDamageValue(),stack.mItem->getMaxDamage()))text(panel.y+40*scale,durabilityText(*value));
    }
    ui.flushText(0,std::nullopt);
}
#endif
}
