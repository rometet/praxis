#include "core/RuntimePolicy.h"
#include "runtime/ConfigMigration.h"
#include "runtime/ProfileDocument.h"
#include "support/SdkCodecApi.h"
#include "runtime/ConfigCodec.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("InventoryHUD boundary");};
    RuntimeConfig defaults;check(!defaults.inventoryHud);
    for(unsigned i=0;i<27;++i)check(inventoryHudSlot(i)==i+9);
    check(inventoryHudSlot(27)==36);check(unsigned(Feature::InventoryHUD)==23);
    for(auto size:std::array{std::pair{160.f,90.f},std::pair{1920.f,1080.f},std::pair{3440.f,1440.f}})
        for(int anchor=0;anchor<9;++anchor)for(float scale:{.5f,1.f,3.f}) {
            HudTransform t{true,0,0,scale,anchor};auto b=inventoryHudLayout({},t,size.first,size.second);
            check(b.width<=size.first&&b.height<=size.second&&b.x>=0&&b.y>=0);
        }
    auto old=ll::reflection::serialize<ConfigJson>(defaults).value();
    old.erase("inventoryHud");old.erase("inventoryHudOptions");old["informationOptions"]["hudLayouts"].erase("inventory");
    old["keybinds"]=std::vector<int>(22,0);old["keybindModifiers"]=std::vector<unsigned>(22,8);
    RuntimeConfig restored;check(decodeRuntimeConfig(old,restored));check(!restored.inventoryHud);check(restored.informationOptions.hudLayouts.panels.size()==5);
    auto source=old,target=old;target["keybinds"]=std::vector<int>(32,81);target["keybindModifiers"]=std::vector<unsigned>(32,7);
    target["inventoryHud"]=true;target["inventoryHudOptions"]={{"x",500},{"future",nullptr}};
    target["future"]={{"private",nullptr}};target["favoriteModules"]=UINT32_MAX;
    auto typed=ll::reflection::serialize<ConfigJson>(restored).value();
    auto saved=mergeRuntimeProfileDocument(typed,typed,source,target);
    check(saved["inventoryHud"]==true);check(saved["inventoryHudOptions"]["x"]==500);check(saved["inventoryHudOptions"]["future"].is_null());
    for(unsigned slot=22;slot<32;++slot){check(saved["keybinds"][slot]==81);check(saved["keybindModifiers"][slot]==7);}
    check(saved["future"]["private"].is_null());check((saved["favoriteModules"].get<unsigned>()&~featureRegistryMask(22))==(~featureRegistryMask(22)));
    auto edited=restored;edited.inventoryHud=true;edited.inventoryHudOptions.x=600;auto editedJson=ll::reflection::serialize<ConfigJson>(edited).value();
    ProfileEditHistory history;recordProfileEdits(history,typed,editedJson);
    auto updated=mergeRuntimeProfileDocument(editedJson,typed,source,target,featureProfileFields,history);
    check(updated["inventoryHud"]==true&&updated["inventoryHudOptions"]["x"]==500);check(updated["inventoryHudOptions"]["future"].is_null());
    auto s=runtimeSettings(edited,0);check(!s.on(Feature::InventoryHUD));
    check(!runtimeSettings(edited,kAllRenderPasses,UINT32_MAX).on(Feature::InventoryHUD));
    for(unsigned i:{15u,16u,17u,18u,19u,22u,23u,25u,27u,28u,29u,30u,31u})check(!(ownedFeatureSlotMask&featureRegistryBit(i)));
    std::cout<<"InventoryHUD "<<checks<<" checks PASS\n";
}
