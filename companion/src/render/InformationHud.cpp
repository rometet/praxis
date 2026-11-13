#include "render/InformationHud.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include "gui/JapaneseText.h"
#include "core/UiLayoutPolicy.h"
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <vector>
namespace praxis::companion {
namespace {
struct Bounds {HudRect rect;std::uint64_t generation{},observedMs{};};
std::atomic<std::shared_ptr<Bounds const>> equipmentBounds,infoBounds;
HudRect currentBounds(std::atomic<std::shared_ptr<Bounds const>>& published) noexcept {
    auto p=published.load();auto now=GetTickCount64();
    return p&&p->generation==worldGeneration.load()&&now>=p->observedMs&&now-p->observedMs<=1000?p->rect:HudRect{};
}
}
void publishDurabilityHudBounds(HudRect rect,std::uint64_t generation) noexcept {
    try {equipmentBounds.store(std::make_shared<Bounds const>(Bounds{rect,generation,GetTickCount64()}));}catch(...){}
}
HudRect durabilityHudBounds() noexcept {return informationOptions()->durabilityHud?currentBounds(equipmentBounds):HudRect{};}
HudRect informationHudBounds() noexcept {return informationOptions()->infoHud?currentBounds(infoBounds):HudRect{};}

bool informationHudNeeded() noexcept {
    auto const o=informationOptions();
    return runtimeReady.load()&&o->infoHud
        &&informationCurrent(*informationSnapshot(),worldGeneration.load(),GetTickCount64());
}
void drawInformationHud(void* context) noexcept {
    if(!context||!informationHudNeeded())return;
    auto* previous=ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    try {
        auto o=informationOptions();
        auto sample=*informationSnapshot();
        char number[64]{};
        // The selected ImGui host's frame estimate, not Minecraft tick rate.
        float fps=ImGui::GetIO().Framerate;
        if(std::isfinite(fps)&&fps>0) {
            std::snprintf(number,sizeof(number),"%.0f",fps);sample.values[9]=number;
            std::snprintf(number,sizeof(number),"%.2f ms",1000.0f/fps);sample.values[10]=number;
        }
        ImVec2 size=ImGui::GetIO().DisplaySize;
        if(!std::isfinite(size.x)||!std::isfinite(size.y)||size.x<=0||size.y<=0){ImGui::SetCurrentContext(previous);return;}
        auto lines=informationLinesJa(sample,*o);
        auto const& transform=o->hudLayouts.panels[static_cast<unsigned>(HudId::Information)];
        float scale=transform.custom?transform.scale:o->scale;
        float fontSize=ImGui::GetFontSize()*scale;
        float textWidth=0;
        for(auto const& line:lines)textWidth=std::max(textWidth,ImGui::GetFont()->CalcTextSizeA(fontSize,FLT_MAX,0,line.c_str()).x);
        auto padding=ImGui::GetStyle().WindowPadding;
        float panelWidth=std::min(size.x,textWidth+2*padding.x);
        float panelHeight=std::min(size.y,lines.size()*(fontSize+ImGui::GetStyle().ItemSpacing.y)+2*padding.y);
        float lx=sample.logicalWidth/size.x,ly=sample.logicalHeight/size.y;
        std::vector<HudRect> obstacles{paperDollBounds(lx,ly)};
        if(o->durabilityHud)obstacles.push_back(durabilityHudBounds());
        auto placement=placeHud(transformHud({o->x,o->y,panelWidth,panelHeight},transform,size.x,size.y),size.x,size.y,obstacles);
        infoBounds.store(std::make_shared<Bounds const>(Bounds{placement,sample.generation,GetTickCount64()}));
        ImGui::SetNextWindowPos({placement.x,placement.y},ImGuiCond_Always);
        ImGui::SetNextWindowSize({placement.width,placement.height},ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);
        constexpr auto flags=ImGuiWindowFlags_NoDecoration
            |ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing
            |ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoInputs;
        if(ImGui::Begin("Information##praxis_information",nullptr,flags)) {
            ImGui::SetWindowFontScale(scale);
            ImGui::PushStyleColor(ImGuiCol_Text,o->textColor);
            if(o->infoHud)for(auto const& line:lines)ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    } catch(...) { /* Owned-data failure must not escape a provider callback. */ }
    ImGui::SetCurrentContext(previous);
}
bool effectHudNeeded() noexcept {
    auto const o=informationOptions();
    auto const s=settings();
    return runtimeReady.load()&&s->on(Feature::EffectTimer)&&o->effectTimer.scale>0
        &&informationCurrent(*informationSnapshot(),worldGeneration.load(),GetTickCount64());
}
void drawEffectHud(void* context) noexcept {
    if(!context||!effectHudNeeded())return;
    auto* previous=ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    try {
        auto o=informationOptions();
        auto sample=*informationSnapshot();
        auto const lines=effectTimerLines(sample.effects,o->effectTimer);
        if(lines.empty()){ImGui::SetCurrentContext(previous);return;}
        auto const& transform=o->hudLayouts.effects;
        float scale=transform.custom?transform.scale:o->effectTimer.scale;
        ImVec2 size=ImGui::GetIO().DisplaySize;
        if(!std::isfinite(size.x)||!std::isfinite(size.y)||size.x<=0||size.y<=0){ImGui::SetCurrentContext(previous);return;}
        float width=0;
        for(auto const& line:lines)width=std::max(width,ImGui::CalcTextSize(line.c_str()).x*scale);
        auto padding=ImGui::GetStyle().WindowPadding;
        float panelWidth=std::min(size.x,width+2*padding.x);
        float panelHeight=std::min(size.y,lines.size()*(ImGui::GetTextLineHeight()+ImGui::GetStyle().ItemSpacing.y)+2*padding.y);
        float lx=sample.logicalWidth/size.x,ly=sample.logicalHeight/size.y;
        std::vector<HudRect> obstacles{paperDollBounds(lx,ly),informationHudBounds(),durabilityHudBounds()};
        auto placement=placeHud(transformHud({o->effectTimer.x,o->effectTimer.y,panelWidth,panelHeight},transform,size.x,size.y),
            size.x,size.y,obstacles);
        ImGui::SetNextWindowPos({placement.x,placement.y},ImGuiCond_Always);
        ImGui::SetNextWindowSize({placement.width,placement.height},ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings
            |ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoInputs;
        if(ImGui::Begin("Effects##praxis_effects",nullptr,flags)) {
            ImGui::SetWindowFontScale(scale);
            ImGui::PushStyleColor(ImGuiCol_Text,o->effectTimer.textColor);
            for(auto const& line:lines)ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    } catch(...) {}
    ImGui::SetCurrentContext(previous);
}
bool debugHudNeeded() noexcept {
    auto const o=informationOptions();
    auto const s=settings();
    if(!runtimeReady.load()||!s->on(Feature::DebugScreen))return false;
    auto const& d=o->debugScreen;
    return (d.clientPerformance||d.serverPerformance||d.lookingAt||d.worldDiagnostics)
        &&informationCurrent(*informationSnapshot(),worldGeneration.load(),GetTickCount64());
}
void drawDebugHud(void* context) noexcept {
    if (!context || !debugHudNeeded()) return;
    auto* previous=ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    try {
        auto const options=informationOptions();
        auto const sample=*informationSnapshot();
        ImVec2 const size=ImGui::GetIO().DisplaySize;
        if(!std::isfinite(size.x)||!std::isfinite(size.y)||size.x<100||size.y<100) {
            ImGui::SetCurrentContext(previous); return;
        }
        auto const& config=options->debugScreen;
        double const fps=ImGui::GetIO().Framerate;
        auto panels=javaDebugColumns(sample.debug,config,sample.values[0],sample.values[13],fps);
        if (panels.left.empty() && panels.right.empty()) {
            ImGui::SetCurrentContext(previous); return;
        }
        float const scale=std::clamp(config.scale,.5f,3.f);
        bool const sideBySide=size.x>=760.f*scale;
        float const margin=std::max(6.f,8.f*scale);
        float const available=sideBySide?(size.x-3*margin)/2.f:size.x-2*margin;
        auto const heightOf=[&](std::vector<std::string> const& lines) {
            float lineHeight=(ImGui::GetTextLineHeight()+ImGui::GetStyle().ItemSpacing.y)*scale;
            return std::min(size.y-margin,lines.size()*lineHeight+12.f*scale);
        };
        auto const widthOf=[&](std::vector<std::string> const& lines) {
            float maxWidth=0.f;
            auto* font=ImGui::GetFont();
            float const fontSize=ImGui::GetFontSize()*scale;
            for(auto const& line:lines) {
                auto const current=font->CalcTextSizeA(fontSize,FLT_MAX,0,line.c_str()).x;
                maxWidth=std::max(maxWidth,current);
            }
            return std::min(available,std::max(165.f*scale,maxWidth+15.f*scale));
        };
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings
            |ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav
            |ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoScrollbar;
        auto drawPanel=[&](char const* name,std::vector<std::string> const& lines,float x,float y) {
            if(lines.empty()||y>=size.y-margin)return;
            auto const width=widthOf(lines);
            auto const height=std::min(heightOf(lines),size.y-y-margin);
            if(height<10.f)return;
            ImGui::SetNextWindowPos({x,y},ImGuiCond_Always);
            ImGui::SetNextWindowSize({width,height},ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(.44f);
            if(ImGui::Begin(name,nullptr,flags)) {
                ImGui::SetWindowFontScale(scale);
                ImGui::PushStyleColor(ImGuiCol_Text,config.textColor);
                for(std::size_t i=0;i<lines.size();++i){
                    if(i==0) ImGui::TextUnformatted(lines[i].c_str());
                    else if(lines[i].empty()) ImGui::Spacing();
                    else ImGui::TextUnformatted(lines[i].c_str());
                }
                ImGui::PopStyleColor();
            }
            ImGui::End();
        };
        float const leftWidth=widthOf(panels.left);
        float const rightWidth=widthOf(panels.right);
        drawPanel("F3 Left##praxis_debug_java",panels.left,margin,margin);
        float const rightX=sideBySide?std::max(margin,size.x-margin-rightWidth):margin;
        float const rightY=sideBySide?margin:margin+heightOf(panels.left)+4.f*scale;
        drawPanel("F3 Right##praxis_debug_java",panels.right,rightX,rightY);
    } catch(...) {}
    ImGui::SetCurrentContext(previous);
}
} // namespace praxis::companion
