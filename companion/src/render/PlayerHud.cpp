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
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/core/string/HashedString.h"
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
#include <algorithm>
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
        lines.erase(std::remove_if(lines.begin(),lines.end(),[](std::string const& line){return line.empty();}),lines.end());
        float width=0;for(auto const& line:lines)width=std::max(width,ImGui::CalcTextSize(line.c_str()).x*h.foodScale);
        float height=static_cast<float>(lines.size())*ImGui::GetTextLineHeightWithSpacing()*h.foodScale;
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
namespace {
// Vanilla's hunger control is the only stable alignment source. If an active
// UI pack moves or removes it, do NOT invent a screen-center coordinate.
struct HungerBarAnchor { float x{}, y{}; };
std::optional<HungerBarAnchor> locateVanillaHunger(ScreenView& view) {
    auto const screen=view.getScreenName();
    if(screen!="hud_screen" && screen!="in_game_play_screen")return {};
    auto* tree=view.mVisualTree.get();
    if(!tree)return {};
    auto node=tree->getControlByName("hunger_rend",true);
    if(!node || node->mCachedPositionDirty)return {};
    auto const xy=*node->mCachedPosition;
    if(!std::isfinite(xy.x)||!std::isfinite(xy.y)||xy.x<0||xy.y<0)return {};
    return HungerBarAnchor{xy.x,xy.y};
}
enum class HungerFill : unsigned { Empty, Half, Full };
HungerFill portion(float value,int index) {
    if(!std::isfinite(value)||value<0.f||value>20.f)return HungerFill::Empty;
    float const amount=value-static_cast<float>(2*index);
    if(amount>=2.f)return HungerFill::Full;
    if(amount>0.f)return HungerFill::Half;
    return HungerFill::Empty;
}
void drawNativeFoodPreview(MinecraftUIRenderContext& ui,char const* path,
                           std::vector<glm::vec2> const& slots,float opacity) {
    if(slots.empty())return;
    auto pointer=ui.getTexture(ResourceLocation(Core::PathView(path)),false);
    auto const& data=pointer.mClientTexture.get();
    if(!data || data->mIsMissingTexture==IsMissingTexture::Yes)return;
    auto const& tex=data->mClientTexture;
    for(auto const pos:slots)
        ui.drawImage(tex,pos,{9.f,9.f},{0.f,0.f},{1.f,1.f},false);
    ui.flushImages(mce::Color{1.f,1.f,1.f},opacity,HashedString{"ui_textured_and_glcolor"});
}
void drawSaturationBorder(MinecraftUIRenderContext& ui,float x,float y,
                          HungerFill fill,mce::Color const& color,float alpha) {
    if(fill==HungerFill::Empty)return;
    // Small, legible gold outline around the game's own drumstick texture,
    // without replacing or covering vanilla's existing food icons.
    auto const width=fill==HungerFill::Half?4.5f:9.f;
    ui.drawRectangle(RectangleArea{x,x+width,y,y+9.f},color,alpha,1);
}
} // namespace
void drawNativeAppleSkinHud(ll::event::AfterUIRenderEvent& event) noexcept {
    try {
        if(!foodHudNeeded()||guiInputCaptured())return;
        auto pos=locateVanillaHunger(event.screenView());
        if(!pos)return;
        auto const info=informationSnapshot();
        if(!info || !informationCurrent(*info,worldGeneration.load(),GetTickCount64()))return;
        auto const hunger=exactFoodValue(info->food.hunger,20.f);
        if(!hunger)return;
        auto& ui=event.uiRenderContext();
        auto* player=ui.mClient.getLocalPlayer();
        if(!player || !player->isAlive() || player->isCreative() || player->isSpectator())return;
        auto const restoration=heldFoodRestoration(info->food,info->food.held);
        float const futureHunger=restoration?std::min(20.f,*hunger+restoration->hunger):*hunger;
        std::vector<glm::vec2> previewFull,previewHalf;
        previewFull.reserve(10);previewHalf.reserve(10);
        for(int i=0;i<10;++i) {
            auto const current=portion(*hunger,i);
            auto const after=portion(futureHunger,i);
            if(after<=current)continue;
            glm::vec2 const slot{pos->x-8.f-8.f*i,pos->y};
            if(after==HungerFill::Full)previewFull.push_back(slot);
            else previewHalf.push_back(slot);
        }
        drawNativeFoodPreview(ui,"textures/ui/hunger_full",previewFull,.5f);
        drawNativeFoodPreview(ui,"textures/ui/hunger_half",previewHalf,.5f);
        auto const saturation=exactFoodValue(info->food.saturation,20.f);
        if(!saturation)return;
        float const futureSat=restoration?std::min(20.f,*saturation+restoration->saturation):*saturation;
        constexpr mce::Color gold{1.f,.74f,.13f};
        constexpr mce::Color pale{1.f,.95f,.42f};
        for(int i=0;i<10;++i) {
            float const x=pos->x-8.f-8.f*i,y=pos->y;
            auto const now=portion(*saturation,i);
            auto const after=portion(futureSat,i);
            drawSaturationBorder(ui,x,y,now,gold,.95f);
            if(after>now)drawSaturationBorder(ui,x+.75f,y+.75f,after,pale,.88f);
        }
    } catch(...) {}
}
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
    auto const& fontPtr=ui.mDebugTextFontHandle->mDefaultFont.get();if(!fontPtr)return;auto& font=*fontPtr;float lineHeight=font.getWrapHeight();if(!std::isfinite(lineHeight)||lineHeight<=0)return;
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
