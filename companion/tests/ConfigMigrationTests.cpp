#include "core/RuntimePolicy.h"
#include "runtime/ConfigMigration.h"
#include "support/SdkCodecApi.h"
#include "runtime/ConfigCodec.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
using Json=nlohmann::ordered_json;
int main(){
 unsigned checks{};auto check=[&](bool v,char const* m){++checks;if(!v)throw std::runtime_error(m);};
 try {
   check(featureCount==27 && static_cast<unsigned>(Feature::WeatherChanger)==8
   && static_cast<unsigned>(Feature::Hitbox)==9 && static_cast<unsigned>(Feature::ToolProtection)==10
   && static_cast<unsigned>(Feature::WeaponSwitch)==11 && static_cast<unsigned>(Feature::HandRestock)==12
   && static_cast<unsigned>(Feature::InventorySort)==13 && static_cast<unsigned>(Feature::InventoryTransfer)==14,"fixed slot contract");
  for(unsigned card=0;card<8;++card)check(favoriteSlotForCard(card,Feature::Zoom)==card,"legacy favorite card bits preserved");
  check(favoriteSlotForCard(8,Feature::ToolProtection)==10,"new favorite uses feature slot");
  RuntimeConfig expected;
  expected.toolProtection=true;expected.toolProtectionOptions={42,17,true,false};
  expected.weaponSwitch=true;expected.weaponSwitchOptions={2,true,false,false,false};
  expected.handRestock=true;expected.handRestockOptions={13,800,true,false};
  expected.inventorySort=true;expected.inventorySortOptions={false,false,82};
  expected.inventorySortOptions.excludedInventory[0]=expected.inventorySortOptions.excludedInventory[35]=true;
  expected.inventorySortOptions.excludedStorage[0]=expected.inventorySortOptions.excludedStorage[53]=true;
  expected.inventoryTransfer=true;expected.inventoryTransferOptions={false,true,false,true,false,false,true};
  expected.fullbright=true;expected.brightness=12;expected.guiStyle.fontFamily=2;
  expected.informationOptions.bundlePreview=true;expected.zoomModifiers=1;expected.mapToggleModifiers=4;
  expected.hitboxOptions={true,true,false,120,128,0x12345678};
  for(unsigned length:{7u,8u,9u,10u,15u,16u,24u,32u}){
   auto legacy=expected;legacy.keybinds.resize(length);legacy.keybindModifiers.resize(length,8);legacy.timeChanger=length>=8;legacy.weatherChanger=length>=9;
   legacy.favoriteModules=registryMask(length);
   for(unsigned i=0;i<length;++i){legacy.keybinds[i]='A'+i;legacy.keybindModifiers[i]=i%9;}
   auto document=ll::reflection::serialize<Json>(legacy).value();document.erase("featureSlotSchema");
   while(document["keybinds"].size()>length)document["keybinds"].erase(document["keybinds"].size()-1);
   while(document["keybindModifiers"].size()>length)document["keybindModifiers"].erase(document["keybindModifiers"].size()-1);
   check(normalizeFeatureKeybinds(document),"legacy/current layout accepted");
   check(document["keybinds"].size()==std::max(featureCount,length) && document["keybindModifiers"].size()==std::max(featureCount,length),"all arrays padded consistently");
   legacy.keybinds.resize(std::max(featureCount,length),0);legacy.keybindModifiers.resize(std::max(featureCount,length),8);
   RuntimeConfig loaded;auto merged=typedConfigDocument(document,ll::reflection::serialize<Json>(loaded).value());
   ll::reflection::deserialize(loaded,merged).value();
   check(validRuntimeConfig(loaded) && ll::reflection::serialize<Json>(loaded).value()==ll::reflection::serialize<Json>(legacy).value(),"full SDK roundtrip preserves all feature data, modifiers and favorites");
   auto settings=runtimeSettings(loaded,kAllRenderPasses,featureSlotMask);
   check(std::equal(settings.keys.begin(),settings.keys.end(),loaded.keybinds.begin()) && std::equal(settings.keyModifiers.begin(),settings.keyModifiers.end(),loaded.keybindModifiers.begin()),"published arrays preserve all slots");
   check(settings.on(Feature::WeatherChanger)==loaded.weatherChanger && settings.on(Feature::Hitbox)==loaded.hitboxOptions.enabled,"Weather dispatch enabled; Hitbox and Inventory retain distinct slots");
   check(!runtimeSettings(loaded,kAllRenderPasses,0).on(Feature::WeatherChanger),"Weather unavailable backend does not publish enable");
  }
  auto good=ll::reflection::serialize<Json>(expected).value();
  auto opaque=good;opaque["futureModule"]={{"enabled",true},{"nullable",nullptr}};
  check(validConfigDocument(typedConfigDocument(opaque,good),good),"unknown fields stay out of typed decoder");
  Json destination={{"futureModule",{{"destinationOnly",42}}}};overlayProfileFields(destination,opaque);
  check(destination["futureModule"]["nullable"].is_null() && destination["futureModule"]["destinationOnly"]==42,"nested unknown/null data retained");
  check(modifierBits(true,false,false)==1 && modifierBits(false,true,false)==2 && modifierBits(false,false,true)==4,"canonical matches baseline Schematic modifier enum");

  check(validConfigDocument(good,good),"current document shape accepted");
  auto badExclusions=good;badExclusions["inventorySortOptions"]["excludedStorage"].push_back(false);
  check(!validConfigDocument(badExclusions,good),"unknown excluded-slot tail rejected");
  badExclusions=good;badExclusions["inventorySortOptions"]["excludedInventory"][0]=1;
  check(!validConfigDocument(badExclusions,good),"excluded-slot element requires boolean");
  auto badTransfer=good;badTransfer["inventoryTransferOptions"]["wheelOne"]=1;
  check(!validConfigDocument(badTransfer,good),"gesture flag requires boolean");
  for(auto patch:{Json{{"toolProtectionOptions",{{"remaining",UINT64_C(4294967297)}}}},Json{{"handRestockOptions",{{"threshold",true}}}},Json{{"hitboxOptions",{{"color",UINT64_C(4294967296)}}}},Json{{"zoomOptions",nullptr}}}) {
   auto bad=good;bad.merge_patch(patch);check(!validConfigDocument(bad,good),"unknown field/type/32-bit overflow rejected before SDK narrowing");
  }
  auto reject=[&](Json bad){auto before=bad;check(!normalizeFeatureKeybinds(bad),"invalid contract rejected");check(bad==before,"failure leaves entire document intact");};
  for(unsigned length:{0u,6u,33u}){auto bad=good;bad["keybinds"]=std::vector<int>(length);reject(bad);bad=good;bad["keybindModifiers"]=std::vector<int>(length);reject(bad);}
   for(unsigned length:{11u,12u,13u,14u}){auto future=good;future["keybinds"]=std::vector<int>(length,65);future["keybindModifiers"]=std::vector<unsigned>(length,4);check(normalizeFeatureKeybinds(future)&&future["keybinds"].size()==featureCount&&future["keybindModifiers"][length-1]==4,"Map-compatible intermediate future extents retained");}
  for(auto value:{Json(-1),Json(256),Json(true),Json(1.5),Json("A"),Json(nullptr),Json(UINT64_MAX)}){auto bad=good;bad["keybinds"][14]=value;reject(bad);}
  for(auto value:{Json(-1),Json(9),Json(true),Json(0.5),Json(UINT64_MAX)}){auto bad=good;bad["keybindModifiers"][8]=value;reject(bad);bad=good;bad["mapToggleModifiers"]=value;reject(bad);}
  for(auto value:{Json(0),Json(2),Json(-1),Json(true),Json(nullptr)}){auto bad=good;bad["featureSlotSchema"]=value;reject(bad);}
  for(auto value:{Json(-1),Json(UINT64_MAX),Json(true)}){auto bad=good;bad["favoriteModules"]=value;reject(bad);}
  reject(Json::array());auto missing=good;missing.erase("keybinds");missing.erase("keybindModifiers");check(normalizeFeatureKeybinds(missing),"missing arrays keep defaults");
  auto bad=expected;bad.featureSlotSchema=2;check(!validRuntimeConfig(bad),"unsupported schema rejected in typed updates");
  bad=expected;bad.hitboxOptions.maxActors=257;check(!validRuntimeConfig(bad),"invalid reserved feature settings rejected");
  for(unsigned m=0;m<=8;++m){check(validModifiers(m),"all modifier values valid");check(modifiersMatch(m,m),"modifier match");}
  check(hotkeyConflict({65,8,1},{65,1,1}) && !hotkeyConflict({65,1,1},{65,2,1}),"modifier conflict policy");
  for(unsigned length=7;length<=32;++length){auto source=good;source["keybinds"]=std::vector<int>(length,65);source["keybindModifiers"]=std::vector<unsigned>(length,4);source["futureNull"]=nullptr;RuntimeConfig decoded;check(decodeRuntimeConfig(source,decoded)&&decoded.keybinds.size()==std::max(length,featureCount)&&decoded.keybindModifiers[length-1]==4,"common startup/profile decoder accepts Map-compatible extents");}
  auto v1=good;v1["version"]=1;v1["brightness"]=99;v1["zoomMultiplier"]=.5;RuntimeConfig migrated;check(decodeRuntimeConfig(v1,migrated)&&migrated.version==2&&migrated.brightness==25&&migrated.zoomOptions.targetFov==35,"shared v1 migration clamps brightness and translates FOV");
  for(auto version:{Json(0),Json(3),Json(-1),Json(UINT64_MAX),Json(nullptr)}){auto bad=good;bad["version"]=version;auto prior=migrated;check(!decodeRuntimeConfig(bad,migrated)&&ll::reflection::serialize<Json>(prior).value()==ll::reflection::serialize<Json>(migrated).value(),"invalid version rejects without changing accepted state");}
  std::cout<<"Pinned SDK slot/schema migration: "<<checks<<" checks PASS; native DLL NOT RUN\n";return 0;
 }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
