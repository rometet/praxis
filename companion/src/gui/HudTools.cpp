#include "gui/HudTools.h"
#include "gui/JapaneseText.h"
#include "gui/PresentationHints.h"
#include "api/HudControlApi.h"
#include "map/MapRuntime.h"
#include "gui/GuiBridge.h"
#include "runtime/Runtime.h"
#include "core/InventoryActionPolicy.h"
#include <atomic>
#include <memory>
#ifndef PRAXIS_HUD_TOOLS_HEADLESS
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#endif
#include <Windows.h>
#include <imgui.h>
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
#include <imgui_internal.h>
#endif
#include <vector>
#include <string>
#include <cstdio>
#include <cctype>

namespace praxis::companion {
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
std::array<bool,256> unifiedHotkeyTestKeys{};
bool unifiedHotkeyTestOwner{true};
HudControlApiV1 unifiedHotkeyTestApi{};
std::vector<UnifiedHotkeyUiProbe> unifiedHotkeyUiProbes;
std::array<float,4> unifiedHotkeyHeaderProbe{};
#endif
namespace {
struct OptionalLholo {
    HMODULE module{};HudControlApiV1 api{};
    std::shared_ptr<void> owner;
    OptionalLholo() noexcept {
#ifndef PRAXIS_HUD_TOOLS_HEADLESS
        try {
            auto mod=ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
            if(!mod||mod->getType()!=ll::mod::NativeModManagerName||!mod->isEnabled())return;
            auto native=std::static_pointer_cast<ll::mod::NativeMod>(mod);
            if(!native->getHandle()||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(native->getHandle()),&module))return;
            owner=native; // Keep LHolo's NativeMod& and module valid for all API calls.
            auto getter=reinterpret_cast<HudControlGetApiV1>(GetProcAddress(module,"lholo_get_hud_control_api_v1"));
            api.size=sizeof(api);
            if(!getter||!getter(1,&api)||api.version!=1||api.size!=sizeof(api))api={};
        } catch(...) {api={};}
#else
        api=unifiedHotkeyTestApi;
#endif
    }
    ~OptionalLholo(){if(module)FreeLibrary(module);}
    OptionalLholo(OptionalLholo const&)=delete;
    OptionalLholo& operator=(OptionalLholo const&)=delete;
    bool ready()const{return api.version==1&&api.hotkey_count&&api.read_hotkey&&api.write_hotkey&&api.read_layout&&api.write_layout;}
};
bool contains(std::string const& text,char const* pattern) {
    std::string a=text,b=pattern;for(auto& c:a)c=char(std::tolower(static_cast<unsigned char>(c)));for(auto& c:b)c=char(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b)!=std::string::npos;
}
std::string keyText(unsigned key,unsigned modifiers) {
    if(!key)return "未設定";
    std::string out{modifiers==anyModifiers?"修飾キー不問 + ":hotkeyModifierPrefix(modifiers)};
    wchar_t label[64]{};UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);LONG code=LONG(scan<<16);
    if(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_INSERT||key==VK_DELETE||key==VK_PRIOR||key==VK_NEXT||key==VK_END||key==VK_HOME)code|=1<<24;
    if(GetKeyNameTextW(code,label,64)>0){char utf8[256]{};if(WideCharToMultiByte(CP_UTF8,0,label,-1,utf8,sizeof(utf8),nullptr,nullptr)>0)out+=utf8;}
    else out+="VK "+std::to_string(key);
    return out;
}
bool hotkeyKeyDown(unsigned key) {
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    return key<unifiedHotkeyTestKeys.size() && unifiedHotkeyTestKeys[key];
#else
    return (GetAsyncKeyState(key)&0x8000)!=0;
#endif
}
unsigned currentModifiers() {
    return modifierBits(hotkeyKeyDown(VK_CONTROL),hotkeyKeyDown(VK_SHIFT),hotkeyKeyDown(VK_MENU));
}
struct Binding {std::string label;HotkeyChord chord;int group{},id{};};
std::atomic_uint64_t captureGeneration{};
struct Capture {int group{-1},id{};std::string label;std::array<bool,256> down{};std::uint64_t generation{};};
Capture capture;
bool captureOwnerActive() noexcept {
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    return unifiedHotkeyTestOwner;
#else
    DWORD process{};auto window=GetForegroundWindow();
    return window&&GetWindowThreadProcessId(window,&process)&&process==GetCurrentProcessId()&&guiInputCaptured();
#endif
}
bool currentCaptureActive() noexcept {
    return capture.group>=0&&capture.generation==captureGeneration.load(std::memory_order_acquire)&&captureOwnerActive();
}
void beginCapture(Binding const& row) {
    capture.group=row.group;capture.id=row.id;capture.label=row.label;
    capture.generation=captureGeneration.load(std::memory_order_acquire);
    for(unsigned k=0;k<256;++k)capture.down[k]=hotkeyKeyDown(k);
}
bool setBinding(RuntimeConfig& c,OptionalLholo& host,int group,int id,unsigned key,unsigned modifiers,unsigned operation,std::uint32_t& bindingEdits) {
    if(group==3)return host.ready()&&host.api.write_hotkey(id,key,praxisModifiersToLholo(modifiers),operation);
    if(group==4)return setMapActionBinding(static_cast<map::MapAction>(id),operation==0?int(key):0);
    RuntimeConfig defaults;
    if(group==5){
        if(id<0||id>=int(InventoryAction::Count))return false;
        int* value=nullptr;unsigned* mods=nullptr;
        switch(InventoryAction(id)){
        case InventoryAction::Sort:value=&c.inventorySortOptions.actionKey;mods=&c.inventorySortOptions.actionModifiers;break;
        case InventoryAction::SameType:value=&c.inventoryTransferOptions.sameKey;mods=&c.inventoryTransferOptions.sameModifiers;break;
        case InventoryAction::All:value=&c.inventoryTransferOptions.allKey;mods=&c.inventoryTransferOptions.allModifiers;break;
        case InventoryAction::Cancel:value=&c.inventoryTransferOptions.cancelKey;mods=&c.inventoryTransferOptions.cancelModifiers;break;
        default:return false;
        }
        *value=operation==0?int(key):0;*mods=operation==0?modifiers:anyModifiers;return true;
    }
    if(group==0){
        if(id<0||static_cast<unsigned>(id)>=featureCount
            ||!(ownedFeatureSlotMask&featureRegistryBit(static_cast<unsigned>(id))))return false;
        bindingEdits|=featureRegistryBit(static_cast<unsigned>(id));
        c.keybinds[id]=operation==0?key:operation==1?0:defaults.keybinds[id];c.keybindModifiers[id]=operation==0?modifiers:8;
    }
    if(group==1){c.zoomKey=operation==0?key:operation==1?0:defaults.zoomKey;c.zoomModifiers=operation==0?modifiers:8;}
    if(group==2){c.mapToggleKey=operation==0?key:operation==1?0:defaults.mapToggleKey;c.mapToggleModifiers=operation==0?modifiers:8;}
    return group>=0&&group<=2;
}
}
void cancelUnifiedHotkeyCapture() noexcept {captureGeneration.fetch_add(1,std::memory_order_acq_rel);}
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
bool unifiedHotkeyTestCapturing() noexcept {return currentCaptureActive();}
#endif
void drawUnifiedHotkeys(RuntimeConfig& c,bool& changed,std::uint32_t& bindingEdits) {
    if(capture.group>=0&&(capture.generation!=captureGeneration.load(std::memory_order_acquire)||!captureOwnerActive()))capture={};
    OptionalLholo host;
    static char search[128]{};static bool conflictsOnly{};static int category{};
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    unifiedHotkeyUiProbes.clear();
#endif
    ImGui::TextUnformatted("キー割当");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##hotkey_search","機能名・Mod・キー・使用場面を検索",search,sizeof(search));
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    auto const searchMin=ImGui::GetItemRectMin(),searchMax=ImGui::GetItemRectMax();
    unifiedHotkeyHeaderProbe[0]=(searchMin.x+searchMax.x)*.5f;unifiedHotkeyHeaderProbe[1]=(searchMin.y+searchMax.y)*.5f;
#endif
    ImGui::SetNextItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x));
    ImGui::Combo("##hotkey_category",&category,"すべて\0Praxis：機能ON/OFF\0Praxis：操作キー／カメラ起動\0LHolo：設計図操作\0固定キー\0");
    ImGui::Checkbox("重複だけ表示",&conflictsOnly);
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    auto const conflictsMin=ImGui::GetItemRectMin(),conflictsMax=ImGui::GetItemRectMax();
    unifiedHotkeyHeaderProbe[2]=(conflictsMin.x+conflictsMax.x)*.5f;unifiedHotkeyHeaderProbe[3]=(conflictsMin.y+conflictsMax.y)*.5f;
#endif
    std::vector<Binding> rows;
    for(unsigned i=0;i<featureCount;++i)if(ownedFeatureSlotMask&featureRegistryBit(i))rows.push_back({featureDisplayName(featureNames[i]),{unsigned(c.keybinds[i]),c.keybindModifiers[i],1},0,int(i)});
    rows.push_back({"ズーム（実行）",{unsigned(c.zoomKey),c.zoomModifiers,1},1,0});
    rows.push_back({"地図のON/OFF",{unsigned(c.mapToggleKey),c.mapToggleModifiers,1},2,0});
    auto mapKeys=mapActionBindings();constexpr std::array mapLabels{"ミニマップ切替","拡大","縮小","洞窟切替","自分を中心に","地点を追加"};
    for(unsigned i=0;i<mapKeys.size();++i)rows.push_back({mapLabels[i],{unsigned(mapKeys[i]),8,1},4,int(i)});
    auto const actionSettings=runtimeSettings(c,0);
    constexpr std::array actionLabels{"所持品の整理を実行","同種をまとめ移動","全体を移動","所持品操作を取消"};
    for(unsigned i=0;i<unsigned(InventoryAction::Count);++i)rows.push_back({actionLabels[i],inventoryActionBinding(actionSettings,InventoryAction(i)),5,int(i)});
    if(host.ready())for(unsigned i=0,n=std::min(host.api.hotkey_count(),128u);i<n;++i) {
        HudControlHotkeyV1 row{};row.size=sizeof(row);
        if(host.api.read_hotkey(i,&row)&&row.size==sizeof(row)&&row.key<=255&&row.modifiers<=7){row.label[127]=0;rows.push_back({row.label,{row.key,lholoModifiersToPraxis(row.modifiers),row.contexts},3,int(row.id)});}
    }
    rows.push_back({"Praxisメニュー (F10)",{VK_F10,8,3},-1,0});
    rows.push_back({"Minecraft全画面 (F11)",{VK_F11,8,3},-1,1});
    auto cameraRow=[](Binding const& row) {
        return row.group==0&&(row.id==static_cast<int>(Feature::Freelook)||row.id==static_cast<int>(Feature::FreeCamera));
    };
    auto effectiveChord=[&](Binding const& row) {
        auto chord=row.chord;
        if(cameraRow(row))chord.modifiers=cameraHotkeyModifiers(chord.modifiers);
        return chord;
    };
    auto bindingText=[&](Binding const& row) {return keyText(row.chord.key,effectiveChord(row).modifiers);};
    auto rowCategory=[](Binding const& row) {
        bool const camera=row.group==0&&(row.id==static_cast<int>(Feature::Freelook)||row.id==static_cast<int>(Feature::FreeCamera)||row.id==static_cast<int>(Feature::OffhandSwap));
        return camera?2:row.group==0?1:row.group==3?3:row.group==-1?4:2;
    };
    auto categoryLabel=[](int value) {return value==1?"Praxis：機能ON/OFF":value==2?"Praxis：操作キー／カメラ起動":value==3?"LHolo：設計図操作":"固定キー";};
    auto contextLabel=[&](Binding const& row) {return cameraRow(row)&&row.chord.modifiers==0?"ゲーム操作中 / 単キー（修飾キー不問）":row.group==0&&row.id==static_cast<int>(Feature::OffhandSwap)?"ゲーム操作中 / 実行":row.chord.contexts==4?"所持品・収納画面 / 実行":row.chord.contexts==1?"ゲーム操作中":"メニュー操作・ゲーム中";};
    auto conflictFor=[&](Binding const& row) {
        std::string with;
        for(auto const& other:rows)if(&row!=&other&&hotkeyConflict(effectiveChord(row),effectiveChord(other))){
            if(!with.empty())with+=" / ";with+=categoryLabel(rowCategory(other));with+="：";with+=other.label;
        }
        return with;
    };
    unsigned conflicts=0;for(unsigned a=0;a<rows.size();++a)for(unsigned b=a+1;b<rows.size();++b)if(hotkeyConflict(effectiveChord(rows[a]),effectiveChord(rows[b])))++conflicts;
    if(ImGui::GetContentRegionAvail().x>240.f)ImGui::SameLine();
    ImGui::Text("重複: %u",conflicts);
    if(ImGui::CollapsingHeader("操作説明")){
        ImGui::TextWrapped("「割当」を選びキーを押す。Ctrl・Shift・Altも使用できます。Escで取消。「解除」は未設定、「初期化」は既定値。変更は現在の設定へ保存されます。");
        ImGui::TextWrapped("機能ON/OFFキーと操作を実行するキーは別です。所持品の実行キーは操作キー欄と機能詳細で設定できます。「修飾キー不問」は従来の割当です。F10・F11は固定。LHoloのAlt+ホイールはLHolo側でON/OFFします。");
    }
    if(!host.ready())disabledTextWrapped("LHolo連携は未準備です。LHolo側のキー割当を使えます。");
    float const footer=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##unified_hotkeys",{0,std::max(40.f,ImGui::GetContentRegionAvail().y-footer)},ImGuiChildFlags_Borders);
    auto const& style=ImGui::GetStyle();
    float const assignWidth=ImGui::CalcTextSize("割当").x+style.FramePadding.x*2;
    float const clearWidth=ImGui::CalcTextSize("解除").x+style.FramePadding.x*2;
    float const resetWidth=ImGui::CalcTextSize("初期化").x+style.FramePadding.x*2;
    float const controlsWidth=assignWidth+clearWidth+resetWidth+style.ItemSpacing.x*2;
    bool const compact=ImGui::GetContentRegionAvail().x < controlsWidth+360.f;
    bool anyVisible{};
    for(int section=1;section<=4;++section){
        if(category && category!=section)continue;
        std::vector<Binding const*> visible;
        for(auto const& row:rows)if(rowCategory(row)==section){
            auto const with=conflictFor(row);
            if(conflictsOnly&&with.empty())continue;
            std::string const searchable=std::string(categoryLabel(section))+" "+row.label+" "+contextLabel(row)+" "+bindingText(row);
            if(contains(searchable,search))visible.push_back(&row);
        }
        if(visible.empty())continue;
        anyVisible=true;ImGui::PushID(section);ImGui::SeparatorText(categoryLabel(section));
        bool const table=!compact&&ImGui::BeginTable("##bindings",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_PadOuterX);
        if(table){
            ImGui::TableSetupColumn("機能・使用場面",ImGuiTableColumnFlags_WidthStretch,.55f);
            ImGui::TableSetupColumn("現在キー",ImGuiTableColumnFlags_WidthStretch,.45f);
            ImGui::TableSetupColumn("変更",ImGuiTableColumnFlags_WidthFixed,controlsWidth+style.CellPadding.x*2);
            ImGui::TableHeadersRow();
        }
        for(auto const* pointer:visible){
            auto const& row=*pointer;
            ImGui::PushID(row.group);ImGui::PushID(row.id);
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
            UnifiedHotkeyUiProbe probe{};probe.group=row.group;probe.id=row.id;probe.category=section;probe.compact=compact;
            auto record=[&](int control){
                auto const a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                auto* window=ImGui::GetCurrentWindow();
                if(ImGui::IsItemVisible()){
                    probe.visible=true;
                    probe.withinClip=probe.withinClip&&a.x>=window->ClipRect.Min.x-.5f&&b.x<=window->ClipRect.Max.x+.5f;
                }
                float const x=(a.x+b.x)*.5f,y=(a.y+b.y)*.5f;
                if(control==0){probe.assignX=x;probe.assignY=y;}
                else if(control==1){probe.clearX=x;probe.clearY=y;}
                else {probe.resetX=x;probe.resetY=y;}
            };
#endif
            if(table){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);}
            ImGui::TextWrapped("%s",row.label.c_str());
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
            probe.nameBottom=ImGui::GetItemRectMax().y;
#endif
            disabledTextWrapped(contextLabel(row));
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
            probe.contextTop=ImGui::GetItemRectMin().y;probe.contextBottom=ImGui::GetItemRectMax().y;
#endif
            // Draw all description lines in column zero before changing
            // columns. Returning to a table column restores its row origin.
            auto const with=conflictFor(row);
            if(!with.empty()){
                ImGui::PushStyleColor(ImGuiCol_Text,{1.f,.72f,.28f,1.f});
                ImGui::TextWrapped("重複相手: %s",with.c_str());ImGui::PopStyleColor();
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
                probe.conflictTop=ImGui::GetItemRectMin().y;
#endif
            }
            if(table)ImGui::TableSetColumnIndex(1);
            if(compact)ImGui::TextWrapped("現在キー: %s",bindingText(row).c_str());
            else ImGui::TextWrapped("%s",bindingText(row).c_str());
            if(table)ImGui::TableSetColumnIndex(2);
            if(row.group>=0){
                if(ImGui::Button("割当",{assignWidth,0}))beginCapture(row);
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
                record(0);
#endif
                if(!compact || ImGui::GetContentRegionAvail().x>=assignWidth+clearWidth+style.ItemSpacing.x)ImGui::SameLine();
                if(ImGui::Button("解除",{clearWidth,0}))changed=setBinding(c,host,row.group,row.id,0,0,1,bindingEdits)||changed;
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
                record(1);
#endif
                if(!compact || ImGui::GetContentRegionAvail().x>=controlsWidth)ImGui::SameLine();
                if(ImGui::Button("初期化",{resetWidth,0}))changed=setBinding(c,host,row.group,row.id,0,0,2,bindingEdits)||changed;
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
                record(2);unifiedHotkeyUiProbes.push_back(probe);
#endif
            }else disabledTextWrapped(row.group==-2?"機能詳細で設定":"変更できません");
            if(compact)ImGui::Separator();
            ImGui::PopID();ImGui::PopID();
        }
        if(table)ImGui::EndTable();ImGui::PopID();
    }
    if(!anyVisible)disabledTextWrapped("条件に一致する割当はありません。");
    ImGui::EndChild();
    if(ImGui::Button("全割当を初期化"))for(auto const& row:rows)if(row.group>=0)changed=setBinding(c,host,row.group,row.id,0,0,2,bindingEdits)||changed;
    if(capture.group>=0){
        ImGui::OpenPopup("キーを割当##unified");
        auto const display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSizeConstraints({std::min(280.f,display.x-24),0},{std::max(160.f,display.x-24),std::max(140.f,display.y-24)});
        if(ImGui::BeginPopupModal("キーを割当##unified",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s",capture.label.c_str());
            ImGui::TextWrapped("キー入力待ち / Escで取消 / F10・F11は固定");
            if(capture.group==4)disabledTextWrapped("地図の操作キーは修飾キーの有無を問いません。");
            bool cancel=ImGui::Button("取消");
            for(unsigned key=VK_BACK;currentCaptureActive()&&key<256;++key){
                bool down=hotkeyKeyDown(key),edge=down&&!capture.down[key];capture.down[key]=down;
                if(!currentCaptureActive()){cancel=true;break;}
                if(!edge||key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_LMENU||key==VK_RMENU||key==VK_LWIN||key==VK_RWIN||key==VK_F10||key==VK_F11)continue;
                if(key==VK_ESCAPE)cancel=true;
                else {
                    auto const modifiers=currentModifiers();
                    if(!currentCaptureActive())cancel=true;
                    else if(setBinding(c,host,capture.group,capture.id,key,modifiers,0,bindingEdits)){changed=true;cancel=true;}
                }
                break;
            }
            if(cancel){capture={};ImGui::CloseCurrentPopup();}ImGui::EndPopup();
        }
    }
}
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
unsigned hudLayoutTestTargetCount{};
#endif
void drawHudLayoutEditor(RuntimeConfig& c,bool& changed) {
    OptionalLholo host;static HudLayoutOptions draft;static bool initialized{};static int selected{};
    static std::array<HudControlLayoutV1,2> foreign{};static bool foreignReady{},dirty{};static std::string saveStatus;
    auto refresh=[&]{draft=c.informationOptions.hudLayouts;initialized=true;dirty=false;saveStatus.clear();foreignReady=host.ready();
        for(unsigned i=0;i<2;++i){foreign[i]={};foreign[i].size=sizeof(foreign[i]);if(foreignReady&&!host.api.read_layout(i,&foreign[i]))foreignReady=false;}};
    if(!initialized||(!dirty&&draft!=c.informationOptions.hudLayouts))refresh();
    ImGui::TextUnformatted("HUD配置");
    ImGui::TextWrapped("下のプレビューでパネルを左ドラッグして移動します。倍率と基準位置を選び「保存」で確定。「初期化」は従来の位置へ戻す下書きです。初期化後も保存が必要。「読込み」で未保存の編集を取り消します。");
    if(ImGui::Button("読込み"))refresh();ImGui::SameLine();
    if(ImGui::Button("保存")){
        bool accepted=true;
        if(foreignReady){if(!host.ready())accepted=false;else for(auto const& value:foreign)if(!host.api.write_layout(&value,0))accepted=false;}
        if(accepted){c.informationOptions.hudLayouts=draft;changed=true;dirty=false;saveStatus="配置を保存しました。Praxisの配置は現在の設定と保存設定に含まれます。";}
        else saveStatus="LHoloの配置を保存できませんでした。「読込み」後に再試行してください。Praxisの下書きは未保存です。";
    }ImGui::SameLine();
    if(ImGui::Button("全体を初期化")){draft.panels={};for(auto& value:foreign){value.custom=0;value.scale=1;value.x=value.y=0;value.anchor=0;}dirty=true;}
    if(dirty)ImGui::TextDisabled("未保存の編集があります");
    if(!saveStatus.empty())ImGui::TextWrapped("%s",saveStatus.c_str());
    unsigned count=foreignReady?7:5;
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
    hudLayoutTestTargetCount=count;
#endif
    if(selected>=int(count))selected=0;
    if(ImGui::BeginCombo("対象HUD",selected<5?hudDisplayNames[selected]:selected==5?"LHolo 設計図":"LHolo 材料")){
        for(unsigned i=0;i<count;++i)if(ImGui::Selectable(i<5?hudDisplayNames[i]:i==5?"LHolo 設計図":"LHolo 材料",selected==int(i)))selected=i;
        ImGui::EndCombo();
    }
    if(selected>=int(count))selected=0;
    HudTransform transform=selected<5?draft.panels[selected]:HudTransform{foreign[selected-5].custom!=0,foreign[selected-5].x,foreign[selected-5].y,foreign[selected-5].scale,int(foreign[selected-5].anchor)};
    bool edited=false;edited|=ImGui::Checkbox("位置を指定",&transform.custom);
    edited|=ImGui::SliderFloat("倍率",&transform.scale,.5f,3,"%.2f");
    edited|=ImGui::Combo("基準位置",&transform.anchor,"左上\0上中央\0右上\0左中央\0中央\0右中央\0左下\0下中央\0右下\0");
    edited|=ImGui::InputFloat("横のずれ",&transform.x,1,10);edited|=ImGui::InputFloat("縦のずれ",&transform.y,1,10);
    if(ImGui::Button("基準へ揃える")){transform.custom=true;transform.x=transform.y=0;edited=true;}ImGui::SameLine();
    if(ImGui::Button("初期化")){transform={};edited=true;}
    if(edited){if(validHudTransform(transform)){if(selected<5)draft.panels[selected]=transform;else {auto& value=foreign[selected-5];value.custom=transform.custom;value.x=transform.x;value.y=transform.y;value.scale=transform.scale;value.anchor=transform.anchor;}dirty=true;}}
    disabledTextWrapped("枠の大きさは目安です。LHoloの倍率は現在のHUD倍率に掛け合わせます。");
    auto display=ImGui::GetIO().DisplaySize;float cw=std::max(100.f,ImGui::GetContentRegionAvail().x),ch=std::max(80.f,std::min(360.f,ImGui::GetContentRegionAvail().y));
    float ratio=std::min(cw/std::max(1.f,display.x),ch/std::max(1.f,display.y));
    auto start=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(start,{start.x+display.x*ratio,start.y+display.y*ratio},IM_COL32(25,28,33,255));
    auto const& o=c.informationOptions;std::array<HudRect,7> defaults{{{o.x,o.y,260*o.scale,150*o.scale},{o.durabilityX,o.durabilityY,316*o.scale,268*o.scale},
        {o.playerHud.offhandX,o.playerHud.offhandY,244*o.playerHud.offhandScale,64*o.playerHud.offhandScale},{o.playerHud.foodX,o.playerHud.foodY,250*o.playerHud.foodScale,85*o.playerHud.foodScale},
        {10,300,240,125},{12,display.y-180,330,168},{display.x-330,display.y-180,318,168}}};
    for(unsigned i=0;i<count;++i){
        HudTransform t=i<5?draft.panels[i]:HudTransform{foreign[i-5].custom!=0,foreign[i-5].x,foreign[i-5].y,foreign[i-5].scale,int(foreign[i-5].anchor)};
        auto box=defaults[i];if(t.custom){
            float legacyScale=i<2?o.scale:i==2?o.playerHud.offhandScale:i==3?o.playerHud.foodScale:1.f;
            box.width*=t.scale/legacyScale;box.height*=t.scale/legacyScale;
        }
        box=transformHud(box,t,display.x,display.y);box=placeHud(box,display.x,display.y,{});
        ImVec2 a{start.x+box.x*ratio,start.y+box.y*ratio},z{a.x+box.width*ratio,a.y+box.height*ratio};
        draw->AddRectFilled(a,z,i==unsigned(selected)?IM_COL32(110,65,76,220):IM_COL32(50,65,76,180));draw->AddRect(a,z,IM_COL32(180,180,195,255));
        auto saved=ImGui::GetCursorPos();ImGui::SetCursorScreenPos(a);ImGui::PushID(i);
        ImGui::InvisibleButton("##panel",{std::max(5.f,z.x-a.x),std::max(5.f,z.y-a.y)});
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s / 左ドラッグで移動",i<5?hudDisplayNames[i]:i==5?"LHolo 設計図":"LHolo 材料");
        if(ImGui::IsItemClicked())selected=i;
        if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){box.x+=ImGui::GetIO().MouseDelta.x/ratio;box.y+=ImGui::GetIO().MouseDelta.y/ratio;moveHud(t,box,display.x,display.y);
            if(i<5)draft.panels[i]=t;else {foreign[i-5].custom=1;foreign[i-5].x=t.x;foreign[i-5].y=t.y;foreign[i-5].anchor=t.anchor;}dirty=true;}
        ImGui::PopID();ImGui::SetCursorPos(saved);
        constexpr char const* labels[]{"情報","装備","左手","満腹","チャンク","設計図","材料"};
        ImVec4 clip{a.x+1,a.y+1,z.x-1,z.y-1};
        draw->AddText(ImGui::GetFont(),std::clamp(ImGui::GetFontSize()*ratio,8.f,12.f),{a.x+2,a.y+2},IM_COL32_WHITE,labels[i],nullptr,0,&clip);
    }
    ImGui::Dummy({display.x*ratio,display.y*ratio});
    if(!foreignReady)disabledTextWrapped("LHoloの配置連携は未準備です。Praxisの配置は編集できます。");
}
}
