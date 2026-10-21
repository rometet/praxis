#include "render/InformationHud.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include "gui/JapaneseText.h"
#include "core/UiLayoutPolicy.h"
#include <Windows.h>
#include <imgui.h>
#include <cstdio>
#include <cfloat>
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
} // namespace praxis::companion
