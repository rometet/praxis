#include "platform/TabTpsClient.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/mod/NativeMod.h"
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace praxis::companion::tabtps {
namespace {
namespace v2=source;
struct DisplaySettings{
    bool tabEnabled=false;
    bool actionbarEnabled=false;
    bool bossbarEnabled=false;
    bool customOverlayNoPlayerList=true;
    std::string header="";
    std::string footer="tps,mspt";
    std::string actionbar="tps,mspt,ping";
    std::string bossbar="tps,mspt,ping";
    std::string separator=" ";
    v2::Fill fill=v2::Fill::Mspt;
    int notches=20;
    float tabYOffset=22.f;
    float fontScale=1.0f;
};
struct Store{
    std::mutex mutex;
    bool loaded{};
    DisplaySettings options;
    std::filesystem::path file;
    std::string error;
};
Store& store(){static auto* p=new Store;return *p;}

bool validModules(std::string_view csv){
    std::vector<v2::Module> x;return v2::modules(csv,x);
}
std::filesystem::path configPath() {
    auto m=ll::mod::NativeMod::current();
    if(!m)return {};
    return m->getConfigDir()/"tabtps-display-v2.ini";
}
bool boolean(std::string const& value,bool& out) {
    if(value=="0"){out=false;return true;}
    if(value=="1"){out=true;return true;}
    return false;
}
void loadLocked(Store& state) {
    if(state.loaded)return;
    state.loaded=true;
    try {state.file=configPath();}catch(...){state.error="Cannot resolve mod config path";return;}
    if(state.file.empty())return;
    std::ifstream in(state.file);
    if(!in)return;
    std::string line;
    while(std::getline(in,line)){
        auto n=line.find('=');
        if(n==line.npos||n>=100)continue;
        auto key=line.substr(0,n),value=line.substr(n+1);
        if(!value.empty()&&value.back()=='\r')value.pop_back();
        auto& cfg=state.options;
        if(key=="tab")boolean(value,cfg.tabEnabled);
        else if(key=="actionbar")boolean(value,cfg.actionbarEnabled);
        else if(key=="bossbar")boolean(value,cfg.bossbarEnabled);
        else if(key=="hidePlayers")boolean(value,cfg.customOverlayNoPlayerList);
        else if(key=="header" && value.size()<=160 && validModules(value))cfg.header=value;
        else if(key=="footer" && value.size()<=160 && validModules(value))cfg.footer=value;
        else if(key=="actionModules" && value.size()<=160 && validModules(value))cfg.actionbar=value;
        else if(key=="bossModules" && value.size()<=160 && validModules(value))cfg.bossbar=value;
        else if(key=="separator" && value.size()<=16)cfg.separator=value;
        else if(key=="fill"){try {int v=std::stoi(value);if(v>=0&&v<=3)cfg.fill=static_cast<v2::Fill>(v);}catch(...){}}
        else if(key=="notches"){try {int v=std::stoi(value);if(v==0||v==6||v==10||v==12||v==20)cfg.notches=v;}catch(...){}}
        else if(key=="offsetY"){try{float v=std::stof(value);if(std::isfinite(v)&&v>=0&&v<=400)cfg.tabYOffset=v;}catch(...){}}
        else if(key=="fontScale"){try{float v=std::stof(value);if(std::isfinite(v)&&v>=0.65&&v<=2.0)cfg.fontScale=v;}catch(...){}}
    }
}
DisplaySettings snapshotSettings() {
    auto& state=store();
    std::lock_guard lock(state.mutex);
    loadLocked(state);
    return state.options;
}
void applySettings(DisplaySettings const& cfg){
    auto& state=store();
    std::lock_guard lock(state.mutex);
    loadLocked(state);
    state.options=cfg;
    if(state.file.empty())return; // settings still work in session
    try{
        std::filesystem::create_directories(state.file.parent_path());
        auto tmp=state.file;
        tmp+=".pending";
        std::ofstream out(tmp,std::ios::trunc|std::ios::binary);
        if(!out){state.error="Cannot write settings";return;}
        out<<"# Bedrock port options, not original Java TabTPS config\n";
        out<<"tab="<<cfg.tabEnabled<<"\nactionbar="<<cfg.actionbarEnabled
           <<"\nbossbar="<<cfg.bossbarEnabled
           <<"\nhidePlayers="<<cfg.customOverlayNoPlayerList
           <<"\nheader="<<cfg.header<<"\nfooter="<<cfg.footer
           <<"\nactionModules="<<cfg.actionbar<<"\nbossModules="<<cfg.bossbar
           <<"\nseparator="<<cfg.separator
           <<"\nfill="<<int(cfg.fill)<<"\nnotches="<<cfg.notches
           <<"\noffsetY="<<cfg.tabYOffset<<"\nfontScale="<<cfg.fontScale<<"\n";
        out.close();
        if(!MoveFileExW(tmp.c_str(),state.file.c_str(),
                        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            state.error="Settings save failed";
            return;
        }
        state.error.clear();
    }catch(...){state.error="Settings save failed";}
}
bool foreground(){
    DWORD owner{};
    HWND window=GetForegroundWindow();
    return window && GetWindowThreadProcessId(window,&owner)
        && owner==GetCurrentProcessId();
}
bool inputContext(){
    return runtimeReady.load(std::memory_order_acquire)
        && inputAllowed.load(std::memory_order_acquire)
        && !guiInputCaptured()&&foreground();
}
bool tabPressed(){
    return inputContext() && (GetAsyncKeyState(VK_TAB)&0x8000)!=0;
}
std::vector<v2::Colored> renderLine(
    v2::Snapshot const& s,std::string const& csv,std::string const& separator){
    std::vector<v2::Module> modules;
    if(!v2::modules(csv,modules))return {};
    return v2::render(s,modules,separator);
}
ImVec4 color(std::uint32_t c){
    return {float((c>>16)&255)/255.f,float((c>>8)&255)/255.f,float(c&255)/255.f,1.f};
}
void gradientLine(std::vector<v2::Colored> const& line){
    auto plain=v2::plain(line);
    if(plain.empty())return;
    auto* draw=ImGui::GetWindowDrawList();
    auto pos=ImGui::GetCursorScreenPos();
    float width=ImGui::CalcTextSize(plain.c_str()).x;
    float available=ImGui::GetContentRegionAvail().x;
    pos.x+=std::max(0.f,(available-width)/2.f);
    for(auto const& ch:line){
        char text[2]={ch.c,0};
        draw->AddText(pos,ImGui::GetColorU32(color(ch.rgb)),text);
        pos.x+=ImGui::CalcTextSize(text).x;
    }
    // Register the ACTUAL rendered text width with ImGui layout. A custom
    // draw-list AddText call does not participate in AlwaysAutoResize sizing.
    // Using GetContentRegionAvail() here creates a self-referential narrow
    // window and clips the right-hand modules (notably MSPT).
    ImGui::Dummy({width,ImGui::GetTextLineHeight()});
}
void displayWindow(char const* key,ImVec2 anchor,float y,
    std::vector<v2::Colored> const& header,
    std::vector<v2::Colored> const& footer,float opacity,float scale,
    bool boss=false,v2::Snapshot const* sample=nullptr,
    v2::Fill fill=v2::Fill::Mspt,int segments=20) {
    auto const size=ImGui::GetIO().DisplaySize;
    if(!(size.x>0.f&&size.y>0.f))return;
    ImGui::SetNextWindowPos({size.x*anchor.x,y},ImGuiCond_Always,{anchor.x,0.f});
    ImGui::SetNextWindowBgAlpha(opacity);
    constexpr auto flags=ImGuiWindowFlags_NoDecoration
        |ImGuiWindowFlags_AlwaysAutoResize |ImGuiWindowFlags_NoInputs
        |ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove
        |ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing;
    if(ImGui::Begin(key,nullptr,flags)){
        ImGui::SetWindowFontScale(scale);
        if(!header.empty())gradientLine(header);
        if(!footer.empty())gradientLine(footer);
        if(boss){
            auto const grade=sample?v2::bossGrade(*sample,fill):v2::Grade::Missing;
            auto const progress=sample?v2::bossProgress(*sample,fill):std::optional<float>{};
            const auto [a,b]=v2::ramp(grade);
            const auto rgb=(grade==v2::Grade::Good)?v2::green
                :(grade==v2::Grade::Medium)?v2::yellow
                :(grade==v2::Grade::Low)?v2::red:v2::gray;
            auto* draw=ImGui::GetWindowDrawList();
            const float width=std::max(190.f,ImGui::GetContentRegionAvail().x);
            auto top=ImGui::GetCursorScreenPos();
            auto const chunks=segments<=0?1:segments;
            const float gap=segments<=0?0.f:2.f;
            const float w=(width-gap*(chunks-1))/chunks;
            auto filled=progress?*progress:0.f;
            for(int i=0;i<chunks;++i){
                float x=top.x+i*(w+gap);
                draw->AddRectFilled({x,top.y},{x+w,top.y+8},
                    ImGui::GetColorU32({.22f,.22f,.22f,.9f}),2.f);
                float local=std::clamp(filled*chunks-i,0.f,1.f);
                if(local>0&&progress)
                    draw->AddRectFilled({x,top.y},{x+w*local,top.y+8},
                        ImGui::GetColorU32(color(rgb)),2.f);
            }
            ImGui::Dummy({width,8.f});
        }
    }
    ImGui::End();
}
bool inputCsv(char const* label,std::string& current,bool& changed){
    char buffer[192]{};
    std::snprintf(buffer,sizeof(buffer),"%s",current.c_str());
    if(ImGui::InputText(label,buffer,sizeof(buffer))){
        std::string proposal=buffer;
        if(proposal.size()<=160 && validModules(proposal)){
            current=proposal;changed=true;
        }
        else ImGui::TextDisabled("Invalid module id. Use tps,mspt,ping,cpu,memory,players.");
        return true;
    }
    return false;
}
}
bool suppressVanillaTabKey(int keyCode) noexcept {
    if(keyCode!=VK_TAB)return false;
    try{
        auto cfg=snapshotSettings();
        return cfg.tabEnabled && cfg.customOverlayNoPlayerList && inputContext();
    }catch(...){return false;}
}
bool tabHudNeeded() noexcept {
    try{
        auto cfg=snapshotSettings();
        return inputContext()&&(cfg.actionbarEnabled||cfg.bossbarEnabled
            ||(cfg.tabEnabled&&tabPressed()));
    }catch(...){return false;}
}
void drawTabHud(void* ctx) noexcept {
    if(!ctx||!tabHudNeeded())return;
    auto* previous=ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    try{
        const auto cfg=snapshotSettings();
        auto sample=telemetryView(GetTickCount64());
        v2::Snapshot unknown{};
        auto const& view=sample?*sample:unknown;
        if(cfg.tabEnabled&&tabPressed()){
            // Source Java tab header/footer are modelled independently.
            // Hide-players option uses an independent performance-only canvas;
            // native Bedrock player list suppression requires input hook audit.
            auto head=renderLine(view,cfg.header,cfg.separator);
            auto foot=renderLine(view,cfg.footer,cfg.separator);
            if(!head.empty()||!foot.empty())
                displayWindow("TabTPS List##tabtps", {.5f,0.f},cfg.tabYOffset,
                    head,foot,.67f,cfg.fontScale);
        }
        if(cfg.actionbarEnabled){
            auto list=renderLine(view,cfg.actionbar,cfg.separator);
            if(!list.empty()){
                const auto height=ImGui::GetIO().DisplaySize.y;
                displayWindow("Action Bar##tabtps", {.5f,0.f},
                    std::max(0.f,height-110.f),{},list,.45f,cfg.fontScale);
            }
        }
        if(cfg.bossbarEnabled){
            auto list=renderLine(view,cfg.bossbar,cfg.separator);
            displayWindow("Boss Bar##tabtps", {.5f,0.f},
                (cfg.tabEnabled&&tabPressed())?cfg.tabYOffset+76.f:20.f,
                {},list,.55f,cfg.fontScale,true,sample?&*sample:nullptr,
                cfg.fill,cfg.notches);
        }
    }catch(...){}
    ImGui::SetCurrentContext(previous);
}
void drawTabTpsSettingsPage() noexcept {
    try{
        auto cfg=snapshotSettings();
        bool changed=false;
        ImGui::TextUnformatted("TabTPS (source-based Bedrock port)");
        ImGui::TextDisabled("Original: jpenilla/TabTPS e0e55e9 (MIT)");
        ImGui::TextWrapped("Source formats; independently measured BDS 60s TPS, simulation MSPT, per-player ping, OS CPU, process RAM and online count.");
        ImGui::SeparatorText("Independent displays (OFF by default)");
        changed|=ImGui::Checkbox("Enable Tab display",&cfg.tabEnabled);
        changed|=ImGui::Checkbox("Enable Action Bar HUD",&cfg.actionbarEnabled);
        changed|=ImGui::Checkbox("Enable Boss Bar HUD",&cfg.bossbarEnabled);
        changed|=ImGui::Checkbox("Performance-only Tab overlay (no player rows drawn by Praxis)",&cfg.customOverlayNoPlayerList);
        ImGui::TextDisabled("Vanilla/other-mod Tab list suppression is not yet guaranteed by this toggle.");
        ImGui::SeparatorText("Modules, comma-separated in exact display order");
        inputCsv("Tab header (default empty)",cfg.header,changed);
        inputCsv("Tab footer",cfg.footer,changed);
        inputCsv("Action Bar",cfg.actionbar,changed);
        inputCsv("Boss Bar",cfg.bossbar,changed);
        char sep[18]{};
        std::snprintf(sep,sizeof(sep),"%s",cfg.separator.c_str());
        if(ImGui::InputText("Separator",sep,sizeof(sep))){
            cfg.separator=sep;changed=true;
        }
        ImGui::SeparatorText("Boss bar");
        int fill=static_cast<int>(cfg.fill);
        char const* types[]={"MSPT","TPS","REVERSE_MSPT","REVERSE_TPS"};
        if(ImGui::Combo("Fill mode",&fill,types,4)){
            cfg.fill=static_cast<v2::Fill>(fill);changed=true;
        }
        int notches=0;
        int const options[]{0,6,10,12,20};
        for(int i=0;i<5;++i)if(cfg.notches==options[i])notches=i;
        char const* overlays[]={"Progress","Notched 6","Notched 10","Notched 12","Notched 20"};
        if(ImGui::Combo("Overlay",&notches,overlays,5)){
            cfg.notches=options[notches];changed=true;
        }
        changed|=ImGui::SliderFloat("Tab top offset",&cfg.tabYOffset,0.f,300.f,"%.0f px");
        changed|=ImGui::SliderFloat("Scale",&cfg.fontScale,.65f,2.f,"%.2fx");
        if(changed)applySettings(cfg);
        ImGui::SeparatorText("Live values (no fallback to fake TPS)");
        auto snapshot=telemetryView(GetTickCount64());
        if(snapshot){
            auto fields=v2::render(*snapshot,{v2::Module::Tps,v2::Module::Mspt,v2::Module::Ping,
              v2::Module::Cpu,v2::Module::Memory,v2::Module::Players});
            ImGui::TextWrapped("%s",v2::plain(fields).c_str());
            if(snapshot->tpsCenti<0)ImGui::TextDisabled("60s TPS warm-up in progress.");
        }else ImGui::TextDisabled("No fresh TabTPS v2 telemetry (must connect to matching server).");
        ImGui::TextDisabled("RAM is BDS process Working Set / Private Commit, not Java heap.");
        ImGui::TextDisabled("Display updates every 250ms; server metric collection is independent.");
        {
            auto& state=store();
            std::lock_guard lock(state.mutex);
            if(!state.error.empty())ImGui::TextDisabled("%s",state.error.c_str());
        }
    }catch(...){ImGui::TextUnformatted("TabTPS settings unavailable");}
}
} // namespace praxis::companion::tabtps
