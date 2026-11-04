#include "core/RuntimePolicy.h"
#include "runtime/ConfigMigration.h"
#include "runtime/ProfileDocument.h"
#include "support/SdkCodecApi.h"
#include "runtime/ConfigCodec.h"
#include "gui/JapaneseText.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("Inspector boundary");};
    RuntimeConfig config;check(!config.informationOptions.advancedTooltips);check(!inspectorNeeded(config.informationOptions.inspector));
    check(informationView("hud_screen","hud_screen")==InformationView::Collect);
    check(informationView("toast_screen","hud_screen")==InformationView::Defer);
    check(informationView("hud_screen","inventory_screen")==InformationView::Retire);
    check(informationView("inventory_screen","inventory_screen")==InformationView::Retire);
    InspectorSnapshot s;InspectorOptions o{true,true,true,true,true};auto unknown=inspectorLinesJa(s,o);
    check(unknown.size()==5);for(auto const& line:unknown)check(line.ends_with("不明"));
    s.looking[0]="minecraft:stone @ 1, 2, 3\n{name:stone,states:{}}";s.looking[3]="minecraft:cow / @ 1, 2, 3";
    check(inspectorCopyText(s,0,true)==s.looking[0]);check(inspectorCopyText(s,3,true)==s.looking[3]);
    for(auto const& line:inspectorLinesJa(s,o))check(line.find('\n')==std::string::npos);
    check(inspectorCopyText(s,0,false).empty());check(inspectorCopyText(s,4,true).empty());check(inspectorCopyText(s,2,true).empty());
    check(boundedInspectorText(std::string(4097,'a')).empty());check(boundedInspectorText(std::string(4096,'a')).size()==4096);
    InformationSnapshot sample;sample.valid=true;sample.generation=15;sample.observedMs=100;sample.inspector=s;
    check(informationCurrent(sample,15,1100));check(!informationCurrent(sample,16,100));check(!informationCurrent(sample,15,1101));check(!informationCurrent(sample,15,99));
    config.informationOptions.inspector=o;
    // The compare inspector still captures and copies complete state, but no
    // diagnostic/SNBT rows may leak into the gameplay Information HUD.
    check(informationLinesJa(sample,config.informationOptions).empty());
    sample.values[0]="1, 2, 3";
    auto hudRows=informationLinesJa(sample,config.informationOptions);
    check(hudRows.size()==1);
    check(hudRows.front().find("1, 2, 3")!=std::string::npos);
    check(hudRows.front().find("minecraft:stone")==std::string::npos);
    check(advancedItemText("minecraft:stone",0,64,0,0)=="ID: minecraft:stone\n補助値: 0 / 個数: 64");
    check(advancedItemText("minecraft:pickaxe",0,1,5,100).ends_with("95 / 100"));
    check(advancedItemText("minecraft:pickaxe",0,1,101,100).ends_with("0 / 100"));
    check(advancedItemText(std::string(513,'x'),0,1,0,0).empty());check(advancedItemText("x",0,256,0,0).empty());
    auto json=ll::reflection::serialize<ConfigJson>(config).value();json["informationOptions"]["future"]={{"unknown",nullptr}};
    json["informationOptions"]["inspector"]["future"]=nullptr;
    json["keybinds"]=std::vector<int>(32,80);json["keybindModifiers"]=std::vector<unsigned>(32,7);json["favoriteModules"]=UINT32_MAX;
    RuntimeConfig decoded;check(decodeRuntimeConfig(json,decoded));check(decoded.informationOptions.inspector==o);
    auto typed=ll::reflection::serialize<ConfigJson>(decoded).value();overlayOwnedConfig(json,typed);
    check(json["informationOptions"]["future"]["unknown"].is_null());check(json["informationOptions"]["inspector"]["future"].is_null());
    for(unsigned i=24;i<32;++i){check(json["keybinds"][i]==80);check(json["keybindModifiers"][i]==7);}
    auto old=typed;old["informationOptions"].erase("inspector");old["informationOptions"].erase("advancedTooltips");
    check(decodeRuntimeConfig(old,decoded));check(!inspectorNeeded(decoded.informationOptions.inspector)&&!decoded.informationOptions.advancedTooltips);
    std::cout<<"Inspector/tooltip "<<checks<<" checks PASS; SDK/Minecraft sample execution NOT_RUN\n";
}
