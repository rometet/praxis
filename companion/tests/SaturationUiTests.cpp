#include "render/PlayerHud.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include "core/SaturationPolicy.h"
#include "gui/JapaneseText.h"
#include "gui/LegacyUiFont.h"
#include "runtime/Runtime.h"
#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
namespace praxis::companion {
RuntimeConfig runtimeConfigSnapshot() {return {};}
void logGuiError(char const*) noexcept {}
std::shared_ptr<InformationOptions const> testFoodOptions;
std::shared_ptr<InformationSnapshot const> testFoodSample;
std::shared_ptr<InformationOptions const> informationOptions() noexcept {return testFoodOptions;}
std::shared_ptr<InformationSnapshot const> informationSnapshot() noexcept {return testFoodSample;}
}
using namespace praxis::companion;
int main() {
    auto* context=ImGui::CreateContext();auto* sentinel=ImGui::CreateContext();
    unsigned checks{},frames{};auto check=[&](bool v,char const* m){++checks;if(!v)throw std::runtime_error(m);};
    try {
        ImGui::SetCurrentContext(context);auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;
        check(configureLegacyUiFont(LegacyUiFontFamily::CorporateLogo,18),"production embedded Japanese Food font");
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        runtimeReady=true;worldGeneration=9;
        for(auto viewport:std::array<ImVec2,3>{{{2048,1152},{1920,1080},{1280,720}}})
          for(float scale:{.5f,1.f,3.f})for(bool custom:{false,true})for(unsigned mode=0;mode<3;++mode) {
            ImGui::SetCurrentContext(context);io.DisplaySize=viewport;
            InformationOptions options;options.playerHud.foodHud=true;options.playerHud.foodScale=scale;
            if(custom)options.hudLayouts.panels[static_cast<unsigned>(HudId::Food)]={true,-12,-12,scale,8};
            testFoodOptions=std::make_shared<InformationOptions const>(options);
            InformationSnapshot sample;sample.valid=true;sample.generation=9;sample.observedMs=GetTickCount64();
            sample.logicalWidth=viewport.x*.5f;sample.logicalHeight=viewport.y*.5f;
            sample.food.hunger={20.f,FoodSource::ClientAttribute};
            ClientSaturationTracker tracker;SaturationObservation observation{true,true,true,17,0,20,5,5};
            sample.food.saturation=tracker.observe(observation,9,0,100);
            if(mode){observation.current=mode==1?0.f:13.4f;sample.food.saturation=tracker.observe(observation,9,0,200);}
            testFoodSample=std::make_shared<InformationSnapshot const>(sample);
            auto lines=foodHudLinesJa(sample.food);
            check(lines[0]=="満腹度 (端末): 20.00"&&lines[2]=="消耗度: 不明","existing hunger/exhaustion text preserved");
            check(lines[1]==(mode==0?"隠し満腹度 (端末): 不明":mode==1?"隠し満腹度 (端末): 0.00":"隠し満腹度 (端末): 13.40"),
                "renderer uses exact client label with unknown and real-zero states");
            ImGui::NewFrame();ImGui::SetCurrentContext(sentinel);drawFoodHud(context);
            check(ImGui::GetCurrentContext()==sentinel,"food renderer restores the selected GUI owner's context");
            ImGui::SetCurrentContext(context);auto* window=ImGui::FindWindowByName("Food##praxis_food");
            check(window&&window->LastFrameActive==ImGui::GetFrameCount(),"actual production Food window rendered");
            check((window->Flags&ImGuiWindowFlags_NoInputs)!=0&&io.WantCaptureKeyboard==false,
                "food display adds no input owner");
            check(window->Pos.x>=0&&window->Pos.y>=0&&window->Pos.x+window->Size.x<=viewport.x+1
                &&window->Pos.y+window->Size.y<=viewport.y+1,"legacy/custom Food layout remains in viewport at each scale");
            auto color=ImGui::GetStyle().Colors[ImGuiCol_Text];
            check(window->FontWindowScale==scale&&color.w==1,"font scale honored and temporary text color restored");
            ImGui::Render();check(ImGui::GetDrawData()&&ImGui::GetDrawData()->TotalVtxCount>0,"actual ImGui draw lists contain Food geometry");++frames;
        }
        auto hiddenFrame=[&](InformationSnapshot sample,InformationOptions options){
            testFoodSample=std::make_shared<InformationSnapshot const>(sample);
            testFoodOptions=std::make_shared<InformationOptions const>(options);
            ImGui::SetCurrentContext(context);ImGui::NewFrame();drawFoodHud(context);
            auto* window=ImGui::FindWindowByName("Food##praxis_food");
            check(!window||window->LastFrameActive!=ImGui::GetFrameCount(),"stale/exited/disabled Food snapshot never renders");
            ImGui::Render();++frames;
        };
        InformationSnapshot sample;sample.valid=true;sample.generation=9;sample.observedMs=GetTickCount64();
        sample.food.saturation={13.4f,FoodSource::ClientAttribute};InformationOptions options;options.playerHud.foodHud=true;
        auto stale=sample;stale.observedMs=sample.observedMs>2000?sample.observedMs-2000:0;hiddenFrame(stale,options);
        auto retired=sample;retired.generation=8;hiddenFrame(retired,options);
        retired=sample;retired.valid=false;hiddenFrame(retired,options);
        options.playerHud.foodHud=false;hiddenFrame(sample,options);
        std::cout<<"Production Food HUD: "<<checks<<" checks, "<<frames<<" actual ImGui frames PASS; Minecraft NOT_RUN\n";
        ImGui::DestroyContext(context);ImGui::DestroyContext(sentinel);return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';ImGui::DestroyContext(context);ImGui::DestroyContext(sentinel);return 1;}
}
