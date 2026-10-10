// Compile the actual settings/action/persistence owner. The test excludes only
// SDK-native tick capture; no runtime native objects are mocked or admitted.
#define PRAXIS_MAP_HEADLESS_TEST 1
#include "map/MapRuntime.cpp"
#include <imgui_internal.h>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include "gui/LegacyUiFont.h"
#include "../HeadlessRaster.h"
using namespace praxis::companion;
using namespace praxis::companion::map;
int main() {
    auto* context=ImGui::CreateContext();
    try {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;
        if(!configureLegacyUiFont(LegacyUiFontFamily::CorporateLogo,18))throw std::runtime_error("actual Map Japanese font unavailable");
        unsigned char* data;int width,height;
        io.Fonts->GetTexDataAsRGBA32(&data,&width,&height);
        auto fixture=std::make_shared<Snapshot>();fixture->session={"settings-fixture",1,0,false};
        fixture->playerY=64;fixture->waypointsEditable=true;fixture->persistenceStatus="Fixture values";
        std::vector<Cell> terrain;
        for(int z=-16;z<=16;++z)for(int x=-16;x<=16;++x) {
            Sample sample;sample.known=true;sample.color=0xff448866;sample.height=64;
            terrain.push_back({x,z,1,sample,tileKey(x,z,0,Layer::Surface),3,{1,1}});
        }
        fixture->cells=std::make_shared<std::vector<Cell> const>(std::move(terrain));
        fixture->terrainImages[1]=rasterTerrain(View{0,0,16},*fixture->cells,2);
        for(std::uint64_t i=1;i<=128;++i)fixture->waypoints.push_back({i,"Waypoint",double(i),64,0,0});
        publication.store(fixture);activeSession=fixture->session;stopped=false;worldOpen=true;
        optionsLoadDone.store(true);waypointsLoadDone=true;waypointsBlocked=false;actionDispatcherAvailable.store(true);
        Settings selected;selected.enabled=true;selected.actionKeys={77,187,189,67,72,78};
        options.store(std::make_shared<Settings const>(selected));
        int cases=0;
        for(auto display:{ImVec2{640,480},ImVec2{1280,720},ImVec2{2048,1152}})
            for(float textSize:{13.f,18.f,30.f})for(bool open:{false,true}) {
                worldOpen=open;io.DisplaySize=display;
                ImGui::NewFrame();ImGui::SetNextWindowPos({8,8});ImGui::SetNextWindowSize({display.x-16,display.y-16});
                ImGui::Begin("Map settings",nullptr,ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetWindowFontScale(textSize/18.f);
                int styles=ImGui::GetCurrentContext()->StyleVarStack.Size;
                int ids=ImGui::GetCurrentWindow()->IDStack.Size;
                drawMapSettings();
                if(ImGui::GetCurrentWindow()->IDStack.Size!=ids||ImGui::GetCurrentContext()->StyleVarStack.Size!=styles)
                    throw std::runtime_error("settings left an unbalanced ImGui stack");
                ImGui::End();ImGui::Render();
                if(!ImGui::GetDrawData()||ImGui::GetDrawData()->TotalVtxCount<50)
                    throw std::runtime_error("actual settings UI produced no geometry");
                if(*options.load()!=selected)throw std::runtime_error("drawing settings mutated persisted options");
                ++cases;
                if(open && textSize==18.f && (display.x==640 || display.x==1280)) {
                    auto file=std::string("praxis-ui-map-ja-")+std::to_string(int(display.x))+".ppm";
                    offline_ui::saveRender(file.c_str(),ImGui::GetDrawData(),int(display.x),int(display.y));
                }
            }
        // Legacy persisted biomeText=true must not restore the removed footer.
        // Compare actual HUD geometry; coordinates and terrain still render.
        io.DisplaySize={1280,720};worldOpen=false;
        auto hud=[&](bool biome,bool coords) {
            auto next=selected;next.biomeText=biome;next.coordinates=coords;
            options.store(std::make_shared<Settings const>(next));
            ImGui::NewFrame();drawMapHud(context);ImGui::Render();
            auto* data=ImGui::GetDrawData();std::vector<ImDrawVert> vertices;
            for(auto* list:data->CmdLists)vertices.insert(vertices.end(),list->VtxBuffer.begin(),list->VtxBuffer.end());
            return vertices;
        };
        for(bool coords:{false,true}) {
            auto without=hud(false,coords),legacy=hud(true,coords);
            if(without.empty()||without.size()!=legacy.size()||
                std::memcmp(without.data(),legacy.data(),without.size()*sizeof(ImDrawVert)))
                throw std::runtime_error("legacy biome flag changed the minimap footer");
        }
        if(hud(true,true).size()<=hud(true,false).size())
            throw std::runtime_error("coordinate footer was removed with biome text");
        options.store(std::make_shared<Settings const>(selected));
        if(mapActionBindings()!=selected.actionKeys||!dispatchMapAction(MapAction::ZoomIn)||
            options.load()->zoom!=1.25||!dispatchMapAction(MapAction::CenterOnPlayer)||
            !dispatchMapAction(MapAction::AddWaypoint)||commands.empty())
            throw std::runtime_error("native-independent action callbacks disconnected");
        if(!dispatchMapAction(MapAction::ToggleMinimap)||options.load()->minimap)
            throw std::runtime_error("new minimap action did not retain semantics");
        toggleMapEnabled();if(options.load()->enabled)
            throw std::runtime_error("legacy Map enable action did not retain independent semantics");
        setMapActionDispatcherAvailable(false);
        if(dispatchMapAction(MapAction::ZoomIn))throw std::runtime_error("actions bypassed retired input admission");
        shutdownMap();if(!stopped||publication.load())throw std::runtime_error("Map cleanup did not retire publication");
        ImGui::DestroyContext(context);
        std::cout<<"PraxisCompanionMapSettingsUiTests: "<<cases
            <<" actual Map settings frames (3 fonts x 3 viewports x world open/closed), all action APIs/cleanup PASS; no native/game claim\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"FAIL "<<e.what()<<'\n';ImGui::DestroyContext(context);return 1;
    }
}
