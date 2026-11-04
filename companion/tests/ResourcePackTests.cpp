#include "resourcepacks/PackBrowser.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>

using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool b,char const* message) {++checks;if(!b)throw std::runtime_error(message);}
std::string id(unsigned n) {char b[64];std::snprintf(b,sizeof(b),"00000000-0000-0000-0000-%012u",n);return b;}
nlohmann::json manifest(unsigned n,std::string type="resources") {
    return {{"format_version",2},{"header",{{"name","Fixture "+std::to_string(n)},{"uuid",id(n)},{"version",{1,0,0}}}},
        {"modules",nlohmann::json::array({{{"type",type},{"uuid",id(n+1000)},{"version",{1,0,0}}}})}};
}
void write(std::filesystem::path const& p,std::string const& text) {
    std::filesystem::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<text;
    check(out.good(),"fixture write");
}
auto contents(std::filesystem::path const& root) {
    std::map<std::string,std::string> result;
    for(auto const& e:std::filesystem::recursive_directory_iterator(root))if(e.is_regular_file()) {
        std::ifstream in(e.path(),std::ios::binary);result[e.path().string()]={std::istreambuf_iterator<char>(in),{}};
    }
    return result;
}
bool awaitIdle() {
    for(int i=0;i<600;++i) {if(!browserView().busy)return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    return false;
}
}
int main() {
 try {
    auto root=std::filesystem::temp_directory_path()/("PraxisResourcePackFixture-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    check(std::filesystem::create_directory(root),"unique owned fixture root");
    write(root/"a"/"manifest.json",manifest(1).dump());
    write(root/"b"/"manifest.json",manifest(2).dump());
    std::filesystem::create_directories(root/"a"/"textures");
    std::filesystem::create_directories(root/"fontshader"/"font");
    std::filesystem::create_directories(root/"fontshader"/"shaders");
    std::filesystem::create_directories(root/"b"/"textures");
    write(root/"fontshader"/"manifest.json",manifest(9).dump());
    write(root/"bad"/"manifest.json","{");
    write(root/"behavior"/"manifest.json",manifest(3,"data").dump());
    auto dependency=manifest(4);dependency["dependencies"]={{{"uuid",id(1)},{"version",{1,0,0}}}};
    write(root/"dependency"/"manifest.json",dependency.dump());
    write(root/"protected"/"manifest.json",manifest(5).dump());write(root/"protected"/"contents.json","{}");
    write(root/"large"/"manifest.json",std::string(128*1024+1,' '));
    auto future=manifest(6);future["format_version"]=3;write(root/"v3"/"manifest.json",future.dump());
    write(root/"archive.mcpack","fixture archive, never installed");
    auto duplicate=manifest(7);write(root/"dup1"/"manifest.json",duplicate.dump());write(root/"dup2"/"manifest.json",duplicate.dump());
    auto subpack=manifest(8);subpack["subpacks"]={{{"folder_name","test"}}};write(root/"subpack"/"manifest.json",subpack.dump());
    auto capabilities=manifest(10);capabilities["capabilities"]={"raytraced"};write(root/"pbr"/"manifest.json",capabilities.dump());
    std::filesystem::create_directories(root/"pbr"/"textures");
    auto before=contents(root);
    std::atomic_bool cancel{};auto catalog=scanCatalog(root,cancel);catalog.generation=10;
    check(catalog.complete&&catalog.packs.size()==14,"ordinary and excluded rows appear");
    check(std::count_if(catalog.packs.begin(),catalog.packs.end(),[](auto const& p){return p.selectable;})==2,"only independent resource manifests selectable");
    check(catalog.packs[0].categories=="textures","texture classification");
    check(std::any_of(catalog.packs.begin(),catalog.packs.end(),[](auto const& p){return p.categories=="font, shaders"&&!p.selectable;}),"font and shader distinct and excluded");
    check(std::any_of(catalog.packs.begin(),catalog.packs.end(),[](auto const& p){return p.name=="Fixture 10"&&!p.selectable&&p.reason.starts_with("Declared capabilities");}),"raytraced capabilities are not ordinary texture proposals");
    auto const a=catalog.packs[0].key;
    auto b=std::find_if(catalog.packs.begin(),catalog.packs.end(),[](auto const& p){return p.name=="Fixture 2";})->key;
    Selection selected;selected.reconcile(catalog);
    check(checkApply(catalog,selected,false)==ApplyResult::Valid,"empty optional Global selection allowed");
    check(selected.enable(catalog,a,true)&&selected.enable(catalog,b,true),"enable local proposals");
    check(selected.enable(catalog,a,true)&&selected.order.size()==2,"idempotent enable");
    check(!selected.move(a,-1)&&selected.move(b,-1)&&selected.order.front()==b,"priority boundary and move");
    auto saved=selected.order;selected.order.push_back(a);
    check(checkApply(catalog,selected,false)==ApplyResult::UnsupportedPack,"duplicate native identity selection refused");selected.order=saved;
    check(checkApply(catalog,selected,false)==ApplyResult::Valid&&selected.order==saved,"supported proposal validated without applying to Minecraft");
    check(checkApply(catalog,selected,true)==ApplyResult::Busy,"apply blocked during scan");
    ++catalog.generation;
    check(checkApply(catalog,selected,false)==ApplyResult::StaleCatalog,"stale generation rejected");
    selected.reconcile(catalog);check(selected.order==saved,"good refresh preserves priority");
    selected.order.push_back("missing");check(checkApply(catalog,selected,false)==ApplyResult::UnsupportedPack,"invalid entry rejected");
    selected.reconcile(catalog);check(selected.order==saved,"missing entries removed");
    check(selected.enable(catalog,a,false)&&selected.order.size()==1,"disable proposal");
    check(!selected.enable(catalog,"missing",true),"invalid enable rejected");
    auto failed=scanCatalog(root/"not-present",cancel);selected.reconcile(failed);
    check(!failed.complete&&selected.order.size()==1,"failed scan retains proposal");
    check(!scanCatalog("relative",cancel).complete,"relative path rejected");
    check(!scanCatalog(L"\\\\invalid-server\\share",cancel).complete,"UNC rejected before filesystem enumeration");
    cancel=true;check(!scanCatalog(root,cancel).complete,"cancelled scan unpublished");cancel=false;
    auto cap=root/"many";std::filesystem::create_directories(cap);
    for(int i=0;i<513;++i)std::filesystem::create_directory(cap/std::to_string(i));
    check(!scanCatalog(cap,cancel).complete,"entry cap aborts partial catalog");
    before=contents(root);
    check(startBrowser()&&startBrowser(),"browser lifecycle starts idempotently");
    check(requestScan(root),"async request accepted");
    check(awaitIdle(),"background work completes");
    auto good=browserView();check(good.catalog->complete,"background catalog published");
    check(requestScan(root/"not-present")&&awaitIdle(),"async failure completes");
    auto bad=browserView();check(bad.catalog==good.catalog&&!bad.status.empty(),"failure retains immutable catalog identity");
    check(stopBrowser()&&stopBrowser(),"worker drain and double shutdown");
    check(!requestScan(root),"stopped worker rejects requests");
    check(startBrowser()&&stopBrowser(),"clean browser restart");
    check(contents(root)==before,"catalog and apply leave fixture files unchanged");
    std::printf("Resource packs: %u checks PASS; no Minecraft process or pack mutation\n",checks);
    return 0;
 } catch(std::exception const& e) {
    stopBrowser();std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;
 }
}
