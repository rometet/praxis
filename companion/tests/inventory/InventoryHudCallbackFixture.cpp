// Whole current production bodies are extracted by the runner, not reimplemented
// here. SDK/OS objects and calls are local doubles; Minecraft is never loaded.
#include "core/RuntimePolicy.h"
#include "core/ContainerPreviewPolicy.h"
#include "gui/GuiBackendPolicy.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace praxis::companion {
enum class Fault {None,Unreadable,NbtBudget,IdentityBudget,AggregateBudget,Copy,Context,Rectangle,Text,Icon,Flush};
struct Probe {
    Fault fault{};int failCell{},copyAttempts{},copies{},destroyed{},live{},contexts{},contextDestroyed{};
    int fills{},texts{},emptyHeaders{},icons{},flushes{},retirement{},gui{},durability{},offhand{},refresh{},decodes{},previewDraws{};
    int hudLogs{},outerLogs{},frames{};bool captured{},noPlayer{};std::uint64_t ticks{10000};
} probe;
struct ItemStack;
struct Item {int mCreativeCategory{};int getRarity(ItemStack const&) const{return 0;}int getMaxDamage() const{return 0;}};
struct Tag {std::string text;std::string toSnbt() const{return text;}};
struct ItemStack {
    int slot{},mCount{1};Item* mItem{};Tag* mUserData{};std::string type{"minecraft:stone"};bool copied{};
    ItemStack()=default;
    ItemStack(ItemStack const& other):slot(other.slot),mCount(other.mCount),mItem(other.mItem),mUserData(other.mUserData),type(other.type) {
        ++probe.copyAttempts;
        if(probe.fault==Fault::Copy&&slot==inventoryHudFirstSlot+probe.failCell)throw std::bad_alloc{};
        copied=true;++probe.copies;++probe.live;
    }
    ~ItemStack(){if(copied){++probe.destroyed;--probe.live;}}
    bool isNull() const{return !mCount;}std::string getTypeName() const{return type;}
    int getMaxStackSize() const{return 64;}int getId() const{return 1;}int getAuxValue() const{return 0;}
};
enum class ItemLockMode {None};
struct ItemLockHelper {static auto getItemLockMode(ItemStack const&){return ItemLockMode::None;}};
bool previewItemWithinBudget(ItemStack const& stack) noexcept {
    return !(probe.fault==Fault::NbtBudget&&stack.slot==inventoryHudFirstSlot+probe.failCell);
}
struct Inventory {
    std::array<ItemStack,36> items;ItemStack const& getItem(unsigned slot) const{return items.at(slot);}
};
struct LocalPlayer {
    Inventory inventory;Inventory* mInventory{&inventory};ItemStack selected;int dimension{};
    Inventory& getInventory(){return inventory;}int getDimensionId() const{return dimension;}
    ItemStack const& getSelectedItem() const{return selected;}int getSelectedItemSlot() const{return 1;}
};
struct ItemRenderer {
    template<class Context>void renderGuiItemNew(Context&,ItemStack const&,int,float,float,bool,float,float,float,int) {
        ++probe.icons;if(probe.fault==Fault::Icon&&probe.icons==probe.failCell+1)throw std::runtime_error("native icon draw");
    }
};
struct Vec2 {float x{1920},y{1080};};
template<class T>struct Field {T value;T& get(){return value;}};
struct ScreenSize {Field<Vec2> clientUIScreenSize;};
struct GuiData {Field<ScreenSize> mScreenSizeData;};
struct IClientInstance {
    LocalPlayer player;ItemRenderer renderer;GuiData gui;
    LocalPlayer* getLocalPlayer(){return probe.noPlayer?nullptr:&player;}
    ItemRenderer* getItemRenderer(){return &renderer;}GuiData* getGuiData(){return &gui;}
    std::string getTopScreenName(){return "hud_screen";}void* getMinecraftGame_DEPRECATED(){return nullptr;}
};
struct Font {float getWrapHeight() const{return 18;}};
struct FontHandle {Font font;Font& getFont(){return font;}};
struct RectangleArea {float left,right,top,bottom;};
namespace mce {struct Color {float r,g,b;};}
namespace ui {enum class TextAlignment {Left};}
struct TextMeasureData {float scale;int unused;bool a,b,c;ui::TextAlignment alignment;};
struct CaretMeasureData {int unused;bool flag;};
struct MinecraftUIRenderContext {
    IClientInstance& mClient;void* mScreenContext{};FontHandle font;FontHandle* mDebugTextFontHandle{&font};
    void fillRectangle(RectangleArea,mce::Color,float) {
        ++probe.fills;if(probe.fault==Fault::Rectangle)throw std::runtime_error("native rectangle draw");
    }
    void drawText(Font&,RectangleArea,std::string,float) = delete;
    void drawText(Font&,RectangleArea,std::string text,mce::Color,float,ui::TextAlignment,TextMeasureData,CaretMeasureData) {
        ++probe.texts;if(probe.fault==Fault::Text)throw std::runtime_error("native text draw");
        if(text.find("0 / 27")!=std::string::npos)++probe.emptyHeaders;
    }
    void flushText(int,std::optional<int>) {
        ++probe.flushes;if(probe.fault==Fault::Flush)throw std::runtime_error("native text flush");
    }
};
struct BaseActorRenderContext {
    BaseActorRenderContext(void*,IClientInstance&,void*) {
        if(probe.fault==Fault::Context)throw std::runtime_error("native render context");
        ++probe.contexts;
    }
    ~BaseActorRenderContext(){++probe.contextDestroyed;}
};
struct ScreenView {std::string getScreenName() const{return "hud_screen";}};
struct RECT {int left{},top{},right{1920},bottom{1080};};using DWORD=unsigned long;
void* GetActiveWindow(){return &probe;}DWORD GetCurrentProcessId(){return 17;}
bool GetWindowThreadProcessId(void*,DWORD* value){*value=17;return true;}
bool GetClientRect(void*,RECT*){return true;}std::uint64_t GetTickCount64(){return ++probe.ticks;}
RuntimeConfig config;
RuntimeConfig runtimeConfigSnapshot(){return config;}
GuiBackendKind guiBackendKind() noexcept{return GuiBackendKind::Standalone;}
bool guiInputCaptured() noexcept{return probe.captured;}
InformationOptions const* informationOptions(){return &config.informationOptions;}
HudRect informationHudBounds() noexcept{return {};}
HudRect durabilityHudBounds() noexcept{return {};}
void logGuiInfo(char const*) noexcept{}
void logGuiError(char const* text) noexcept {
    if(std::string_view(text).starts_with("InventoryHUD"))++probe.hudLogs;else ++probe.outerLogs;
}
bool injectingPreview{},uiEventLogged{};std::atomic_bool drawExceptionLogged{};
struct ContainerModel {};
struct Snapshot {
    bool valid{},fromHotbar{};std::uint64_t generation{},tagHash{};int index{};
    std::chrono::steady_clock::time_point observed{};
} snapshot;
std::mutex snapshotMutex;std::atomic_bool exactContainerHoverActive{};
std::atomic<std::uint64_t> exactContainerHoverAt{},lastBundleRefresh{};
int lastHotbarSlot{-1};std::string lastHotbarType;std::uint64_t lastHotbarHash{},lastHotbarGeneration{};
std::chrono::steady_clock::time_point hotbarChangedAt;
void serviceNativeIconRetirement(){++probe.retirement;}
void ensureGuiBackend() noexcept{++probe.gui;}
bool inspectionEnabled(){return true;}bool previewEnabledFor(std::string_view){return true;}
void clear(){snapshot={};}
std::optional<std::uint64_t> previewTagHash(ItemStack const&){return 123;}
std::shared_ptr<ContainerModel> hotbarBundleModel(LocalPlayer&,ItemStack const&){return {};}
bool decode(ItemStack const&,char const*,int slot,std::uint64_t generation,std::uint64_t hash,bool fromHotbar,void*,std::shared_ptr<ContainerModel>) {
    ++probe.decodes;snapshot={true,fromHotbar,generation,hash,slot,std::chrono::steady_clock::now()};return true;
}
void drawSnapshotAtTooltip(MinecraftUIRenderContext&,IClientInstance&,Snapshot const&){++probe.previewDraws;}
}
namespace ll::event::inline render {
struct AfterUIRenderEvent {
    praxis::companion::MinecraftUIRenderContext& context;praxis::companion::ScreenView view;
    auto& uiRenderContext(){return context;}auto& screenView(){return view;}
};
}
// The actual ABI declaration also carries the positive noexcept contract.
#include "ActualInventoryHudDeclaration.inc"
namespace praxis::companion {
void drawNativeDurabilityHud(ll::event::AfterUIRenderEvent&){++probe.durability;}
void drawNativeOffhandHud(ll::event::AfterUIRenderEvent&){++probe.offhand;}
void refreshExactInspection(ll::event::AfterUIRenderEvent&){++probe.refresh;}
namespace inventory {
#include "ActualInventoryStackValue.inc"
}
#include "ActualOwnedHudItem.inc"
}
#include "ActualInventoryHudModule.inc"
namespace praxis::companion {
#include "ActualPreviewUiCallback.inc"
auto actualListener =
#include "ActualPreviewUiListener.inc"
;
}

int main() {
    using namespace praxis::companion;
    unsigned checks{},scenarios{};int diagnosticTotal{};
    auto check=[&](bool condition,char const* message){++checks;if(!condition)throw std::runtime_error(message);};
#if PRAXIS_EXPECT_HUD_ISOLATION
    static_assert(noexcept(drawNativeInventoryHud(std::declval<ll::event::AfterUIRenderEvent&>())));
#endif
    for(auto type:{"minecraft:shulker_box","minecraft:bundle"})
        for(auto fault:{Fault::None,Fault::Unreadable,Fault::NbtBudget,Fault::IdentityBudget,Fault::AggregateBudget,
                        Fault::Copy,Fault::Context,Fault::Rectangle,Fault::Text,Fault::Icon,Fault::Flush})
            for(int failCell:{0,13,26}) {
                ++scenarios;probe={};probe.fault=fault;probe.failCell=failCell;
                runtimeReady=true;worldGeneration=9;availableFeatures=0;drawExceptionLogged=false;
                config={};config.inventoryHud=true;config.informationOptions.bundleShowInHotbar=true;
                snapshot={};lastHotbarSlot=-1;lastHotbarType.clear();lastHotbarHash=lastHotbarGeneration=0;
                injectingPreview=false;uiEventLogged=false;
                auto settingsValue=std::make_shared<Settings>();settingsValue->shulkerOptions.showInHotbar=true;
                publishedSettings.store(settingsValue);
                IClientInstance client;Item item;Tag tag;
                for(unsigned slot=0;slot<36;++slot){auto& stack=client.player.inventory.items[slot];stack.slot=slot;stack.mItem=&item;}
                auto& rejected=client.player.inventory.items[inventoryHudFirstSlot+failCell];
                if(fault==Fault::Unreadable)rejected.mItem=nullptr;
                if(fault==Fault::IdentityBudget){tag.text.assign(524289,'x');rejected.mUserData=&tag;}
                if(fault==Fault::AggregateBudget){tag.text.assign(400000,'x');for(unsigned slot=9;slot<12;++slot)client.player.inventory.items[slot].mUserData=&tag;}
                client.player.selected.type=type;client.player.selected.mItem=&item;
                MinecraftUIRenderContext context{client};ll::event::AfterUIRenderEvent event{context};
                actualListener(event);
                bool const throws=fault!=Fault::None&&fault!=Fault::AggregateBudget;
                bool const continues=PRAXIS_EXPECT_HUD_ISOLATION||!throws;
                check(probe.refresh==int(continues),"actual exact-hover continuation");
                check(probe.decodes==int(continues)&&probe.previewDraws==int(continues),"actual hotbar decode/renew/draw continuation");
                check(snapshot.valid==continues&&snapshot.fromHotbar==continues,"actual owned preview publication");
                check(featureAvailable(Feature::ShulkerPreview)==continues&&featureAvailable(Feature::InventoryHUD)==continues,"actual availability continuation");
                check(lastHotbarSlot==(continues?1:-1),"actual hotbar bookkeeping");
                check(probe.retirement==1&&probe.gui==1&&probe.durability==1&&probe.offhand==1,"earlier callback owners preserved");
                check(probe.live==0&&probe.copies==probe.destroyed,"owned ItemStacks unwind before listener return");
                check(probe.contexts==probe.contextDestroyed&&!injectingPreview,"owned context and actual injection guard unwind");
                check(probe.outerLogs==int(!PRAXIS_EXPECT_HUD_ISOLATION&&throws),"outer listener receives only legacy failures");
                if(fault==Fault::Unreadable||fault==Fault::NbtBudget||fault==Fault::IdentityBudget||fault==Fault::AggregateBudget||fault==Fault::Copy)
                    check(probe.fills==0&&probe.texts==0&&probe.icons==0&&probe.emptyHeaders==0,"rejected inventory is not an empty 0/27 panel");
                diagnosticTotal+=probe.hudLogs;
                // Next good frame uses the same actual listener after this failure.
                probe.fault=Fault::None;rejected.mItem=&item;
                for(auto& stack:client.player.inventory.items)stack.mUserData=nullptr;
                int const copiesBefore=probe.copies,drawsBefore=probe.previewDraws;
                actualListener(event);
                check(probe.copies-copiesBefore==27&&probe.previewDraws==drawsBefore+1,"next frame recovers HUD and preview");
                check(probe.live==0&&!injectingPreview,"recovered frame retains no owned copies/guard");
            }
    for(int boundary=0;boundary<5;++boundary) {
        ++scenarios;probe={};runtimeReady=true;worldGeneration=9;availableFeatures=UINT32_MAX;
        config={};config.inventoryHud=true;snapshot={};lastHotbarSlot=-1;lastHotbarType.clear();
        Item item;IClientInstance client;client.player.selected.type="minecraft:shulker_box";
        client.player.selected.mItem=&item;
        for(auto& stack:client.player.inventory.items)stack.mCount=0;
        if(boundary==1)config.inventoryHud=false;
        if(boundary==2)client.player.mInventory=nullptr;
        if(boundary==3)probe.captured=true;
        if(boundary==4)probe.noPlayer=true;
        MinecraftUIRenderContext context{client};ll::event::AfterUIRenderEvent event{context};
        actualListener(event);
        check(probe.emptyHeaders==int(boundary==0),"actual empty inventory remains distinct from missing/disabled/captured HUD");
        check(probe.copies==0&&probe.icons==0&&probe.live==0,"empty or unavailable HUD has no copied item");
        check(probe.refresh==1&&probe.outerLogs==0,"boundary preserves actual exact-hover continuation");
        check(probe.decodes==int(boundary!=4)&&probe.previewDraws==int(boundary!=4),"boundary preserves actual backend/hotbar continuation");
    }
    check(diagnosticTotal==(PRAXIS_EXPECT_HUD_ISOLATION?1:0),"HUD diagnostic emitted once across repeated faults");
    std::cout<<"InventoryHUD actual module+stackValue+owned-item guard+preview callback+listener "
             <<(PRAXIS_EXPECT_HUD_ISOLATION?"ISOLATED_PASS":"LEGACY_REGRESSION_REPRODUCED")
             <<" scenarios="<<scenarios<<" checks="<<checks<<" SDK_OS_BOUNDARIES=DOUBLED Minecraft=NOT_RUN\n";
}
