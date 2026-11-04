#include "resourcepacks/PackCatalog.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool v,char const* text) {
    ++checks;
    if(!v)throw std::runtime_error(text);
}
std::string id(int i) {
    char text[64]{};
    std::snprintf(text,sizeof(text),"0bca5230-10c1-49b2-a6f2-%012d",i);
    return text;
}
nlohmann::json metadata(int n,std::string scope="any",std::string type="resources") {
    nlohmann::json h={
        {"name","R28 Test "+std::to_string(n)},
        {"uuid",id(n)},{"version",{1,0,0}},{"pack_scope",scope}
    };
    return {
        {"format_version",2},{"header",h},
        {"modules",nlohmann::json::array({
            {{"type",type},{"uuid",id(n+1000)},{"version",{1,0,0}}}
        })}
    };
}
void write(std::filesystem::path path,std::string const& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);
    out<<bytes;
    check(out.good(),"cannot create owned test fixture");
}
Pack const& named(Catalog const& c,std::string_view n) {
    for(auto const& p:c.packs)if(p.name==n)return p;
    throw std::runtime_error("fixture missing in scanner result");
}
void mk(std::filesystem::path root,int index) {
    write(root/"manifest.json",metadata(index).dump());
    std::filesystem::create_directories(root/"textures");
}
}
int main(int argc,char** argv) {
    if(argc==2){
        std::atomic_bool cancel{};
        auto nativeCatalog=scanCatalog(std::filesystem::path(argv[1]),cancel);
        std::printf("R28 ACTUAL CATALOG: complete=%d count=%zu error=%s\n",
            int(nativeCatalog.complete),nativeCatalog.packs.size(),nativeCatalog.error.c_str());
        for(auto const& pack:nativeCatalog.packs)
            std::printf("  name=%s selectable=%u category=%s content=%s metadata=%u reason=%s\n",
                pack.name.c_str(),unsigned(pack.selectable),pack.categories.c_str(),
                pack.visualCoverage.c_str(),unsigned(pack.blockAtlasMetadata),pack.reason.c_str());
        return nativeCatalog.complete?0:5;
    }
    std::filesystem::path root;
    try {
        root=std::filesystem::temp_directory_path()/
            ("PraxisR28MultiTextureFixture-"+std::to_string(GetCurrentProcessId()));
        if(std::filesystem::exists(root))throw std::runtime_error("fixture root collision");
        std::filesystem::create_directory(root);
        mk(root/"simple",1);
        std::filesystem::create_directories(root/"simple"/"textures"/"blocks");
        std::filesystem::create_directories(root/"simple"/"textures"/"items");
        write(root/"simple"/"textures"/"blocks"/"stone.png","file content unimportant to catalog");
        write(root/"simple"/"textures"/"items"/"diamond_sword.png","file content unimportant");
        mk(root/"blocksjson",2);
        std::filesystem::create_directories(root/"blocksjson"/"textures"/"entity");
        write(root/"blocksjson"/"blocks.json",R"({"format_version":"1.19.30"})");
        mk(root/"particles",3);
        std::filesystem::create_directories(root/"particles"/"textures"/"particle");
        mk(root/"uiimages",4);
        std::filesystem::create_directories(root/"uiimages"/"textures"/"ui");
        mk(root/"unknownkind",5);
        mk(root/"worldscope",6);
        write(root/"worldscope"/"manifest.json",metadata(6,"world").dump());
        mk(root/"globalscope",7);
        write(root/"globalscope"/"manifest.json",metadata(7,"global").dump());
        mk(root/"unknownscope",8);
        write(root/"unknownscope"/"manifest.json",metadata(8,"other").dump());
        mk(root/"oversized",9);
        write(root/"oversized"/"blocks.json",std::string(8*1024*1024+1,' '));
        mk(root/"mixed",10);
        write(root/"mixed"/"sounds.json","{}");
        mk(root/"fontmix",11);
        std::filesystem::create_directories(root/"fontmix"/"font");
        mk(root/"blocksOnly",12);
        std::filesystem::remove_all(root/"blocksOnly"/"textures");
        write(root/"blocksOnly"/"blocks.json","{}");
        mk(root/"dataModule",13);
        write(root/"dataModule"/"manifest.json",metadata(13,"any","data").dump());
        mk(root/"dependency",14);
        auto depend=metadata(14);
        depend["dependencies"]=nlohmann::json::array({{{"uuid",id(1)},{"version",{1,0,0}}}});
        write(root/"dependency"/"manifest.json",depend.dump());
        mk(root/"subpack",15);
        auto sub=metadata(15);
        sub["subpacks"]=nlohmann::json::array({{{"folder_name","test"},{"name","Test"}}});
        write(root/"subpack"/"manifest.json",sub.dump());
        std::atomic_bool cancel{};
        auto c=scanCatalog(root,cancel);
        check(c.complete&&c.packs.size()==15,"all pack rows published");
        check(named(c,"R28 Test 1").selectable,"block/item multi-texture selectable");
        check(named(c,"R28 Test 1").visualCoverage.find("ブロック")!=std::string::npos,
            "block images classified");
        check(named(c,"R28 Test 1").visualCoverage.find("アイテム")!=std::string::npos,
            "item images classified");
        check(named(c,"R28 Test 2").selectable,"blocks.json with textures selectable");
        check(named(c,"R28 Test 2").blockAtlasMetadata,"blocks.json metadata detection");
        check(named(c,"R28 Test 2").visualCoverage.find("エンティティ")!=std::string::npos,
            "entity imagery classified");
        check(named(c,"R28 Test 3").visualCoverage.find("パーティクル")!=std::string::npos,
            "particle imagery classified");
        check(named(c,"R28 Test 4").visualCoverage.find("UI画像")!=std::string::npos,
            "UI image metadata classified without promising UI JSON");
        check(named(c,"R28 Test 5").selectable &&
              named(c,"R28 Test 5").visualCoverage=="一般テクスチャ",
              "unknown image subdirectories can still use native pack loader");
        check(!named(c,"R28 Test 6").selectable,"world-only scope not enabled globally");
        check(named(c,"R28 Test 7").selectable,"explicit Global scope allowed");
        check(!named(c,"R28 Test 8").selectable,"unknown manifest pack_scope rejected");
        check(!named(c,"R28 Test 9").selectable,"oversized blocks metadata rejected");
        check(!named(c,"R28 Test 10").selectable,"sounds.json mixed pack rejected");
        check(!named(c,"R28 Test 11").selectable,"font+texture mixed pack rejected");
        check(!named(c,"R28 Test 12").selectable,"blocks metadata without texture images rejected");
        check(!named(c,"R28 Test 13").selectable,"behavior module remains rejected");
        check(!named(c,"R28 Test 14").selectable,"dependency-bearing pack remains rejected");
        check(!named(c,"R28 Test 15").selectable,"subpack variant remains rejected");
        check(std::count_if(c.packs.begin(),c.packs.end(),
            [](auto const& p){return p.selectable;})==6,
            "exactly six independent texture-only rows");
        check(named(c,"R28 Test 2").categories=="textures",
            "old category contract preserved");
        std::filesystem::remove_all(root);
        std::printf("R28 GeneralTextureCatalog PASS (%u checks)\n",checks);
        return 0;
    }catch(std::exception const& e){
        if(!root.empty()){
            std::error_code ignored;
            std::filesystem::remove_all(root,ignored);
        }
        std::fprintf(stderr,"R28 catalog FAIL %s\n",e.what());
        return 1;
    }
}
