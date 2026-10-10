#include "runtime/ConfigStorage.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
using namespace praxis::companion;
int main() {
    unsigned checks{},failures{};
    auto check=[&](bool ok,char const* name){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}};
    auto wait=[&](auto predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return true;};
    auto root=std::filesystem::temp_directory_path()/("PraxisConfigStorageTests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    if(!std::filesystem::create_directory(root))return 2;
    RuntimeConfig c; c.fullbright=true; c.timeChanger=true; c.keybinds[7]='T';
    c.toolProtection=true;c.toolProtectionOptions={42,17,true,false};
    c.weaponSwitch=true;c.weaponSwitchOptions={2,true,false,false,false};
    c.handRestock=true;c.handRestockOptions={13,800,true,false};
    c.offhandSwap=true;c.keybinds[26]='G';c.keybindModifiers[26]=2;c.handRestockOptions.offhand=true;
    c.handRestockOptions.excludedItems[0]="minecraft:golden_carrot";c.handRestockOptions.excludedInventory[35]=true;
    c.inventorySort=true;c.inventorySortOptions={false,false,82};
    c.inventorySortOptions.excludedInventory[0]=c.inventorySortOptions.excludedInventory[35]=true;
    c.inventorySortOptions.excludedStorage[0]=c.inventorySortOptions.excludedStorage[53]=true;
    c.inventoryTransfer=true;c.inventoryTransferOptions={false,true,false,true,false,false,true};
    c.weatherChanger=true;c.hitboxOptions={true,true,false,120,128,0x12345678};
    c.weatherOptions={2,21.f};
    c.favoriteModules=0x7fff;c.keybinds[8]='W';c.keybinds[9]='H';c.keybinds[10]='G';
    c.keybindModifiers[8]=1;c.keybindModifiers[9]=2;c.keybindModifiers[10]=4;
    c.zoomModifiers=3;c.mapToggleModifiers=5;c.informationOptions.bundlePreview=true;
    check(startConfigStorage(root/"config.json"),"start owned worker");
    check(queueConfigSave(c,root/"config.json"),"accept config save");
    check(stopConfigStorage(),"shutdown drains admitted save and joins");
    check(std::filesystem::file_size(root/"config.json")>0,"atomic destination exists after join");
    check(!queueConfigSave(c,root/"config.json"),"stopped worker rejects admission");
    check(startConfigStorage(root/"config.json"),"re-enable worker");
    std::ofstream(root/"blocked")<<"owned test blocker";
    check(queueConfigSave(c,root/"blocked/profile.json"),"failed I/O is initially admitted");
    check(wait([&]{return configStorageStatus().find("Unsaved")!=std::string::npos;}),"failure remains owned and visible");
    check(!stopConfigStorage(),"unresolved save prevents successful retirement");
    std::filesystem::remove(root/"blocked");
    check(startConfigStorage(root/"config.json"),"restart retains failed owned job");
    check(stopConfigStorage(),"last retry recovers after destination becomes writable");
    check(std::filesystem::exists(root/"blocked/profile.json"),"retained save restored");
    check(startConfigStorage(root/"config.json"),"profile operations worker start");
    check(queueConfigSave(c,root/"profiles/A.json"),"accept profile save");
    check(stopConfigStorage(),"profile save joined");
    check(startConfigStorage(root/"config.json"),"profile read worker start");
    check(queueProfileLoad(root/"profiles/A.json",42),"profile read admitted");
    check(stopConfigStorage(),"profile read joined");
    auto loaded=takeLoadedProfile();
    check(loaded && loaded->requestRevision==42 && loaded->config.fullbright
        && loaded->config.timeChanger && loaded->config.keybinds[7]=='T'
        && loaded->config.weatherChanger && loaded->config.keybinds[8]=='W',
        "owned completion carries request identity, TimeChanger and binding");
    check(loaded && loaded->config.weatherOptions==c.weatherOptions,"Weather mode/intensity round-trip through the actual queued profile codec");
    check(cachedProfileNames()==std::vector<std::string>{"A"},"profile index cached away from renderer");
    check(startConfigStorage(root/"config.json"),"delete worker start");
    check(queueProfileDelete(root/"profiles/A.json"),"delete admitted");
    check(stopConfigStorage() && !std::filesystem::exists(root/"profiles/A.json"),"delete drains and refreshes");
    std::ofstream(root/"profiles/Legacy.json")
        << R"({"version":2,"fullbright":true,"keybinds":[65,66,67,68,69,70,71]})";
    check(startConfigStorage(root/"config.json"),"legacy profile worker start");
    check(queueProfileLoad(root/"profiles/Legacy.json",43),"legacy profile read admitted");
    check(stopConfigStorage(),"legacy profile read joined");
    loaded=takeLoadedProfile();
    check(loaded && loaded->config.fullbright && !loaded->config.timeChanger
        && loaded->config.keybinds[0]==65 && loaded->config.keybinds[6]==71
        && loaded->config.keybinds[7]==0, "production worker migrates seven bindings and defaults new feature OFF");
    check(loaded && !loaded->config.weatherChanger && loaded->config.weatherOptions==WeatherOptions{},
        "legacy profile preserves default OFF/Clear/intensity without a new setting");
    for(unsigned count:{7u,8u,9u}) {
        auto key=std::string("Slots")+std::to_string(count);
        nlohmann::ordered_json fixture{{"version",2},{"weatherChanger",count==9},{"favoriteModules",(1u<<count)-1}};
        fixture["keybinds"]=nlohmann::ordered_json::array();fixture["keybindModifiers"]=nlohmann::ordered_json::array();
        for(unsigned i=0;i<count;++i){fixture["keybinds"].push_back(65+i);fixture["keybindModifiers"].push_back(i%8);}
        auto file=root/"profiles"/(key+".json");{std::ofstream out(file);out<<fixture.dump();}
        check(startConfigStorage(root/"config.json"),"legacy registry profile worker start");
        check(queueProfileLoad(file,100+count),"legacy registry profile load queued");
        check(stopConfigStorage(),"legacy registry profile load drained");loaded=takeLoadedProfile();
        check(loaded&&loaded->requestRevision==100+count&&loaded->config.favoriteModules==((1u<<count)-1)
            &&loaded->config.weatherChanger==(count==9)&&loaded->config.keybinds[8]==(count==9?73:0)
            &&loaded->config.keybinds[9]==0&&loaded->config.keybindModifiers[9]==8,"actual worker preserves Weather8 and appends unbound Hitbox9");
        if(loaded){
            auto expected=loaded->config;
            check(startConfigStorage(root/"config.json"),"upgraded profile save worker start");
            check(queueConfigSave(expected,file),"upgraded profile save queued");check(stopConfigStorage(),"upgraded save drained");
            check(startConfigStorage(root/"config.json"),"upgraded profile reread worker start");
            check(queueProfileLoad(file,200+count),"upgraded profile reread queued");check(stopConfigStorage(),"upgraded profile reread drained");
            loaded=takeLoadedProfile();check(loaded&&loaded->config.keybinds==expected.keybinds&&loaded->config.keybindModifiers==expected.keybindModifiers
                &&loaded->config.favoriteModules==expected.favoriteModules&&loaded->config.weatherChanger==expected.weatherChanger,"saved profile keeps exact keys/modifiers/favorites/Weather request");
        }
    }
    auto futureFile=root/"profiles/Future15.json";
    nlohmann::ordered_json future{{"version",2},{"weatherChanger",true},{"favoriteModules",0x7fffu},
        {"toolProtection",true},{"toolProtectionOptions",{{"threshold",23},{"context","gameplay"}}},
        {"weaponSwitchOptions",{{"preferSword",true}}},{"handRestock",true},
        {"inventorySortOptions",{{"order","name"}}},{"inventoryTransferOptions",{{"mode","matching"}}},
        {"futureNull",nullptr},{"informationOptions",{{"playerHud",{{"futureOwnerOption",42}}}}}};
    future["keybinds"]=nlohmann::ordered_json::array();future["keybindModifiers"]=nlohmann::ordered_json::array();
    for(unsigned i=0;i<15;++i){future["keybinds"].push_back(65+i);future["keybindModifiers"].push_back(i%8);}
    {std::ofstream output(futureFile);output<<future.dump();}
    check(startConfigStorage(root/"config.json")&&queueProfileLoad(futureFile,300)&&stopConfigStorage(),"future fifteen-slot actual worker load drains");
    loaded=takeLoadedProfile();
    check(loaded&&loaded->requestRevision==300&&loaded->config.keybinds.size()==featureCount&&loaded->config.keybindModifiers.size()==featureCount
        &&loaded->config.favoriteModules==0x7fffu&&loaded->config.weatherChanger,"future registry and Weather request retained by standalone worker");
    if(loaded) {
        auto source=loaded->sourceDocument;auto next=loaded->config;next.keybinds[9]='H';next.brightness=11;
        auto checkSaved=[&](std::filesystem::path const& file){
            std::ifstream input(file);auto actual=nlohmann::ordered_json::parse(input);
            check(actual["keybinds"].size()==featureCount&&actual["keybinds"][8]==future["keybinds"][8]&&actual["keybinds"][9]=='H'
                &&actual["brightness"]==11&&actual["favoriteModules"]==0x7fff,"known edit applied without truncating future registry");
            for(unsigned i=10;i<15;++i)check(actual["keybinds"][i]==future["keybinds"][i]
                &&actual["keybindModifiers"][i]==future["keybindModifiers"][i],"inventory owner slots ten through fourteen preserve meaning");
            for(auto field:{"toolProtection","toolProtectionOptions","weaponSwitchOptions","handRestock","inventorySortOptions","inventoryTransferOptions","futureNull"})
                {if(future[field].is_object()){for(auto const& item:future[field].items())check(actual[field][item.key()]==item.value(),"unknown nested Inventory field retained beside typed defaults");}else check(actual[field]==future[field],"unknown root/null and known Inventory enable retained");}
            check(actual["informationOptions"]["playerHud"]["futureOwnerOption"]==42,"unknown nested option survives real codec defaults");
        };
        check(startConfigStorage(root/"config.json")&&queueConfigSave(next,futureFile,source)&&stopConfigStorage(),"future profile in-place save drains");checkSaved(futureFile);
        auto newFile=root/"profiles/Future15Copy.json";
        check(startConfigStorage(root/"config.json")&&queueConfigSave(next,newFile,source),"Save As admits owned source document");
        source.clear();check(stopConfigStorage(),"Save As owns source after caller mutation");checkSaved(newFile);
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(newFile,301)&&stopConfigStorage(),"future Save As reload drains");
        auto reread=takeLoadedProfile();check(reread&&reread->config.keybinds==next.keybinds&&reread->config.keybindModifiers==next.keybindModifiers
            &&reread->config.favoriteModules==next.favoriteModules&&nlohmann::ordered_json::parse(reread->sourceDocument)["toolProtectionOptions"]["threshold"]==future["toolProtectionOptions"]["threshold"],"future Save As reread retains complete owned profile semantics");
    }

    // Exercise Save As across two independent documents through the actual
    // worker; the SDK target uses the pinned real codec for every load/save.
    unsigned matrixCases{};
    for(auto const [sourceCount,targetCount]:{std::pair{8u,9u},std::pair{8u,10u},
        std::pair{8u,15u},std::pair{8u,32u},std::pair{9u,15u},
        std::pair{10u,32u},std::pair{15u,32u}})for(bool sourceHasModifiers:{false,true}) {
        ++matrixCases;
        auto suffix=std::to_string(sourceCount)+"To"+std::to_string(targetCount)
            +(sourceHasModifiers?"Exact":"Legacy");
        auto sourceFile=root/"profiles"/("Source"+suffix+".json");
        auto targetFile=root/"profiles"/("Target"+suffix+".json");
        auto sourceFavorite=0x55555555u&featureRegistryMask(sourceCount);
        // A padded/partial source may carry a favorite beyond its binding
        // extent. It cannot take ownership of the destination Weather tail.
        if(sourceCount==8)sourceFavorite|=featureRegistryBit(8);
        auto targetFavorite=0xaaaaaaaau&featureRegistryMask(targetCount);
        nlohmann::ordered_json source{{"version",2},{"brightness",13},{"fullbright",true},
            {"favoriteModules",sourceFavorite},{"sourceOnly",{{"value",17}}},{"sourceNull",nullptr},
            {"sharedFuture",{{"sourceOnly",1},{"shared","source"}}},
            {"informationOptions",{{"playerHud",{{"sourceFuture",23}}}}}};
        nlohmann::ordered_json target{{"version",2},{"brightness",19},{"fullbright",false},
            {"favoriteModules",targetFavorite},{"weatherChanger",true},
            {"hitboxOptions",{{"enabled",true},{"targetFuture",37}}},
            {"toolProtectionOptions",{{"threshold",23},{"futureNested",{{"value",41}}}}},
            {"targetOnly",{{"value",29}}},{"targetNull",nullptr},
            {"sharedFuture",{{"targetOnly",2},{"shared","target"}}},
            {"informationOptions",{{"playerHud",{{"targetFuture",31}}}}}};
        if(sourceCount>=9)source["weatherChanger"]=false;
        if(sourceCount>=10)source["hitboxOptions"]={{"enabled",false},{"sourceFuture",43}};
        source["keybinds"]=nlohmann::ordered_json::array();
        if(sourceHasModifiers)source["keybindModifiers"]=nlohmann::ordered_json::array();
        target["keybinds"]=nlohmann::ordered_json::array();target["keybindModifiers"]=nlohmann::ordered_json::array();
        for(unsigned i=0;i<sourceCount;++i){source["keybinds"].push_back(65+i);
            if(sourceHasModifiers)source["keybindModifiers"].push_back((i+2)%8);}
        for(unsigned i=0;i<targetCount;++i){target["keybinds"].push_back(128+i);target["keybindModifiers"].push_back((i+4)%9);}
        {std::ofstream out(sourceFile);out<<source.dump();}
        {std::ofstream out(targetFile);out<<target.dump();}
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(sourceFile,400+matrixCases)
            &&stopConfigStorage(),"cross-profile original load drains");
        auto original=takeLoadedProfile();check(original&&original->requestRevision==400+matrixCases,
            "cross-profile original load has owned completion");
        if(!original)continue;
        auto carrier=nlohmann::ordered_json::parse(original->sourceDocument);
        check(carrier["keybinds"].size()==sourceCount
            &&carrier.contains("keybindModifiers")==sourceHasModifiers,
            "load completion retains original source extent before runtime padding");
        auto current=original->config;current.brightness=11;
        check(startConfigStorage(root/"config.json")
            &&queueConfigSave(current,targetFile,original->sourceDocument)&&stopConfigStorage(),
            "cross-profile Save As through production atomic worker drains");
        std::ifstream savedInput(targetFile);auto saved=nlohmann::ordered_json::parse(savedInput);savedInput.close();
        auto expectedCount=std::max(featureCount,targetCount);
        auto expectedFavorite=(targetFavorite&~featureRegistryMask(sourceCount))|(sourceFavorite&featureRegistryMask(sourceCount));
        check(saved["keybinds"].size()==expectedCount&&saved["keybindModifiers"].size()==expectedCount,
            "cross-profile save preserves independent destination registry extent");
        for(unsigned i=0;i<expectedCount;++i){
            auto expectedKey=i<sourceCount?65+i:(i<targetCount?128+i:0);
            auto expectedModifier=i<sourceCount?(sourceHasModifiers?(i+2)%8:8):(i<targetCount?(i+4)%9:8);
            check(saved["keybinds"][i]==expectedKey&&saved["keybindModifiers"][i]==expectedModifier,
                "source prefix and destination tail keys/modifier numeric bits remain exact");
        }
        check(saved["favoriteModules"]==expectedFavorite,"cross-profile save preserves unowned favorite tail including bit31");
        check(saved["brightness"]==11&&saved["fullbright"]==true,
            "recognized source settings and user edits persist");
        check(saved["weatherChanger"]==(sourceCount<9),"absent Weather owner inherits destination while explicit source wins");
        check(saved["hitboxOptions"]["enabled"]==(sourceCount<10)
            &&saved["hitboxOptions"]["targetFuture"]==37,
            "appended absent module and unknown nested fields retain destination owner values");
        check(saved["sourceOnly"]==source["sourceOnly"]&&saved["targetOnly"]==target["targetOnly"]
            &&saved["sourceNull"].is_null()&&saved["targetNull"].is_null(),
            "independent source and destination unknown objects/nulls survive");
        check(saved["sharedFuture"]["sourceOnly"]==1&&saved["sharedFuture"]["targetOnly"]==2
            &&saved["sharedFuture"]["shared"]=="source"
            &&saved["toolProtectionOptions"]["threshold"]==target["toolProtectionOptions"]["threshold"]&&saved["toolProtectionOptions"]["futureNested"]==target["toolProtectionOptions"]["futureNested"],
            "unknown recursive merge retains destination inventory fields and source precedence");
        check(saved["informationOptions"]["playerHud"]["sourceFuture"]==23
            &&saved["informationOptions"]["playerHud"]["targetFuture"]==31,
            "typed real codec roundtrip preserves both nested unknown owners");
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(targetFile,500+matrixCases)
            &&stopConfigStorage(),"cross-profile saved document reload drains");
        auto reread=takeLoadedProfile();check(reread&&reread->config.keybinds.size()==expectedCount
            &&reread->config.favoriteModules==expectedFavorite&&reread->config.weatherChanger==(sourceCount<9),
            "cross-profile final document passes real runtime validation");
        if(sourceCount==8){
            // A newly edited compiled slot must win over the destination while
            // untouched Weather8 / inventory10..31 remain destination-owned.
            current.keybinds[9]='H';current.keybindModifiers[9]=2;
            current.favoriteModules|=featureRegistryBit(9);
            current.hitboxOptions.enabled=true;
            check(startConfigStorage(root/"config.json")
                &&queueConfigSave(current,targetFile,original->sourceDocument)&&stopConfigStorage(),
                "edited appended Hitbox slot saves against original eight-slot carrier");
            std::ifstream editedInput(targetFile);auto edited=nlohmann::ordered_json::parse(editedInput);editedInput.close();
            check(edited["keybinds"][9]=='H'&&edited["keybindModifiers"][9]==2
                &&edited["favoriteModules"]==(expectedFavorite|featureRegistryBit(9)),
                "new Hitbox binding/favorite edit overrides only owned slot without swapping Shift2");
            for(unsigned i=8;i<targetCount;++i)if(i!=9)check(edited["keybinds"][i]==target["keybinds"][i]
                &&edited["keybindModifiers"][i]==target["keybindModifiers"][i],
                "editing appended Hitbox never erases Weather/inventory/future tail");
            check(edited["weatherChanger"]==true&&edited["hitboxOptions"]["enabled"]==true
                &&edited["hitboxOptions"]["targetFuture"]==37,
                "edited appended module retains opaque destination options and Weather request");
        }
    }
    check(matrixCases==14,"all seven cross-profile width pairs with exact and legacy modifiers tested");
    // A profile may omit bindings entirely but explicitly own known favorites.
    auto noBindingsFile=root/"profiles/NoBindings.json";
    auto noBindingsTarget=root/"profiles/NoBindingsTarget.json";
    {std::ofstream out(noBindingsFile);out<<R"({"version":2,"favoriteModules":512})";}
    nlohmann::ordered_json targetWithoutSourceKeys{{"version",2},{"favoriteModules",0x7fffu}};
    targetWithoutSourceKeys["keybinds"]=std::vector<int>(15,81);
    targetWithoutSourceKeys["keybindModifiers"]=std::vector<unsigned>(15,4);
    {std::ofstream out(noBindingsTarget);out<<targetWithoutSourceKeys.dump();}
    check(startConfigStorage(root/"config.json")&&queueProfileLoad(noBindingsFile,600)
        &&stopConfigStorage(),"profile without binding fields loads through production worker");
    auto noBindings=takeLoadedProfile();check(noBindings&&noBindings->config.favoriteModules==512,
        "binding-free source retains explicit known favorites");
    if(noBindings){
        check(startConfigStorage(root/"config.json")&&queueConfigSave(noBindings->config,noBindingsTarget,noBindings->sourceDocument)
            &&stopConfigStorage(),"binding-free source Save As drains");
        std::ifstream input(noBindingsTarget);auto actual=nlohmann::ordered_json::parse(input);
        auto expectedBindings=targetWithoutSourceKeys;
        for(auto const& [field,value]:{std::pair{"keybinds",0},std::pair{"keybindModifiers",8}})
            while(expectedBindings[field].size()<featureCount)expectedBindings[field].push_back(value);
        check(actual["keybinds"]==expectedBindings["keybinds"]
            &&actual["keybindModifiers"]==expectedBindings["keybindModifiers"],
            "absent source binding arrays do not acquire ownership of target bindings");
        check(actual["favoriteModules"]==((0x7fffu&~featureRegistryMask(featureCount))|512u),
            "explicit binding-free favorites own compiled prefix while future tail survives");
    }

    // Accepted original widths retain the legacy missing-modifier contract.
    for(unsigned count:{7u,8u,9u,10u,15u,32u})for(bool withModifiers:{false,true}) {
        nlohmann::ordered_json fixture{{"version",2},{"favoriteModules",featureRegistryBit(count-1)}};
        fixture["keybinds"]=nlohmann::ordered_json::array();
        if(withModifiers)fixture["keybindModifiers"]=nlohmann::ordered_json::array();
        for(unsigned i=0;i<count;++i){fixture["keybinds"].push_back(65+i);
            if(withModifiers)fixture["keybindModifiers"].push_back(i%8);}
        auto file=root/"profiles"/("Compatible"+std::to_string(count)+(withModifiers?"Exact":"Legacy")+".json");
        {std::ofstream out(file);out<<fixture.dump();}
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(file,700+count)
            &&stopConfigStorage(),"all compatible widths load through real worker");
        auto accepted=takeLoadedProfile();
        check(accepted&&accepted->config.keybinds.size()==std::max(featureCount,count)
            &&accepted->config.favoriteModules==featureRegistryBit(count-1),
            "compatible width and favorite bit31 retained");
        if(!accepted)continue;
        auto raw=nlohmann::ordered_json::parse(accepted->sourceDocument);
        check(raw["keybinds"].size()==count&&raw.contains("keybindModifiers")==withModifiers,
            "compatible raw source remains unpadded with modifier absence preserved");
        check(startConfigStorage(root/"config.json")&&queueConfigSave(accepted->config,file,accepted->sourceDocument)
            &&stopConfigStorage(),"compatible exact or absent-modifier source saves atomically");
        std::ifstream input(file);auto saved=nlohmann::ordered_json::parse(input);
        check(saved["favoriteModules"]==featureRegistryBit(count-1),"compatible save preserves favorite tail");
        for(unsigned i=0;i<count;++i)check(saved["keybinds"][i]==65+i
            &&saved["keybindModifiers"][i]==(withModifiers?i%8:8),
            "compatible source keys/modifiers keep numeric meaning through actual codec worker");
    }
    auto readBytes=[](std::filesystem::path const& file){std::ifstream input(file,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());};
    for(auto const [keyCount,modifierCount]:{std::pair{8u,32u},std::pair{32u,8u}}) {
        auto suffix=std::to_string(keyCount)+"Keys"+std::to_string(modifierCount)+"Modifiers";
        auto malformedFile=root/"profiles"/("Malformed"+suffix+".json");
        auto destination=root/"profiles"/("GuardDestination"+suffix+".json");
        nlohmann::ordered_json bad{{"version",2},{"favoriteModules",0xffffffffu},
            {"keybinds",std::vector<int>(keyCount,75)},{"keybindModifiers",std::vector<unsigned>(modifierCount,2)}};
        nlohmann::ordered_json good{{"version",2},{"favoriteModules",0xaaaaaaaau},
            {"keybinds",std::vector<int>(32,81)},{"keybindModifiers",std::vector<unsigned>(32,4)},
            {"targetUnknown",{{"retained",true}}},{"targetNull",nullptr}};
        auto badBytes=bad.dump(2),goodBytes=good.dump(2);
        {std::ofstream out(malformedFile,std::ios::binary);out<<badBytes;}
        {std::ofstream out(destination,std::ios::binary);out<<goodBytes;}
        // An invalid queued load must not replace the last valid completion.
        check(startConfigStorage(root/"config.json")&&queueProfileLoad(destination,801)
            &&queueProfileLoad(malformedFile,802)&&stopConfigStorage(),
            "valid load then mismatched raw load complete in order");
        auto retained=takeLoadedProfile();
        check(retained&&retained->requestRevision==801&&retained->config.keybinds==std::vector<int>(32,81)
            &&retained->config.keybindModifiers==std::vector<unsigned>(32,4)
            &&retained->config.favoriteModules==0xaaaaaaaau,
            "mismatched load preserves existing completion/state and favorite bit31");
        check(readBytes(malformedFile)==badBytes&&readBytes(destination)==goodBytes,
            "mismatched load never rewrites source or destination");
        if(!retained)continue;
        auto unchanged=retained->config;
        check(startConfigStorage(root/"config.json")&&queueConfigSave(unchanged,destination,badBytes),
            "malformed source Save As is owned by production queue");
        check(!stopConfigStorage(),"mismatched source rejected before atomic replacement and remains unsaved");
        check(readBytes(destination)==goodBytes&&unchanged.keybinds==retained->config.keybinds
            &&unchanged.keybindModifiers==retained->config.keybindModifiers,
            "source mismatch preserves existing destination bytes and caller state");
        bool noPending=true;for(auto const& entry:std::filesystem::directory_iterator(destination.parent_path()))
            if(entry.path().filename().string().starts_with(destination.filename().string()+".pending-"))noPending=false;
        check(noPending,"mismatch rejected before creating any pending output");
        check(startConfigStorage(root/"config.json")&&queueConfigSave(unchanged,destination,goodBytes)
            &&stopConfigStorage(),"valid newer save supersedes the owned malformed failure");
        std::ifstream recoveredInput(destination);auto recovered=nlohmann::ordered_json::parse(recoveredInput);recoveredInput.close();
        check(recovered["keybinds"]==good["keybinds"]&&recovered["keybindModifiers"]==good["keybindModifiers"]
            &&recovered["favoriteModules"]==good["favoriteModules"]&&recovered["targetUnknown"]==good["targetUnknown"]
            &&recovered["targetNull"].is_null(),"recovery preserves full registry/favorite/unknown destination semantics");
        // The same gate protects a malformed destination read independently.
        {std::ofstream out(destination,std::ios::binary);out<<badBytes;}
        check(startConfigStorage(root/"config.json")&&queueConfigSave(unchanged,destination,goodBytes),
            "malformed destination save is queued");
        check(!stopConfigStorage()&&readBytes(destination)==badBytes,
            "malformed destination rejected atomically without overwrite");
        {std::ofstream out(destination,std::ios::binary);out<<goodBytes;}
        check(startConfigStorage(root/"config.json")&&queueConfigSave(unchanged,destination,goodBytes)
            &&stopConfigStorage(),"restored destination allows owned save recovery and clean retirement");
    }
    std::filesystem::remove_all(root); // Exclusively created test fixture above.
    #ifdef PRAXIS_CONFIG_STORAGE_REAL_CODEC_TEST
    std::cout<<"Config storage production worker + actual SDK codec: "
#else
    std::cout<<"Config storage production worker: "
#endif
    <<checks<<" checks "<<(failures?"FAIL":"PASS")<<'\n';
    return failures?1:0;
}
