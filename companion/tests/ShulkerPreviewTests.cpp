#include "core/ShulkerPreviewPolicy.h"
#include "core/NativePreviewClipScope.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <optional>

using namespace praxis::companion;
struct FakeUi {
    struct Slot { std::optional<int> value{12}; auto& get(){return value;} } mSavedOriginalClippingRectangle;
    int current{34},throwSave{}; bool throwFull{},throwRestore{};
    void saveCurrentClippingRectangle(){if(throwSave==1)throw std::runtime_error("save failure");mSavedOriginalClippingRectangle.value=current;if(throwSave==2)throw std::runtime_error("partial save failure");}
    void setFullClippingRectangle(){current=100; if(throwFull)throw std::runtime_error("full clip failure");}
    void restoreSavedClippingRectangle(){if(throwRestore)throw std::runtime_error("restore failure");current=mSavedOriginalClippingRectangle.value.value_or(100);}
};
int main() {
    int checks{};
    auto require=[&](bool condition,char const* message){
        ++checks;if(!condition)throw std::runtime_error(message);
    };
    try {
        struct NativeRendererContract {
            int* context{};int* item{};int frame{},z{},calls{};float x{},y{},alpha{},light{},scale{};bool foil{};
            void renderGuiItemNew(int& c,int& i,int f,float px,float py,bool glint,float transparency,float lightMultiplier,float size,int order) {
                context=&c;item=&i;frame=f;x=px;y=py;foil=glint;alpha=transparency;light=lightMultiplier;scale=size;z=order;++calls;
            }
        } renderer;
        int context=26,item=51;
        renderPreviewNativeIcon(renderer,context,item,303.696f,202.003f,.810f);
        require(renderer.calls==1&&renderer.context==&context&&renderer.item==&item,"26.51 native icon uses admitted context and owned item exactly once");
        require(renderer.frame==0&&!renderer.foil&&renderer.z==17,"26.51 frame foil and z contract preserved");
        require(renderer.alpha==1.f&&renderer.light==1.f,"060cfbb colour fix retained: 26.51 transparency AND light=1, never 1.21 light=0");
        require(renderer.x==303.696f&&renderer.y==202.003f&&renderer.scale==.810f,"native physical-to-logical position and aspect scale preserved");
        FakeUi ui;
        {NativePreviewClipScope scope(ui);require(ui.current==100,"preview receives full clip");}
        require(ui.current==34&&ui.mSavedOriginalClippingRectangle.value==12,"caller current and previously saved clip both restored");
        try {NativePreviewClipScope scope(ui);throw std::runtime_error("item draw failure");}catch(std::runtime_error const&){}
        require(ui.current==34&&ui.mSavedOriginalClippingRectangle.value==12,"item exception restores both clip states");
        ui.throwFull=true;
        try {NativePreviewClipScope scope(ui);}catch(std::runtime_error const&){}
        require(ui.current==34&&ui.mSavedOriginalClippingRectangle.value==12,"construction exception restores both clip states");
        ui.throwFull=false;
        for(int mode:{1,2}) {
            ui.throwSave=mode;
            try {NativePreviewClipScope scope(ui);}catch(std::runtime_error const&){}
            require(ui.current==34&&ui.mSavedOriginalClippingRectangle.value==12,"save exception preserves current and caller saved clip");
        }
        ui.throwSave=0;ui.throwRestore=true;
        {NativePreviewClipScope scope(ui);require(!scope.restore(),"native restoration failure is reported");}
        require(ui.mSavedOriginalClippingRectangle.value==12,"caller saved slot restored even when native restore fails");
        ui.throwRestore=false;ui.current=34;ui.mSavedOriginalClippingRectangle.value.reset();
        {NativePreviewClipScope scope(ui);require(scope.restore()&&scope.restore(),"explicit restore is idempotent");}
        require(ui.current==34&&!ui.mSavedOriginalClippingRectangle.value,"empty caller saved slot stays empty");
        require(!previewAllowsAnimation(true,true)&&!previewAllowsAnimation(true,false),"preview disables nested native pop animation for both native requests");
        require(previewAllowsAnimation(false,true)&&!previewAllowsAnimation(false,false),"ordinary inventory animation contract preserved");
        require(shulkerIdentity("minecraft:shulker_box"),"default shulker");
        constexpr std::array colors{"white","orange","magenta","light_blue","yellow","lime",
            "pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"};
        for(auto const* color:colors) {
            std::string name="minecraft:";name+=color;name+="_shulker_box";
            require(shulkerIdentity(name),"colored shulker");
        }
        require(!shulkerIdentity("minecraft:chest"),"non-shulker rejected");
        require(!shulkerIdentity("minecraft:shulker_shell"),"shulker shell rejected");
        ShulkerSlotViews slots;
        require(slots.size()==27,"exact slot count");
        require(acceptShulkerSlot(0,1),"first slot");
        require(acceptShulkerSlot(26,64),"last slot");
        require(!acceptShulkerSlot(-1,1)&&!acceptShulkerSlot(27,1),"malformed slot rejected");
        require(!acceptShulkerSlot(0,0)&&!acceptShulkerSlot(0,128),"malformed count rejected");
        slots[0]={true,1};slots[26]={true,64};
        require(!slots[13].present&&slots[0].count==1&&slots[26].count==64,
                "empty slots and source slot indices preserved");
        ShulkerSlotViews empty{};
        require(!empty[0].present&&!empty[26].present,"empty shulker has 27 empty slots");
        require(storeShulkerSlot(empty,13,1)&&empty[13].present&&empty[13].count==1,
                "one item remains in its NBT slot");
        require(!storeShulkerSlot(empty,13,2),"duplicate slot rejected");
        ShulkerSlotViews full{};
        bool populated=true;
        for(int slot=0;slot<27;++slot)populated&=storeShulkerSlot(full,slot,slot+1);
        require(populated,"all 27 slots accepted");
        require(full[0].count==1&&full[26].count==27,"full shulker preserves counts");
        require(!storeShulkerSlot(full,27,1),"out-of-range NBT rejected");
        require(hotbarPreviewActive(0)&&hotbarPreviewActive(2199)&&!hotbarPreviewActive(2200),
                "hotbar preview expires with the item tooltip");
        std::cout<<"Shulker preview policy checks: "<<checks<<" PASS\n";
        return 0;
    } catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
