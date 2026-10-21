#include "runtime/ConfigStorage.h"
#include "support/ConfigStorageApi.h"
#include "runtime/ProfileDocument.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
using namespace praxis::companion;
int main() {
    unsigned checks{},failures{};
    auto check=[&](bool v,char const* label){++checks;if(!v){++failures;std::cerr<<label<<'\n';}};
    auto root=std::filesystem::temp_directory_path()/("PraxisProfileOwnership-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root/"profiles");
    auto write=[](auto const& path,ConfigJson const& value){std::ofstream(path)<<value.dump(2);};
    auto read=[](auto const& path){std::ifstream file(path);return ConfigJson::parse(file,nullptr,true,true);};
    RuntimeConfig defaults;
    auto baseline=ll::reflection::serialize<ConfigJson>(defaults).value();
    for(unsigned sourceWidth:{8u,15u})for(unsigned width:{9u,10u,15u,32u}) {
        auto source=baseline;
        source["keybinds"]=std::vector<int>(sourceWidth,65);
        source["keybindModifiers"]=std::vector<unsigned>(sourceWidth,2);
        source["favoriteModules"]=0;
        for(auto const& owner:featureProfileFields)source.erase(owner.field);
        source["sourceOpaque"]={{"null",nullptr}};
        auto target=baseline;
        target["keybinds"]=std::vector<int>(width,80);
        target["keybindModifiers"]=std::vector<unsigned>(width,4);
        target["favoriteModules"]=featureRegistryMask(width);
        for(auto name:{"timeChanger","weatherChanger","toolProtection","weaponSwitch","handRestock","inventorySort","inventoryTransfer"})target[name]=true;
        target["hitboxOptions"]["enabled"]=true;
        target["weatherOptions"]={{"mode",2},{"intensity",23.f}};
        target["toolProtectionOptions"]["remaining"]=77;
        target["weaponSwitchOptions"]["priority"]=2;
        target["handRestockOptions"]["threshold"]=13;
        target["inventorySortOptions"]["preserveHotbar"]=false;
        target["inventoryTransferOptions"]["reverseWheel"]=true;
        target["toolProtectionOptions"]["opaqueNull"]=nullptr;
        target["targetOpaque"]={{"null",nullptr}};
        auto sourceFile=root/"profiles/source.json",targetFile=root/"profiles/target.json";
        write(sourceFile,source);write(targetFile,target);
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(sourceFile,1)&&stopConfigStorage(),"load original sparse source through production worker");
        auto loaded=takeLoadedProfile();check(bool(loaded),"owned source completion");if(!loaded)continue;
        auto current=loaded->config;
        check(startConfigStorage(root/"config.json")&&queueConfigSave(current,targetFile,loaded->sourceDocument)&&stopConfigStorage(),"untouched sparse Save As drains");
        auto saved=read(targetFile);
        for(auto const& owner:featureProfileFields)check(saved[owner.field]==target[owner.field],"all thirteen absent typed fields inherit destination, even inside declared source extent");
        for(unsigned i=0;i<std::max({featureCount,width,sourceWidth});++i)
            check(saved["keybinds"][i]==(i<sourceWidth?65:i<width?80:0)&&saved["keybindModifiers"][i]==(i<sourceWidth?2:i<width?4:8),"source prefix and independent target tail remain coupled");
        check(saved["sourceOpaque"]["null"].is_null()&&saved["targetOpaque"]["null"].is_null(),"opaque null owners retained");
        // An explicit edit followed by a return to the loaded baseline must
        // still win over an inherited destination binding/enable/options.
        ProfileEditHistory edits;
        auto old=ll::reflection::serialize<ConfigJson>(current).value();
        auto changed=current;
        changed.weatherChanger=true;changed.toolProtection=true;changed.toolProtectionOptions.remaining=33;
        changed.weatherOptions={1,17.f};
        changed.keybinds[10]='G';changed.keybindModifiers[10]=1;changed.favoriteModules|=featureRegistryBit(10);
        recordProfileEdits(edits,old,ll::reflection::serialize<ConfigJson>(changed).value());
        recordProfileEdits(edits,ll::reflection::serialize<ConfigJson>(changed).value(),old);
        write(targetFile,target);
        check(startConfigStorage(root/"config.json")&&queueConfigSave(current,targetFile,loaded->sourceDocument,edits),"sticky explicit edit snapshot admitted");
        edits={}; // The worker owns the previously admitted history value.
        check(stopConfigStorage(),"history job drains after caller resets carrier");
        saved=read(targetFile);
        check(saved["weatherChanger"]==false&&saved["toolProtection"]==false&&saved["toolProtectionOptions"]["remaining"]==1,"ON then OFF and option then reset are explicit baseline edits");
        check(saved["weatherOptions"]==baseline["weatherOptions"],"explicit mode edit then reset owns the final Clear/default intensity instead of inheriting Snow");
        check(saved["keybinds"][10]==current.keybinds[10]&&saved["keybindModifiers"][10]==current.keybindModifiers[10]
            &&!(saved["favoriteModules"].get<std::uint32_t>()&featureRegistryBit(10)),"bind then clear/modifier reset/unpin apply final baseline values");
        check(saved["weaponSwitchOptions"]==target["weaponSwitchOptions"]&&saved["inventoryTransferOptions"]==target["inventoryTransferOptions"],"unrelated Inventory option owners survive edited save");
        // Explicit Clear of an already-unbound newly appended slot claims it.
        if(sourceWidth==8){ProfileEditHistory cleared;cleared.keys=cleared.modifiers=featureRegistryBit(11);write(targetFile,target);
            check(startConfigStorage(root/"config.json")&&queueConfigSave(current,targetFile,loaded->sourceDocument,cleared)&&stopConfigStorage(),"explicit no-op Clear is saved");
            saved=read(targetFile);check(saved["keybinds"][11]==0&&saved["keybindModifiers"][11]==8,"Clear of baseline unbound replaces inherited target binding");}
    }
    for(unsigned version:{1u,2u}) {
        auto file=root/"profiles/commented.json";
        std::ofstream out(file);out<<"/* accepted profile comment */\n{\"version\":"<<version<<",\"brightness\":"<<(version==1?99:12)<<",\"zoomMultiplier\":0.5,\"keybinds\":[65,66,67,68,69,70,71]}";out.close();
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(file,2)&&stopConfigStorage(),"commented legacy profile executes real worker");
        auto loaded=takeLoadedProfile();check(loaded&&(version==1?(loaded->config.brightness==25&&loaded->config.zoomOptions.targetFov==35):loaded->config.brightness==12),"comments preserve accepted v1 migration and valid v2 decode");
    }
    auto source=baseline;source["keybinds"]=std::vector<int>(8,65);source["keybindModifiers"]=std::vector<unsigned>(8,2);source["favoriteModules"]=featureRegistryBit(31);
    auto sourceFile=root/"profiles/high-source.json",targetFile=root/"profiles/new-high.json";write(sourceFile,source);
    check(startConfigStorage(root/"config.json")&&queueProfileLoad(sourceFile,3)&&stopConfigStorage(),"accepted high opaque favorite source loads");
    auto loaded=takeLoadedProfile();check(bool(loaded),"high favorite completion");
    if(loaded){check(startConfigStorage(root/"config.json")&&queueConfigSave(loaded->config,targetFile,loaded->sourceDocument)&&stopConfigStorage(),"new target accepts opaque favorite metadata");check(read(targetFile)["favoriteModules"]==featureRegistryBit(31),"favorite bit31 survives new profile without fabricated keys");}
    std::filesystem::remove_all(root);
    std::cout<<checks<<" production profile ownership checks, "<<failures<<" failures\n";
    return failures?1:0;
}
