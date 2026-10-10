#include "runtime/ConfigStorage.h"
#if defined(PRAXIS_CONFIG_STORAGE_REAL_CODEC_TEST)
#include "support/RealConfigStorageApi.h"
#else
#include "support/ConfigStorageApi.h"
#endif
#include "runtime/ConfigCodec.h"
#include "runtime/ProfileDocument.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <limits>

using namespace praxis::companion;

namespace {
unsigned checks{},failures{};
void check(bool value,char const* message) {
    ++checks;
    if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}
}
ConfigJson encode(RuntimeConfig const& config) {return ll::reflection::serialize<ConfigJson>(config).value();}
void write(std::filesystem::path const& path,ConfigJson const& document) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<document.dump(2);out.close();
    if(!out)throw std::runtime_error("Cannot write isolated fixture");
}
ConfigJson read(std::filesystem::path const& path) {
    std::ifstream input(path);return ConfigJson::parse(input,nullptr,true,true);
}
std::string bytes(std::filesystem::path const& path) {
    std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});
}
ConfigJson document(unsigned count,int first,unsigned modifierOffset) {
    auto result=encode(RuntimeConfig{});
    result["keybinds"]=ConfigJson::array();result["keybindModifiers"]=ConfigJson::array();
    for(unsigned i=0;i<count;++i){result["keybinds"].push_back(first+int(i));result["keybindModifiers"].push_back((i+modifierOffset)%9);}
    return result;
}
constexpr std::uint32_t cameraMask=featureRegistryBit(20)|featureRegistryBit(21);
constexpr std::uint32_t cameraFieldMask=featureRegistryBit(13)|featureRegistryBit(14)|featureRegistryBit(15)|featureRegistryBit(16);
constexpr char const* cameraFields[]{"freelook","freelookOptions","freeCamera","freeCameraOptions"};
void removeCamera(ConfigJson& source) {for(auto field:cameraFields)source.erase(field);}
void setTargetCamera(ConfigJson& target) {
    target["freelook"]=target["freeCamera"]=true;
    for(auto field:{"freelookOptions","freeCameraOptions"}) {
        target[field]["hold"]=false;target[field]["degreesPerPixel"]=.3f;
        target[field]["wheelDistance"]=2.f;target[field]["flightSpeed"]=8.f;
        target[field]["foreignNull"]=nullptr;
    }
}
bool saveWorker(std::filesystem::path const& path,RuntimeConfig const& config,std::string source,ProfileEditHistory edits={}) {
    bool const started=startConfigStorage(std::filesystem::absolute(path));
    bool const admitted=started&&queueConfigSave(config,path,std::move(source),edits);
    bool const stopped=started&&stopConfigStorage();
    return started&&admitted&&stopped;
}
std::optional<LoadedProfile> loadWorker(std::filesystem::path const& path,std::uint64_t ticket) {
    if(!startConfigStorage(std::filesystem::absolute(path)))return {};
    bool const admitted=queueProfileLoad(path,ticket);
    bool const stopped=stopConfigStorage();
    check(admitted&&stopped,"actual SDK worker load admitted and drained");
    auto result=takeLoadedProfile();
    check(result&&result->requestRevision==ticket,"actual SDK worker publishes exact completion ticket");
    return result;
}
}

int main(int argc,char** argv) {
    try {
        auto root=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::temp_directory_path()
            /("PraxisCameraIntegrationConfig-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root/"profiles");
        std::cout<<"Isolated fixture: "<<root.string()<<'\n';
        check(featureCount==27&&static_cast<unsigned>(Feature::Freelook)==20&&static_cast<unsigned>(Feature::FreeCamera)==21,"camera sparse slot ABI20/21 retained with appended action26");
        check(ownedFeatureSlotMask==0x05307fffu&&featureRegistryMask(featureCount)==0x7ffffffu,"sparse ownership preserves reserved22/25 and32-bit tails");
        check(featureProfileFields.size()>=22&&featureProfileFields.size()<=32,"all typed owners fit field history");
        for(unsigned i=0;i<4;++i)check(std::string_view(featureProfileFields[13+i].field)==cameraFields[i]
            &&featureProfileFields[13+i].slot==20+i/2,"camera descriptors append without shifting existing history");
        check(modifierBits(true,false,false)==1&&modifierBits(false,true,false)==2&&modifierBits(false,false,true)==4&&anyModifiers==8,"canonical modifier contract remains unchanged");
        for(unsigned i=0;i<8;++i)check(favoriteSlotForCard(i,Feature::Freelook)==i,"legacy first eight favorite meanings preserved");
        check(favoriteSlotForCard(15,Feature::Freelook)==20&&favoriteSlotForCard(16,Feature::FreeCamera)==21,"camera favorites use feature slots rather than new card positions");

        for(unsigned width=7;width<=32;++width) {
            auto raw=document(width,65,2);removeCamera(raw);raw["favoriteModules"]=featureRegistryBit(width-1);raw["foreignOwner"]={{"null",nullptr}};
            auto original=raw;RuntimeConfig decoded;
            check(decodeRuntimeConfig(raw,decoded),"every legacy/future registry width decodes through actual SDK");
            check(raw==original&&decoded.keybinds.size()==std::max(featureCount,width)&&decoded.keybindModifiers.size()==decoded.keybinds.size(),"decoder preserves original extent and pads typed copy only");
            check(!decoded.freelook&&!decoded.freeCamera&&decoded.freelookOptions.hold&&decoded.freeCameraOptions.hold,"old profiles default both camera functions off");
            for(unsigned i=0;i<width;++i)check(decoded.keybinds[i]==65+int(i)&&decoded.keybindModifiers[i]==(i+2)%9,"decode retains declared keys/modifiers including foreign gaps/tails");
            check(decoded.favoriteModules==featureRegistryBit(width-1),"decode preserves favorite metadata through bit31");
        }

        auto good=encode(RuntimeConfig{});
        for(bool upper:{false,true}) {
            auto boundary=good;
            for(auto field:{"freelookOptions","freeCameraOptions"}) {
                boundary[field]["degreesPerPixel"]=upper?1.f:.01f;
                boundary[field]["wheelDistance"]=upper?4.f:.1f;
                boundary[field]["flightSpeed"]=upper?16.f:.25f;
            }
            RuntimeConfig accepted;
            check(decodeRuntimeConfig(boundary,accepted),"both camera range endpoints accepted through actual SDK decode");
            check(accepted.freelookOptions.flightSpeed==(upper?16.f:.25f)
                &&accepted.freeCameraOptions.wheelDistance==(upper?4.f:.1f),"camera boundary values are retained without clamping");
        }
        auto reject=[&](ConfigJson bad) {
            auto original=bad;RuntimeConfig prior;prior.brightness=12;auto before=encode(prior);
            check(!decodeRuntimeConfig(bad,prior),"invalid camera/registry document rejected");
            check(bad==original&&encode(prior)==before,"decode failure preserves both original JSON and caller result");
        };
        for(auto field:{"freelook","freeCamera"})for(auto invalid:{ConfigJson(nullptr),ConfigJson(1),ConfigJson("true")}){auto bad=good;bad[field]=invalid;reject(bad);}
        for(auto field:{"freelookOptions","freeCameraOptions"}) {
            auto bad=good;bad[field]=nullptr;reject(bad);
            bad=good;bad[field]["hold"]=1;reject(bad);
            for(auto option:{"degreesPerPixel","wheelDistance","flightSpeed"}) {
                for(auto invalid:{ConfigJson(0.f),ConfigJson(-1.f),ConfigJson(17.f),ConfigJson(std::numeric_limits<double>::infinity()),ConfigJson("4")}) {
                    bad=good;bad[field][option]=invalid;reject(bad);
                }
            }
        }
        for(auto widths:{std::pair{15u,22u},std::pair{32u,21u}}){auto bad=good;bad["keybinds"]=std::vector<int>(widths.first,65);bad["keybindModifiers"]=std::vector<unsigned>(widths.second,8);reject(bad);}

        auto sourceFile=root/"profiles/source.json",targetFile=root/"profiles/target.json";
        unsigned matrix{};
        for(unsigned sourceWidth:{15u,19u,20u,21u,22u,32u})for(unsigned targetWidth:{22u,32u}) {
            auto source=document(sourceWidth,65,2),target=document(targetWidth,128,4);removeCamera(source);setTargetCamera(target);
            source["favoriteModules"]=0x55555555u&featureRegistryMask(sourceWidth);
            target["favoriteModules"]=0xaaaaaaaau&featureRegistryMask(targetWidth);
            source["sourceForeign"]={{"null",nullptr},{"value",17}};target["targetForeign"]={{"null",nullptr},{"value",29}};
            write(sourceFile,source);write(targetFile,target);auto loaded=loadWorker(sourceFile,100+matrix);
            if(!loaded)continue;
            check(ConfigJson::parse(loaded->sourceDocument)["keybinds"].size()==sourceWidth,"worker owns original unpadded source JSON");
            check(saveWorker(targetFile,loaded->config,loaded->sourceDocument),"cross-width camera Save As executes actual atomic worker");
            auto saved=read(targetFile);auto expectedWidth=std::max({featureCount,sourceWidth,targetWidth});
            check(saved["keybinds"].size()==expectedWidth&&saved["keybindModifiers"].size()==expectedWidth,"saved paired registry keeps full source/target extent");
            for(unsigned i=0;i<expectedWidth;++i)check(saved["keybinds"][i]==(i<sourceWidth?65+int(i):i<targetWidth?128+int(i):0)
                &&saved["keybindModifiers"][i]==(i<sourceWidth?(i+2)%9:i<targetWidth?(i+4)%9:8),"raw source declaration wins prefix; independent target gap/tail survives absent prefix");
            for(auto field:cameraFields)check(saved[field]==target[field],"absent untouched camera typed field inherits target with opaque nested null");
            auto mask=featureRegistryMask(sourceWidth);auto expectedFavorite=(target["favoriteModules"].get<std::uint32_t>()&~mask)|(source["favoriteModules"].get<std::uint32_t>()&mask);
            check(saved["favoriteModules"]==expectedFavorite,"declared raw favorite prefix contract preserves unrelated later target metadata");
            check(saved["sourceForeign"]==source["sourceForeign"]&&saved["targetForeign"]==target["targetForeign"],"opaque source and target JSON owners persist");
            auto reload=loadWorker(targetFile,200+matrix);
            check(reload&&reload->config.freelook&&reload->config.freeCamera&&reload->config.freelookOptions.flightSpeed==8&&reload->config.freeCameraOptions.wheelDistance==2,"camera enable/options survive Save As and actual SDK reload");
            ++matrix;
        }
        check(matrix==12,"all sparse source/target width cases completed");

        auto source=document(32,65,2);source["favoriteModules"]=~ownedFeatureSlotMask;source["foreignPayload"]={{"nullable",nullptr}};
        RuntimeConfig baseline;check(decodeRuntimeConfig(source,baseline),"32-slot opaque source decodes");auto current=baseline;
        for(unsigned i=0;i<32;++i)if(!(ownedFeatureSlotMask&featureRegistryBit(i))){current.keybinds[i]=0;current.keybindModifiers[i]=8;}
        current.favoriteModules=0;current.brightness=12;ProfileEditHistory ignored;
        recordProfileEdits(ignored,encode(baseline),encode(current));
        check(!ignored.keys&&!ignored.modifiers&&!ignored.favorites,"GUI/default differences never acquire foreign slot history");
        ignored.keys=ignored.modifiers=ignored.favorites=~ownedFeatureSlotMask;
        auto foreignTarget=document(32,128,4);write(targetFile,foreignTarget);
        check(saveWorker(targetFile,current,source.dump(),ignored),"foreign diff and caller-mask stress save drains");auto kept=read(targetFile);
        for(unsigned i=0;i<32;++i)if(!(ownedFeatureSlotMask&featureRegistryBit(i)))check(kept["keybinds"][i]==source["keybinds"][i]
            &&kept["keybindModifiers"][i]==source["keybindModifiers"][i]&&(kept["favoriteModules"].get<std::uint32_t>()&featureRegistryBit(i)),"raw gap/tail metadata survives unowned GUI diffs and injected edit masks");
        check(kept["foreignPayload"]["nullable"].is_null(),"opaque nested null survives sparse owner save");

        auto oldSource=document(15,65,2);removeCamera(oldSource);RuntimeConfig old;
        check(decodeRuntimeConfig(oldSource,old),"old15 source camera baseline decodes");auto on=old;
        on.freelook=on.freeCamera=true;on.freelookOptions.hold=on.freeCameraOptions.hold=false;
        on.freelookOptions.flightSpeed=on.freeCameraOptions.flightSpeed=8;
        on.keybinds[20]='L';on.keybinds[21]='C';on.keybindModifiers[20]=2;on.keybindModifiers[21]=4;on.favoriteModules|=cameraMask;
        ProfileEditHistory cameraEdits;recordProfileEdits(cameraEdits,encode(old),encode(on));recordProfileEdits(cameraEdits,encode(on),encode(old));
        check(cameraEdits.fields==cameraFieldMask&&cameraEdits.keys==cameraMask&&cameraEdits.modifiers==cameraMask&&cameraEdits.favorites==cameraMask,"both camera fields/key/mod/favorites retain explicit edit-to-baseline history");
        auto cameraTarget=document(32,128,4);setTargetCamera(cameraTarget);cameraTarget["favoriteModules"]=cameraMask|featureRegistryBit(17)|featureRegistryBit(31);
        write(targetFile,cameraTarget);check(saveWorker(targetFile,old,oldSource.dump(),cameraEdits),"camera return-to-baseline history saves through actual worker");auto resetCamera=read(targetFile);
        check(resetCamera["freelook"]==false&&resetCamera["freeCamera"]==false,"explicit camera ON-to-OFF overrides inherited target enable");
        for(auto field:{"freelookOptions","freeCameraOptions"})check(resetCamera[field]["hold"]==true&&resetCamera[field]["flightSpeed"]==4
            &&resetCamera[field]["foreignNull"].is_null(),"camera options reset wins while target unknown option survives");
        check(resetCamera["keybinds"][20]==0&&resetCamera["keybinds"][21]==0&&resetCamera["keybindModifiers"][20]==8&&resetCamera["keybindModifiers"][21]==8,"camera clear/modifier reset persists at20/21");
        check((resetCamera["favoriteModules"].get<std::uint32_t>()&cameraMask)==0
            &&(resetCamera["favoriteModules"].get<std::uint32_t>()&featureRegistryBit(17))&&(resetCamera["favoriteModules"].get<std::uint32_t>()&featureRegistryBit(31)),"camera unpin preserves foreign gap and bit31 favorites");
        ProfileEditHistory sameValueClear;sameValueClear.keys=sameValueClear.modifiers=cameraMask;
        write(targetFile,cameraTarget);check(saveWorker(targetFile,old,oldSource.dump(),sameValueClear),"unchanged camera Clear explicit mask admitted");
        check(read(targetFile)["keybinds"][20]==0&&read(targetFile)["keybindModifiers"][21]==8,"unchanged Clear owns camera bindings rather than inherited target values");

        auto bindingFree=ConfigJson{{"version",2},{"favoriteModules",cameraMask}};RuntimeConfig bindingFreeConfig;
        check(decodeRuntimeConfig(bindingFree,bindingFreeConfig),"binding-free camera favorite source decodes");
        cameraTarget["favoriteModules"]=featureRegistryBit(17)|featureRegistryBit(31)|featureRegistryBit(4);write(targetFile,cameraTarget);
        check(saveWorker(targetFile,bindingFreeConfig,bindingFree.dump()),"binding-free sparse favorite source saves");auto sparseFavorite=read(targetFile)["favoriteModules"].get<std::uint32_t>();
        check(sparseFavorite==(cameraMask|featureRegistryBit(17)|featureRegistryBit(31)),"binding-free exception claims owned slots only, leaving foreign gap/tail metadata");

        // Exercise the real SDK codec + production load/save worker on a legacy
        // enabled slot23, including unknown nested data, modifiers and favorites.
        auto legacy=document(32,65,2);legacy["inventoryHud"]=true;
        legacy["inventoryHudOptions"]["future"]={{"null",nullptr},{"payload",{1,"opaque",false}}};
        legacy["informationOptions"]["hudLayouts"]["inventory"]["x"]=77;
        legacy["informationOptions"]["hudLayouts"]["inventory"]["foreign"]=nullptr;
        legacy["keybinds"][23]='I';legacy["keybindModifiers"][23]=4;
        legacy["favoriteModules"]=featureRegistryBit(23)|featureRegistryBit(31);
        write(targetFile,legacy);auto retired=loadWorker(targetFile,9876);
        check(retired&&retired->config.inventoryHud,"legacy enabled value survives the real load worker");
        auto runtime=runtimeSettings(retired->config,kAllRenderPasses,UINT32_MAX);
        check(!runtime.on(Feature::InventoryHUD)&&!(ownedFeatureSlotMask&featureRegistryBit(23)),
              "old enabled=true cannot publish or register a slot23 hotkey");
        auto active=retired->config;active.noRender.enabled=true;
        active.inventoryHud=false;active.inventoryHudOptions.x=0;
        active.informationOptions.hudLayouts.inventory={};
        active.keybinds[23]=0;active.keybindModifiers[23]=8;active.favoriteModules&=~featureRegistryBit(23);
        check(saveWorker(targetFile,active,retired->sourceDocument),"legacy data saved through the actual worker after active UI edits");
        auto preserved=read(targetFile);
        check(preserved["inventoryHud"]==legacy["inventoryHud"]&&preserved["inventoryHudOptions"]==legacy["inventoryHudOptions"]
            &&preserved["informationOptions"]["hudLayouts"]["inventory"]==legacy["informationOptions"]["hudLayouts"]["inventory"],
              "retired typed values and unknown nested/layout data are preserved");
        check(preserved["keybinds"][23]=='I'&&preserved["keybindModifiers"][23]==4
            &&(preserved["favoriteModules"].get<unsigned>()&featureRegistryBit(23)),"reserved23 binding/modifier/favorite retained");
        for(unsigned slot:{15u,16u,17u,18u,19u,22u,25u,27u,28u,29u,30u,31u})
            check(preserved["keybinds"][slot]==legacy["keybinds"][slot]&&preserved["keybindModifiers"][slot]==legacy["keybindModifiers"][slot],
                  "all unknown slots survive production save");
        auto retiredAgain=loadWorker(targetFile,9877);
        check(retiredAgain&&!runtimeSettings(retiredAgain->config,kAllRenderPasses,UINT32_MAX).on(Feature::InventoryHUD),
              "saved legacy enabled=true cannot revive InventoryHUD on reload");

        auto badDestination=cameraTarget;badDestination["freeCameraOptions"]["flightSpeed"]="invalid";write(targetFile,badDestination);auto beforeBad=bytes(targetFile);
        check(!saveWorker(targetFile,old,oldSource.dump()),"invalid recognized existing camera destination rejects atomic save");
        check(bytes(targetFile)==beforeBad,"rejected destination remains byte-identical");
        write(targetFile,cameraTarget);check(saveWorker(targetFile,old,oldSource.dump()),"repaired isolated target supersedes retained failed save");
        std::cout<<"Camera normal config + actual SDK worker: "<<checks<<" checks "<<(failures?"FAIL":"PASS")<<"; Minecraft NOT_RUN\n";
        return failures?1:0;
    }catch(std::exception const& error){std::cerr<<"Fixture exception: "<<error.what()<<'\n';stopConfigStorage();return 2;}
}
