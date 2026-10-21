#include "resourcepacks/PackCatalog.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <set>

namespace praxis::companion::packs {
namespace {
bool plainPath(std::filesystem::path const& path) {
    auto attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}
std::string utf8(std::filesystem::path const& p) {
    auto s = p.u8string(); return {reinterpret_cast<char const*>(s.data()), s.size()};
}
bool uuidValid(std::string const& s) {
    if(s.size()!=36) return false;
    for(std::size_t i=0;i<s.size();++i) {
        if(i==8||i==13||i==18||i==23) {if(s[i]!='-')return false;}
        else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;
    }
    return true;
}
std::string label(std::string s) {
    if(s.size()>512) {
        std::size_t end=512;
        while(end&&((static_cast<unsigned char>(s[end])&0xc0)==0x80))--end;
        s.resize(end);
    }
    for(char& c:s) if(static_cast<unsigned char>(c)<32) c=' ';
    return s;
}
Pack readPack(std::filesystem::path const& path,std::atomic_bool const& cancel,std::chrono::steady_clock::time_point deadline) {
    Pack p; p.key=utf8(path); p.name=label(utf8(path.filename()));
    p.reason="Manifest missing, unsupported or invalid";
    if(!plainPath(path)) {p.reason="Linked directories are excluded";return p;}
    auto manifest=path/"manifest.json";
    if(!plainPath(manifest))return p;
    std::error_code ec;
    auto size=std::filesystem::file_size(manifest,ec);
    if(ec||size==0||size>128*1024) {p.reason="Manifest unreadable or exceeds 128 KiB";return p;}
    // Fixed maximum read also bounds a concurrently growing file.
    std::string bytes(128*1024+1,'\0');
    std::ifstream in(manifest,std::ios::binary); in.read(bytes.data(),bytes.size());
    auto count=in.gcount();
    if(in.bad()||count<1||count>128*1024)return p;
    bytes.resize(static_cast<std::size_t>(count));
    auto j=nlohmann::json::parse(bytes,nullptr,false,true);
    if(j.is_discarded()||!j.is_object())return p;
    try {
        if(j.at("format_version")!=2) {p.reason="Only manifest v2 is supported by this candidate";return p;}
        auto const& h=j.at("header");
        p.name=label(h.at("name").get<std::string>());
        if(p.name.empty())return p;
        p.uuid=h.at("uuid").get<std::string>();
        std::transform(p.uuid.begin(),p.uuid.end(),p.uuid.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!uuidValid(p.uuid))return p;
        auto const& v=h.at("version");
        if(!v.is_array()||v.size()!=3)return p;
        for(auto const& n:v) if(!n.is_number_integer()||n.get<std::int64_t>()<0||n.get<std::int64_t>()>65535)return p;
        p.version=std::to_string(v[0].get<int>())+"."+std::to_string(v[1].get<int>())+"."+std::to_string(v[2].get<int>());
        auto const& modules=j.at("modules");
        if(!modules.is_array()||modules.empty()||modules.size()>64)return p;
        std::set<std::string> ids{p.uuid};
        for(auto const& m:modules) {
            if(m.at("type")!="resources") {p.reason="Behavior/script/skin modules are excluded";return p;}
            auto id=m.at("uuid").get<std::string>();
            std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(!uuidValid(id)||!ids.insert(id).second)return p;
        }
        if(std::filesystem::exists(path/"contents.json",ec)) {p.reason="Protected content is excluded";return p;}
        if(j.contains("dependencies")&&!j.at("dependencies").empty()) {p.reason="Dependencies need normal Minecraft pack management";return p;}
        if(j.contains("subpacks")&&!j.at("subpacks").empty()) {p.reason="Subpack selection needs normal Minecraft pack management";return p;}
        if(j.contains("capabilities")&&!j.at("capabilities").empty()) {p.reason="Declared capabilities need normal Minecraft pack management";return p;}
        // Only global/any-scoped packs may be added to Global in the offline
        // world. Pack-scope "world" must use normal world pack management.
        if(h.contains("pack_scope")){
            auto const scope=h.at("pack_scope").get<std::string>();
            if(scope=="world"){p.reason="World-scoped pack: use normal Minecraft world pack settings";return p;}
            if(scope!="any"&&scope!="global"){
                p.reason="Unknown resource pack scope";return p;
            }
        }
        for(auto const& [dir,category]:std::array<std::pair<char const*,char const*>,6>{{
            {"textures","textures"},{"font","font"},{"shaders","shaders"},
            {"materials","materials"},{"models","models"},{"ui","UI"}}}) {
            if(std::filesystem::exists(path/dir,ec)) {
                if(!p.categories.empty())p.categories+=", ";p.categories+=category;
            }
        }
        if(p.categories.empty())p.categories="resources (manifest only)";
        // Reject mixed categories, including currently unknown future categories.
        // Texture-directory metadata is a proposal classification only. It
        // never proves content compatibility or permission to reload the game.
        // Bedrock's official resource-pack format places blocks.json
        // alongside manifest.json. This metadata can define block texture
        // aliases, so excluding it prevented many real block packs from
        // appearing in R27's selector.
        std::set<std::string> allowed{
            "manifest.json","pack_icon.png","textures","blocks.json",
            "README.md","LICENSE","LICENSE.txt"
        };
        unsigned topEntries{};
        for(auto const& item:std::filesystem::directory_iterator(path)) {
            if(cancel.load()||std::chrono::steady_clock::now()>deadline||++topEntries>512){p.reason="Pack enumeration cancelled or exceeded its budget";return p;}
            if(!plainPath(item.path())||!allowed.contains(utf8(item.path().filename()))) {
                p.reason="Mixed/unsupported content: use normal pack management and re-enter";return p;
            }
        }
        if(p.categories!="textures") {p.reason="This candidate supports ordinary texture packs only";return p;}
        auto textureDir=path/"textures";
        if(!plainPath(textureDir)||!std::filesystem::is_directory(textureDir,ec)||ec){
            p.reason="Texture directory must be a normal local directory";return p;
        }
        auto blockCatalog=path/"blocks.json";
        if(std::filesystem::exists(blockCatalog,ec)){
            if(ec||!plainPath(blockCatalog)
                ||!std::filesystem::is_regular_file(blockCatalog,ec)||ec
                ||std::filesystem::file_size(blockCatalog,ec)>8*1024*1024||ec){
                p.reason="blocks.json must be a regular local file under 8 MiB";return p;
            }
            p.blockAtlasMetadata=true;
        }
        // Bounded known-directory probes only: never recursively enumerate a
        // whole arbitrary pack on the owner/UI thread. Unknown nested textures
        // remain allowed and may be handled by the Minecraft native loader.
        auto tag=[&](char const* directory,char const* label){
            auto location=textureDir/directory;
            std::error_code localError;
            if(std::filesystem::exists(location,localError)
                &&!localError&&plainPath(location)
                &&std::filesystem::is_directory(location,localError)&&!localError){
                if(!p.visualCoverage.empty())p.visualCoverage+=" / ";
                p.visualCoverage+=label;
            }
        };
        tag("blocks","ブロック");
        tag("items","アイテム");
        tag("entity","エンティティ");
        tag("entities","エンティティ");
        tag("particle","パーティクル画像");
        tag("particles","パーティクル画像");
        tag("environment","環境画像");
        tag("gui","GUI画像");
        tag("ui","UI画像");
        if(p.visualCoverage.empty())p.visualCoverage="一般テクスチャ";
        if(p.blockAtlasMetadata)p.visualCoverage+=" + blocks.json";
        p.selectable=true;
        p.reason="Registered texture resources can use Minecraft's native Full Stack reload";
    } catch(...) {p.selectable=false;}
    return p;
}
}
Catalog scanCatalog(std::filesystem::path const& root,std::atomic_bool const& cancel) {
    Catalog result; result.root=root;
    try {
        // Fixed local drives only, with no junction/symlink ancestors or UNC I/O.
        if(!root.is_absolute()||root.has_root_name()==false||utf8(root).starts_with("\\\\")
           ||GetDriveTypeW(root.root_path().c_str())!=DRIVE_FIXED) {
            result.error="Choose an absolute folder on a local fixed drive";return result;
        }
        auto cursor=root;
        while(!cursor.empty()) {
            if(!plainPath(cursor)) {result.error="Missing or linked folder is excluded";return result;}
            auto parent=cursor.parent_path();if(parent==cursor)break;cursor=parent;
        }
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        std::error_code ec;
        std::filesystem::directory_iterator it(root,ec),end;
        if(ec) {result.error="Unable to read the selected folder";return result;}
        unsigned visited{};
        while(it!=end) {
            if(cancel.load()||std::chrono::steady_clock::now()>deadline||++visited>512) {
                result.error="Scan cancelled or exceeded the 512-entry / 2-second limit";return result;
            }
            auto path=it->path();
            if(!plainPath(path)) { result.packs.push_back(readPack(path,cancel,deadline)); }
            else if(it->is_directory(ec)) result.packs.push_back(readPack(path,cancel,deadline));
            else if(path.extension()==".mcpack"||path.extension()==".zip") {
                Pack p;p.key=utf8(path);p.name=label(utf8(path.filename()));p.reason="Archive: import using Minecraft first";result.packs.push_back(std::move(p));
            }
            if(ec) {result.error="Folder changed or became unreadable during scanning";return result;}
            it.increment(ec);if(ec) {result.error="Folder enumeration failed";return result;}
        }
        std::set<std::string> duplicateIds;
        for(auto const& p:result.packs)if(!p.uuid.empty()) {
            auto count=std::count_if(result.packs.begin(),result.packs.end(),[&](auto const& q){return p.uuid==q.uuid;});
            if(count>1)duplicateIds.insert(p.uuid);
        }
        for(auto& p:result.packs)if(duplicateIds.contains(p.uuid)) {p.selectable=false;p.reason="Duplicate pack UUID: resolve in Minecraft first";}
        std::sort(result.packs.begin(),result.packs.end(),[](auto const& a,auto const& b){return a.key<b.key;});
        result.complete=true;
    } catch(...) {result.error="Unable to inspect local pack manifests";}
    return result;
}
void Selection::reconcile(Catalog const& catalog) {
    if(!catalog.complete)return;
    std::erase_if(order,[&](auto const& key){return std::none_of(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key&&p.selectable;});});
    generation=catalog.generation;
}
bool Selection::enable(Catalog const& catalog,std::string const& key,bool value) {
    if(!catalog.complete||generation!=catalog.generation)return false;
    auto pack=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key&&p.selectable;});
    if(pack==catalog.packs.end())return false;
    auto found=std::find(order.begin(),order.end(),key);
    if(value&&found==order.end())order.push_back(key);
    if(!value&&found!=order.end())order.erase(found);
    return true;
}
bool Selection::move(std::string const& key,int direction) {
    auto found=std::find(order.begin(),order.end(),key);
    if(found==order.end()||(direction!=-1&&direction!=1))return false;
    auto index=found-order.begin();auto next=index+direction;
    if(next<0||next>=static_cast<std::ptrdiff_t>(order.size()))return false;
    std::swap(order[index],order[next]);return true;
}
ApplyResult checkApply(Catalog const& catalog,Selection const& selection,bool busy) noexcept {
    if(busy)return ApplyResult::Busy;
    if(!catalog.complete||selection.generation!=catalog.generation)return ApplyResult::StaleCatalog;
    for(std::size_t i=0;i<selection.order.size();++i){
        auto const& key=selection.order[i];
        if(std::find(selection.order.begin(),selection.order.begin()+i,key)!=selection.order.begin()+i)return ApplyResult::UnsupportedPack;
        if(std::none_of(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key&&p.selectable;}))return ApplyResult::UnsupportedPack;
    }
    return ApplyResult::Valid;
}
}
