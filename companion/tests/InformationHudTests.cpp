#include "render/InformationHud.h"
#include "runtime/InformationRuntime.h"
#include "core/Settings.h"
#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
namespace praxis::companion {
std::shared_ptr<InformationOptions const> testOptions;
std::shared_ptr<InformationSnapshot const> testSample;
std::shared_ptr<InformationOptions const> informationOptions() noexcept {return testOptions;}
std::shared_ptr<InformationSnapshot const> informationSnapshot() noexcept {return testSample;}
}
using namespace praxis::companion;
int main() {
    auto* context=ImGui::CreateContext();
    try {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;
        ImFontConfig font;font.SizePixels=18;io.Fonts->AddFontDefault(&font);
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        runtimeReady=true;worldGeneration=7;unsigned cases=0;
        for(auto viewport:std::array<ImVec2,3>{{{2048,1152},{1920,1080},{1280,720}}})
          for(float ratio:{0.25f,0.5f,1.f})for(float scale:{0.5f,1.f,1.5f}) {
            io.DisplaySize=viewport;
            InformationOptions options;options.infoHud=options.durabilityHud=true;options.scale=scale;
            testOptions=std::make_shared<InformationOptions const>(options);
            InformationSnapshot sample;sample.valid=true;sample.generation=7;sample.observedMs=GetTickCount64();
            sample.logicalWidth=viewport.x*ratio;sample.logicalHeight=viewport.y*ratio;
            sample.values[0]="-71.25, 64.00, 136.50";sample.values[2]="Overworld";sample.values[5]="North";sample.values[15]="-5, 8";
            testSample=std::make_shared<InformationSnapshot const>(sample);
            auto doll=paperDollBounds(ratio,ratio);
            auto layout=durabilityHudLayout(6,viewport.x,viewport.y,options);
            auto equipment=placeHud({layout.x,layout.y,layout.width,layout.height},viewport.x,viewport.y,{&doll,1});
            publishDurabilityHudBounds(equipment,7);
            ImGui::NewFrame();drawInformationHud(context);
            auto* window=ImGui::FindWindowByName("Information##praxis_information");
            auto rect=informationHudBounds();
            if(!window||std::abs(window->Pos.x-rect.x)>1||std::abs(window->Pos.y-rect.y)>1
                ||std::abs(window->Size.x-rect.width)>1||std::abs(window->Size.y-rect.height)>1)
                throw std::runtime_error("actual Info HUD differs from collision reservation");
            if(hudIntersects(rect,doll)||hudIntersects(rect,equipment))throw std::runtime_error("actual Info HUD overlaps paper doll or equipment");
            if(rect.x<0||rect.y<0||rect.x+rect.width>viewport.x+1||rect.y+rect.height>viewport.y+1)
                throw std::runtime_error("actual Info HUD outside viewport");
            ImGui::Render();++cases;
          }
        std::cout<<"Actual InformationHud ImGui layout: "<<cases<<" resolution/GUI scale/HUD scale cases PASS\n";
        ImGui::DestroyContext(context);return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';ImGui::DestroyContext(context);return 1;}
}
