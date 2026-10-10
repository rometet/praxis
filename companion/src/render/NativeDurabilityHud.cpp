#include "render/NativeDurabilityHud.h"
#include "render/ShulkerPreview.h"
#include "render/InformationHud.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include "core/UiLayoutPolicy.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include <Windows.h>
#include <array>
namespace praxis::companion {
void drawNativeDurabilityHud(ll::event::AfterUIRenderEvent& event) {
    auto o=informationOptions();
    auto kind=guiBackendKind();
    if(!runtimeReady.load()||!o->durabilityHud
        ||(kind!=GuiBackendKind::LholoBridge&&kind!=GuiBackendKind::Standalone))return;
    auto screen=event.screenView().getScreenName();
    if(screen!="hud_screen"&&screen!="in_game_play_screen")return;
    auto& ui=event.uiRenderContext();auto& client=ui.mClient;
    auto* player=client.getLocalPlayer();auto* renderer=client.getItemRenderer();
    if(!player||!renderer)return;
    auto generation=worldGeneration.load();
    // All six borrowed stacks and native render context are used synchronously in this callback.
    std::array<ItemStack const*,6> stacks{&player->getSelectedItem(),&player->getOffhandSlot()};
    for(unsigned i=0;i<4;++i)stacks[i+2]=&player->getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i));
    InformationSnapshot values;
    for(unsigned i=0;i<stacks.size();++i) {
        auto const& stack=*stacks[i];
        if(!stack.isNull()&&previewItemWithinBudget(stack)&&stack.isDamageableItem()&&stack.mItem)
            values.durability[i]=durabilityValue(stack.getHoverName(),stack.getDamageValue(),stack.mItem->getMaxDamage());
    }
    auto rows=selectDurabilityRows(values,*o);if(rows.empty()){publishDurabilityHudBounds({},generation);return;}
    RECT rect{};HWND window=GetActiveWindow();DWORD process{};
    if(!window||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId()||!GetClientRect(window,&rect))return;
    float width=static_cast<float>(rect.right-rect.left),height=static_cast<float>(rect.bottom-rect.top);
    if(width<=0||height<=0)return;
    auto gui=client.getGuiData();
    auto logical=gui->mScreenSizeData.get().clientUIScreenSize.get();
    if(!std::isfinite(logical.x)||!std::isfinite(logical.y)||logical.x<=0||logical.y<=0)return;
    float lx=logical.x/width,ly=logical.y/height;
    auto layout=durabilityHudLayout(static_cast<unsigned>(rows.size()),width,height,*o);
    if(layout.scale<0.25f)return;
    std::array obstacles{paperDollBounds(lx,ly)};
    auto const& transform=o->hudLayouts.panels[static_cast<unsigned>(HudId::Equipment)];
    auto placed=placeHud(transformHud({layout.x,layout.y,layout.width,layout.height},transform,width,height),width,height,obstacles);
    layout.x=placed.x;layout.y=placed.y;
    auto& font=ui.mDebugTextFontHandle->getFont();
    float const logicalLineHeight=font.getWrapHeight();
    if(!std::isfinite(logicalLineHeight)||logicalLineHeight<=0)return;
    auto area=[&](float x,float y,float w,float h){return RectangleArea{x*lx,(x+w)*lx,y*ly,(y+h)*ly};};
    auto text=[&](float x,float y,float w,std::string value,float pixels){
        float scale=nativeTextScale(pixels,ly,logicalLineHeight);
        // Keep long translated/custom names on one line so they cannot cover
        // the numeric row or its bar. Trim only at UTF-8 boundaries.
        if(value.size()>1024) {
            std::size_t end=1024;
            while(end>0&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
            value.resize(end);
        }
        while(!value.empty()&&font.getLineLength(value,scale,false)>w*lx) {
            std::size_t end=value.size()-1;
            while(end>0&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
            value.resize(end);
        }
        TextMeasureData measure{scale,0,false,false,false,ui::TextAlignment::Left};CaretMeasureData caret{0,false};
        auto color=o->textColor;mce::Color fg{(color&255)/255.0f,((color>>8)&255)/255.0f,((color>>16)&255)/255.0f};
        ui.drawText(font,area(x,y,w,18*layout.scale),std::move(value),fg,1,ui::TextAlignment::Left,measure,caret);
    };
    if(generation!=worldGeneration.load()||!runtimeReady.load()||client.getLocalPlayer()!=player)return;
    mce::Color bg{0.07f,0.08f,0.10f};
    ui.fillRectangle(area(layout.x,layout.y,layout.width,layout.height),bg,0.85f);
    BaseActorRenderContext context{ui.mScreenContext,client,client.getMinecraftGame_DEPRECATED()};
    constexpr std::array labels{"メインハンド","オフハンド","頭","胸","脚","足"};
    unsigned row=0;
    for(auto const& [slot,value]:rows) {
        float x=layout.x+8*layout.scale,y=layout.y+(8+row++*42)*layout.scale;
        float iconScale=std::clamp(28*layout.scale*std::min(lx,ly)/16.0f,0.1f,6.0f);
        renderer->renderGuiItemNew(context,*stacks[slot],0,x*lx,y*ly,false,1,1,iconScale,17);
        text(x+36*layout.scale,y,264*layout.scale,std::string(labels[slot])+": "+value.name,14*layout.scale);
        if(o->durabilityMode!=2)text(x+36*layout.scale,y+15*layout.scale,264*layout.scale,durabilityText(value),14*layout.scale);
        if(o->durabilityMode!=1) {
            float by=y+(o->durabilityMode==2?20:32)*layout.scale;
            ui.fillRectangle(area(x+36*layout.scale,by,260*layout.scale,4*layout.scale),mce::Color{0.12f,0.12f,0.12f},1);
            ui.fillRectangle(area(x+36*layout.scale,by,260*layout.scale*value.ratio,4*layout.scale),mce::Color{1-value.ratio,value.ratio,0.15f},1);
        }
    }
    ui.flushText(0,std::nullopt);
    publishDurabilityHudBounds({layout.x,layout.y,layout.width,layout.height},generation);
}
} // namespace praxis::companion
