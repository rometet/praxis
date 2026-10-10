#include "map/MapCore.h"
#include "map/MapPersistence.h"
#include "map/chiyan/TerrainPalette.h"
#include "map/TickValueCache.h"
#include "map/TerrainBaker.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <unordered_set>
#include <stdexcept>
#include <thread>
using namespace praxis::companion::map;
namespace {
int passed{}, assertions{};
void check(bool result, char const *condition, int line) {
    ++assertions;
    if (!result)
        throw std::runtime_error(std::string(condition) + " @ " + std::to_string(line));
}
#define CHECK(x) check(bool(x), #x, __LINE__)
void test(char const *name, std::function<void()> fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
}
Session session(std::string world = "exact-world", std::uint64_t generation = 1, int dimension = 0) {
    return {world, generation, dimension, true};
}
Sample sample() {
    Sample s;
    s.known = true;
    s.color = 0xff228844;
    s.height = 64;
    std::copy_n("plains", 7, s.biome.begin());
    return s;
}
struct Source : DataSource {
    int calls{};
    bool loaded{true};
    int airBelow = 64;
    std::optional<int> surfaceHeight(int, int) override {
        ++calls;
        return loaded ? std::optional<int>{65} : std::nullopt;
    }
    std::optional<Voxel> voxel(int, int y, int) override {
        ++calls;
        if (!loaded)
            return {};
        return Voxel{y >= airBelow, 0xff228844};
    }
    std::array<char, 64> biome(int, int, int) override {
        ++calls;
        return sample().biome;
    }
};
std::vector<IoResult> wait(Persistence &io, Session s) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        auto r = io.take(s);
        if (!r.empty())
            return r;
        if (std::chrono::steady_clock::now() > until)
            throw std::runtime_error("I/O completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void reseal(Bytes &bytes) {
    std::uint64_t checksum = 14695981039346656037ull;
    for (std::size_t i = 0; i + 8 < bytes.size(); ++i) {
        checksum ^= bytes[i];
        checksum *= 1099511628211ull;
    }
    for (int n = 0; n < 8; ++n)
        bytes[bytes.size() - 8 + n] = std::uint8_t(checksum >> (n * 8));
}
} // namespace
int main() {
    try {
        test("tick-local value cache preserves signed XYZ collision and full identifier tint", [] {
            TickValueCache<std::array<int,3>> cache;unsigned queries=0;
            auto query=[&](int x,int y,int z){++queries;return std::array{x,y,z};};
            CHECK((cache.get(-256,64,-512,query)==std::array{-256,64,-512}));
            CHECK((cache.get(-256,64,-512,query)==std::array{-256,64,-512}));CHECK(queries==1&&cache.hits()==1);
            CHECK((cache.get(-256,65,-512,query)==std::array{-256,65,-512}));
            for(int n=0;n<32;++n)CHECK((cache.get(n*256,0,0,query)==std::array{n*256,0,0}));
            CHECK((cache.get(-256,64,-512,query)==std::array{-256,64,-512}));CHECK(cache.queries()==queries);
            TickValueCache<chiyan::Tints> colors;
            auto fullId=std::string("minecraft:")+std::string(90,'x')+"_swamp";
            auto direct=chiyan::biomeTints(fullId);
            auto tintQuery=[&](int,int,int){return chiyan::biomeTints(fullId);};
            auto first=colors.get(-1,70,-1,tintQuery);auto second=colors.get(-1,70,-1,tintQuery);
            CHECK(first.grass==direct.grass&&second.water==direct.water&&colors.queries()==1&&colors.hits()==1);
            CHECK(direct.grass!=chiyan::biomeTints(fullId.substr(0,63)).grass);
        });
        test("Chiyan detailed palette preserves grass dyes wood and biome tint", [] {
            auto plains=chiyan::biomeTints("minecraft:plains"),jungle=chiyan::biomeTints("minecraft:jungle");
            auto color=[&](std::string const& name,chiyan::Tints t) {return chiyan::getBlockColor(name,t.grass,t.foliage,t.water);};
            CHECK(color("minecraft:grass_block",plains)==plains.grass);
            CHECK(color("minecraft:grass_block",jungle)==jungle.grass);
            CHECK(plains.grass!=jungle.grass);
            CHECK(color("minecraft:dirt",plains)!=plains.grass);
            constexpr std::array dyes{"white","orange","magenta","light_blue","yellow","lime","pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"};
            for(auto family:{"_wool","_concrete","_terracotta"}) {
                std::vector<std::uint32_t> colors;
                for(auto dye:dyes) {
                    bool recognized{};
                    auto c=chiyan::getBlockColor(std::string("minecraft:")+dye+family,plains.grass,plains.foliage,plains.water,&recognized);
                    CHECK(recognized&&c.a==1);colors.push_back(*packMapColor(c.r,c.g,c.b,c.a));
                }
                std::sort(colors.begin(),colors.end());CHECK(std::unique(colors.begin(),colors.end())==colors.end());
            }
            auto log=color("minecraft:oak_log",plains),wood=color(chiyan::GetHorizontalWoodName("minecraft:oak_log"),plains);
            CHECK(log!=wood);
            CHECK(chiyan::GetHorizontalWoodName("minecraft:warped_stem")=="minecraft:warped_hyphae");
            CHECK(chiyan::IsInvisibleOrTechnicalOverlay("minecraft:barrier"));
            CHECK(!chiyan::IsInvisibleOrTechnicalOverlay("minecraft:end_portal_frame"));
            CHECK(color("minecraft:barrier",plains).a==0);
            bool recognized{};auto unknown=chiyan::getBlockColor("addon:unmapped_block",plains.grass,plains.foliage,plains.water,&recognized);
            CHECK(!recognized&&unknown==chiyan::Color(.55f,.55f,.55f,1));
            auto water=chiyan::BlendWaterOverFloor({.2f,.4f,.6f,1},{.6f,.2f,.8f,1});
            CHECK(std::abs(water.r-.46f)<.0001f&&std::abs(water.g-.27f)<.0001f&&water.a==1);
            auto blend=chiyan::blendTints({plains,jungle,plains,jungle},.5f,.5f);
            CHECK(std::abs(blend.grass.r-(plains.grass.r+jungle.grass.r)/2)<.0001f);
        });
        test("dense Chiyan rings have no missing or duplicate mapart columns", [] {
            for(int r:{0,1,2,16,64,256}) {
                std::vector<std::pair<int,int>> coords;
                for(int i=0;i<(r?8*r:1);++i) {
                    int x,z;chiyan::GetRingCoords(r,i,x,z);
                    CHECK(std::max(std::abs(x),std::abs(z))==r);coords.emplace_back(x,z);
                }
                std::sort(coords.begin(),coords.end());CHECK(std::unique(coords.begin(),coords.end())==coords.end());
            }
            Scanner scan;auto s=session();View v{0,0,64};scan.request(s,{&v,1});CHECK(scan.demandSize()==129*129);
            v.radius=256;scan.request(s,{&v,1});CHECK(scan.demandSize()==513*513);
        });
        test("dense retarget retains traversal fairness beyond the refresh interval", [] {
            Cache cache;auto s=session();cache.begin(s);Scanner scan;Source source;Settings settings;
            settings.scanMilliseconds=4;
            for(int tick=1;tick<=900;++tick) {
                if(tick==1||(tick-1)%37==0) {View v{(((tick-1)/37)&1)*8,0,64};scan.request(s,{&v,1});}
                scan.tick(cache,source,settings,tick);
            }
            for(int z=-48;z<=48;z+=16)for(int x=-40;x<=48;x+=16)
                CHECK(cache.find(tileKey(x,z,0,Layer::Surface),x,z,0));
        });
        test("proven known prefix saves lookups without changing the native reservation stream", [] {
            struct Weighted:Source {
                unsigned heights{},voxels{},biomes{};
                std::optional<int> surfaceHeight(int x,int z)override{++heights;return Source::surfaceHeight(x,z);}
                std::optional<Voxel> voxel(int x,int y,int z)override{++voxels;return Source::voxel(x,y,z);}
                std::array<char,64> biome(int x,int y,int z)override{++biomes;return Source::biome(x,y,z);}
                std::uint32_t queryCost(Query q)const noexcept override{return q==Query::Height?4:q==Query::Voxel?24:2;}
            } source;
            source.airBelow=65;Cache cache;auto s=session();cache.begin(s);Scanner scan;
            Settings settings;settings.scanMilliseconds=4;std::uint64_t logicalSkips{},avoided{};
            for(int t=1;t<=1600;++t){
                View view{t/10,0,64};scan.request(s,{&view,1});auto stats=scan.tick(cache,source,settings,t);
                CHECK(stats.reads<=1024&&stats.columns<=128&&stats.visited<=scan.demandSize());
                logicalSkips+=stats.skipped;avoided+=stats.prefixSkipped;
            }
            CHECK(source.heights==30000&&source.voxels==60000&&source.biomes==30000);
            CHECK(logicalSkips==311359&&avoided>200000);
            View view{160,0,64};CHECK(cache.view({&view,1}).size()==10445);
        });
        test("known prefix is invalidated by eviction and explicit unknown samples", [] {
            Cache cache(64);auto s=session();cache.begin(s);Scanner scan;Source source;
            Settings settings;settings.scanMilliseconds=4;View view{0,0,16};
            for(int z=-16;z<=16;++z)for(int x=-16;x<=16;++x)cache.apply(s,tileKey(x,z,0,Layer::Surface),x,z,sample(),1);
            scan.request(s,{&view,1});scan.tick(cache,source,settings,2);
            view.x=1;scan.request(s,{&view,1});scan.tick(cache,source,settings,3);
            cache.setTileLimit(1);cache.setTileLimit(64);source.loaded=false;
            view.x=2;scan.request(s,{&view,1});auto unavailable=scan.tick(cache,source,settings,4);
            CHECK(unavailable.unavailable>0&&unavailable.prefixSkipped==0);
            source.loaded=true;for(int t=5;t<=100;++t)scan.tick(cache,source,settings,t);
            CHECK(cache.view({&view,1}).size()==33*33);
            Sample unknown=sample();unknown.known=false;
            CHECK(cache.apply(s,tileKey(2,0,0,Layer::Surface),2,0,unknown,101));
            view.x=3;scan.request(s,{&view,1});source.loaded=false;
            auto invalidated=scan.tick(cache,source,settings,102);CHECK(invalidated.unavailable>0);
            source.loaded=true;for(int t=103;t<=220;++t)scan.tick(cache,source,settings,t);
            CHECK(cache.known(tileKey(2,0,0,Layer::Surface),2,0));
            scan.cancel();auto next=session("exact-world",2);cache.begin(next);scan.request(next,{&view,1});
            auto fresh=scan.tick(cache,source,settings,221);CHECK(fresh.prefixSkipped==0&&fresh.columns>0);
        });
        test("known prefix intersections preserve unequal two-view reverse and teleport coverage", [] {
            Cache cache;auto s=session();cache.begin(s);Scanner scan;Source source;
            Settings settings;settings.scanMilliseconds=4;
            std::array views{View{0,0,16},View{80,0,32}};
            for(int t=1;t<=300;++t){
                views[0].x=std::abs(t%16-8);views[1].z=std::abs(t%10-5);
                scan.request(s,views);auto stats=scan.tick(cache,source,settings,t);
                CHECK(stats.reads<=1024&&stats.visited<=scan.demandSize());
            }
            for(int t=301;t<=600;++t)scan.tick(cache,source,settings,t);
            CHECK(cache.view(views).size()==33*33+65*65);
            views[0].x=10000;views[1].z=10000;scan.request(s,views);
            CHECK(scan.tick(cache,source,settings,601).prefixSkipped==0);
            for(int t=602;t<=950;++t)scan.tick(cache,source,settings,t);
            CHECK(cache.view(views).size()==33*33+65*65);
        });
        test("surface uses actual roof height and visible overlay independent of player Y", [] {
            struct Roof:Source {
                int base{319},overlay{319};
                std::optional<SurfaceColumn> surfaceColumn(int,int) override {++calls;return SurfaceColumn{base,-64,overlay};}
                std::optional<Voxel> voxel(int,int y,int) override {++calls;return Voxel{false,y==319?0xff000000u:0xffffffffu};}
            } source;
            Cache c;auto s=session();c.begin(s);Scanner scan;View v{0,0,16,Layer::Surface,70,38};scan.request(s,{&v,1});
            Settings settings;settings.columnsPerTick=1;settings.readsPerTick=4;settings.scanMilliseconds=4;
            scan.tick(c,source,settings,1);auto a=c.find(tileKey(0,0,0,Layer::Surface),0,0,0);
            CHECK(a&&a->height==319&&a->color==0xff000000u);
            c.begin(session("low-mapart",2));source.base=-64;source.overlay=-63;scan.request(c.session(),{&v,1});
            scan.tick(c,source,settings,1);a=c.find(tileKey(0,0,0,Layer::Surface),0,0,0);
            CHECK(a&&a->height==-63&&a->color==0xffffffffu);
        });
        test("unready placeholder preserves valid pixel without deep scanning", [] {
            struct Pending:Source {
                std::optional<Voxel> voxel(int,int,int) override {++calls;return {};}
            } source;
            Cache c;auto s=session();c.begin(s);c.apply(s,tileKey(0,0,0,Layer::Surface),0,0,sample(),1);
            Scanner scan;View v{0,0,16};scan.request(s,{&v,1});Settings set;set.readsPerTick=2;set.scanMilliseconds=4;
            auto stats=scan.tick(c,source,set,200);CHECK(stats.reads==2&&stats.unavailable==1&&stats.exhausted==0);
            CHECK(c.find(tileKey(0,0,0,Layer::Surface),0,0,0)==sample());
        });
        test("water searches bounded seafloor and keeps biome water over floor", [] {
            struct Water:Source {
                std::optional<SurfaceColumn> surfaceColumn(int,int) override {++calls;return SurfaceColumn{70,-64,{}};}
                std::optional<Voxel> voxel(int,int y,int) override {++calls;return y>=67?Voxel{false,0xffcc6633u,true,true,true,true}:Voxel{false,0xff6699ccu};}
            } source;
            Cache c;auto s=session();c.begin(s);Scanner scan;View v{0,0,16};scan.request(s,{&v,1});Settings set;
            set.readsPerTick=1;set.columnsPerTick=1;set.scanMilliseconds=4;
            for(int t=1;t<=7;++t){auto before=source.calls;auto stats=scan.tick(c,source,set,t);CHECK(stats.reads<=1&&source.calls-before<=1);}
            auto a=c.find(tileKey(0,0,0,Layer::Surface),0,0,0);CHECK(a&&a->height==66&&a->color!=0xffcc6633u&&a->color!=0xff6699ccu);
        });
        test("base terrain is shaded once and legacy brown cache is isolated", [] {
            Cache c;auto s=session();c.begin(s);auto base=sample();base.height=70;
            c.apply(s,tileKey(0,0,0,Layer::Surface),0,0,base,1);auto low=base;low.height=69;
            c.apply(s,tileKey(0,-1,0,Layer::Surface),0,-1,low,1);c.apply(s,tileKey(-1,0,0,Layer::Surface),-1,0,low,1);
            View v{0,0,1};auto view=c.view({&v,1});auto find=std::find_if(view.begin(),view.end(),[](Cell const& a){return a.x==0&&a.z==0;});
            CHECK(find!=view.end()&&find->sample.color!=base.color);
            CHECK(c.find(tileKey(0,0,0,Layer::Surface),0,0,0)==base);
            auto bytes=encodeTiles(s.world,c.exportTiles(3));CHECK(decodeTiles(bytes,s.world));
            bytes[3]='1';reseal(bytes);CHECK(!decodeTiles(bytes,s.world));
        });
        test("resident publication matches dense reference across views layers and bounds", [] {
            Cache cache;auto s=session();cache.begin(s);
            for(auto layer:{Layer::Surface,Layer::Cave}) for(int z=-19;z<=19;++z) for(int x=-19;x<=19;++x) {
                if((x+z)%3==0)continue;
                auto value=sample();value.height=std::int16_t(50+(x-z)%9);
                value.color=0xff000000u|std::uint32_t((x+20)*211+(z+20)*1221);
                CHECK(cache.apply(s,tileKey(x,z,0,layer,68,36),x,z,value,1));
            }
            for(auto layer:{Layer::Surface,Layer::Cave}) {
                std::array views{View{-4,2,16,layer,68,36},View{8,-6,16,layer,layer==Layer::Cave?72:0,40}};
                auto actual=cache.view(views);std::vector<Cell> expected;
                for(int z=-22;z<=22;++z)for(int x=-22;x<=24;++x) {
                    std::uint8_t mask=0;
                    for(std::size_t i=0;i<views.size();++i) {
                        auto const& v=views[i];
                        if(std::abs(x-v.x)<=v.radius&&std::abs(z-v.z)<=v.radius &&
                            (layer==Layer::Surface||(v.top==68&&v.bottom==36)))mask|=std::uint8_t(1u<<i);
                    }
                    if(!mask)continue;
                    auto key=tileKey(x,z,0,layer,68,36);auto value=cache.find(key,x,z,0);if(!value)continue;
                    auto north=cache.find(tileKey(x,z-1,0,layer,68,36),x,z-1,0);
                    auto west=cache.find(tileKey(x-1,z,0,layer,68,36),x-1,z,0);
                    auto c=value->color;
                    auto shaded=chiyan::shadeTerrain({float(c&255)/255,float((c>>8)&255)/255,float((c>>16)&255)/255,1},
                        value->height,north?north->height:value->height,west?west->height:value->height);
                    value->color=*packMapColor(shaded.r,shaded.g,shaded.b,shaded.a);
                    expected.push_back({x,z,1,*value,key,mask,{1,1}});
                }
                CHECK(actual.size()==expected.size());
                for(std::size_t i=0;i<actual.size();++i) {
                    auto const& a=actual[i];auto const& b=expected[i];
                    CHECK(a.x==b.x&&a.z==b.z&&a.sample==b.sample&&a.key==b.key&&a.viewMask==b.viewMask);
                }
            }
            for(int edge:{-coordinateLimit,coordinateLimit}) {
                CHECK(cache.apply(s,tileKey(edge,edge,0,Layer::Surface),edge,edge,sample(),1));
                View v{edge,edge,16};auto cells=cache.view({&v,1});
                CHECK(cells.size()==1&&cells[0].x==edge&&cells[0].z==edge);
            }
            View centre{0,0,16};CHECK(cache.view({&centre,1},0).empty());
            CHECK(cache.view({&centre,1},11).size()<=11);
        });
        test("value-only terrain worker rejects retired world and cave publications", [] {
            TerrainBaker baker;Cache c;auto old=session();c.begin(old);c.apply(old,tileKey(0,0,0,Layer::Surface),0,0,sample(),1);
            View v{0,0,16};baker.enqueue({old,{v,v},1,1,c.exportTiles(1)});baker.cancel();CHECK(!baker.take(old,v));
            auto fresh=session("new-world",2,1);c.begin(fresh);c.apply(fresh,tileKey(0,0,1,Layer::Surface),0,0,sample(),1);
            baker.enqueue({fresh,{v,v},1,2,c.exportTiles(1)});
            std::shared_ptr<BakedTerrain const> baked;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!baked&&std::chrono::steady_clock::now()<deadline){baked=baker.take(fresh,v);std::this_thread::yield();}
            CHECK(baked&&baked->cells->size()==1&&baked->session==fresh);
            CHECK(!baker.take(old,v));View cave=v;cave.layer=Layer::Cave;cave.top=68;cave.bottom=36;CHECK(!baker.take(fresh,cave));
            baker.cancel();CHECK(!baker.take(fresh,v));CHECK(baked->cells->size()==1);
        });
        test("native map RGB packing preserves dye and channel order", [] {
            CHECK(packMapColor(1,0,0)==0xff0000ffu);
            CHECK(packMapColor(0,0,1)==0xffff0000u);
            CHECK(packMapColor(1,1,1)==0xffffffffu);
            CHECK(packMapColor(0,0,0)==0xff000000u);
            CHECK(packMapColor(-1,2,0)==0xff00ff00u);
            CHECK(!packMapColor(std::numeric_limits<float>::quiet_NaN(),0,0));
            CHECK(!packMapColor(0,0,0,0));
            CHECK(!packMapColor(1,1,1,0));
            CHECK(packMapColor(0,0,0,1)==0xff000000u);
            CHECK(!packMapColor(1,0,0,std::numeric_limits<float>::quiet_NaN()));
        });
        test("negative chunk and region boundaries", [] {
            for (int n : {-513, -512, -511, -17, -16, -15, -1, 0, 1, 15, 16, 17, 511, 512, 513}) {
                auto q = floorDiv(n, 16), r = floorMod(n, 16);
                CHECK(q * 16 + r == n);
                CHECK(r >= 0 && r < 16);
                auto region = floorDiv(q, 32);
                CHECK(region * 32 + floorMod(q, 32) == q);
            }
            CHECK(floorDiv(-1, 16) == -1);
            CHECK(cellIndex(-1, -1) == 255);
        });
        test("world dimension and generation separation", [] {
            Cache c(2);
            auto a = session();
            c.begin(a);
            auto k = tileKey(-1, -1, 0, Layer::Surface);
            CHECK(c.apply(a, k, -1, -1, sample(), 1));
            CHECK(!c.apply(session("other"), k, -1, -1, sample(), 1));
            CHECK(!c.apply(session("exact-world", 2), k, -1, -1, sample(), 1));
            CHECK(!c.apply(session("exact-world", 1, 1), k, -1, -1, sample(), 1));
            c.begin(session("other"));
            CHECK(c.size() == 0);
        });
        test("integer extremes and invalid grid sizes", [] {
            for (int n : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
                for (int divisor : {3, 16, 32}) {
                    auto q = floorDiv(n, divisor), r = floorMod(n, divisor);
                    CHECK(std::int64_t(q) * divisor + r == n);
                    CHECK(r >= 0 && r < divisor);
                }
            CHECK(floorDiv(1, 0) == 0);
            CHECK(floorMod(1, -1) == 0);
        });
        test("local storage identity excludes names seeds and ambiguous connections", [] {
            auto a = localWorldIdentity("E:/worlds/a", "same-level-name-or-seed", true);
            auto b = localWorldIdentity("E:/worlds/b", "same-level-name-or-seed", true);
            CHECK(a && b && *a != *b);
            CHECK(a == localWorldIdentity("E:\\worlds\\a\\", "same-level-name-or-seed", true));
            CHECK(a == localWorldIdentity("E:/worlds/temp/../a", "same-level-name-or-seed", true));
            CHECK(a != localWorldIdentity("E:/worlds/a", "replacement-world", true));
            CHECK(!localWorldIdentity("E:/worlds/a", "id", false));
            CHECK(!localWorldIdentity("relative/world", "id", true));
            CHECK(!localWorldIdentity("E:/worlds/a", "", true));
            CHECK(!localWorldIdentity(std::string("E:/bad\0world", 12), "id", true));
            CHECK(!localWorldIdentity("E:/worlds/a", std::string(512, 'x'), true));
        });
        test("LRU and byte admission", [] {
            Cache c(2);
            auto s = session();
            c.begin(s);
            for (int x : {0, 16})
                CHECK(c.apply(s, tileKey(x, 0, 0, Layer::Surface), x, 0, sample(), 1));
            CHECK(c.find(tileKey(0, 0, 0, Layer::Surface), 0, 0, 1));
            CHECK(c.apply(s, tileKey(32, 0, 0, Layer::Surface), 32, 0, sample(), 1));
            CHECK(c.size() == 2);
            CHECK(!c.find(tileKey(16, 0, 0, Layer::Surface), 16, 0, 1));
            CHECK(c.bytes() < 100000);
            Cache tiny(2, 1);
            tiny.begin(s);
            CHECK(!tiny.apply(s, tileKey(0, 0, 0, Layer::Surface), 0, 0, sample(), 1));
            auto owned = c.find(tileKey(32, 0, 0, Layer::Surface), 32, 0, 0);
            c.setTileLimit(1);
            CHECK(c.size() == 1);
            c.begin(session("replacement"));
            CHECK(owned && owned->height == 64); // No cache ref survives eviction.
        });
        test("bounded exploration and snapshot", [] {
            Cache c(8);
            auto s = session();
            c.begin(s);
            for (int i = 0; i < 10000; ++i) {
                CHECK(c.apply(s, tileKey(i * 16, 0, 0, Layer::Surface), i * 16, 0, sample(), 1));
                CHECK(c.size() <= 8);
                CHECK(c.bytes() < 250000);
            }
            std::array v{View{0, 0, 256}, View{128, 128, 256}};
            CHECK(c.view(v).size() <= maxCells);
        });
        test("rotation inverse and north lock", [] {
            for (double yaw : {-180., -90., 0., 90., 180., 359.}) {
                Transform t{{-42, 95}, {80, 80}, 2, mapAngle(yaw, false)};
                Point p{-57, 106};
                auto q = t.unproject(t.project(p));
                CHECK(std::abs(q.x - p.x) < 1e-9);
                CHECK(std::abs(q.z - p.z) < 1e-9);
                CHECK(mapAngle(yaw, true) == 0);
            }
            Transform mini{{10, 20}, {100, 100}, 2, 0}, big{{10, 20}, {400, 300}, 4, 0};
            auto a = mini.project({15, 17}), b = big.project({15, 17});
            CHECK((a.x - 100) * 2 == b.x - 400);
            CHECK((a.z - 100) * 2 == b.z - 300);
        });
        test("scanner resumes within hard query budget", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            Scanner scan;
            std::array views{View{0, 0, 64}};
            scan.request(s, views);
            Source source;
            Settings set;
            set.readsPerTick = 1;
            set.columnsPerTick = 1;
            set.scanMilliseconds = 4;
            for (int n = 0; n < 5; ++n) {
                auto before = source.calls;
                auto stats = scan.tick(c, source, set, n + 1);
                CHECK(stats.reads <= 1);
                CHECK(source.calls - before <= 1);
            }
            CHECK(c.find(tileKey(0, 0, 0, Layer::Surface), 0, 0, 0));
        });
        test("scanner center priority unloaded and cancellation", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            Scanner scan;
            std::array v{View{-16, -16, 256}, View{512, 512, 256}};
            scan.request(s, v);
            CHECK(scan.demandSize() <= maxCells);
            Source source;
            Settings set;
            set.scanMilliseconds = 4;
            auto stats = scan.tick(c, source, set, 1);
            CHECK(stats.columns <= unsigned(set.columnsPerTick));
            CHECK(stats.reads <= 1024);
            CHECK(c.find(tileKey(-18, -18, 0, Layer::Surface), -18, -18, 0));
            c.begin(session("other", 2));
            auto before = source.calls;
            CHECK(scan.tick(c, source, set, 2).reads == 0);
            CHECK(source.calls == before);
            scan.cancel();
            CHECK(scan.demandSize() == 0);
            scan.request(c.session(), v);
            source.loaded = false;
            CHECK(scan.tick(c, source, set, 3).columns == 0);
        });
        test("malformed view never borrows source", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            Scanner scan;
            Source source;
            Settings settings;
            for (auto v : {View{0, 0, 16, Layer(2), std::numeric_limits<int>::min(), 0},
                           View{0, 0, 16, Layer::Cave, 0, 1},
                           View{0, 0, 16, Layer::Cave, std::numeric_limits<int>::max(), 0},
                           View{std::numeric_limits<int>::min(), 0, 16}, View{0, 0, 257}}) {
                scan.request(s, {&v, 1});
                CHECK(scan.demandSize() == 0);
                CHECK(scan.tick(c, source, settings, 1).reads == 0);
            }
            CHECK(source.calls == 0);
        });
        test("transparent technical cave ceiling cannot invent an air cavity", [] {
            struct Solid:Source {
                std::optional<Voxel> voxel(int,int y,int) override {
                    ++calls;return Voxel{false,0xff228844u,false,false,y<64};
                }
            } source;
            Cache cache;cache.begin(session());Scanner scan;View view{0,0,16,Layer::Cave,68,36};
            scan.request(cache.session(),{&view,1});Settings settings;
            settings.readsPerTick=33;settings.scanMilliseconds=4;
            auto stats=scan.tick(cache,source,settings,1);
            CHECK(stats.noColor==5&&stats.exhausted==1&&stats.columns==0);
            CHECK(!cache.find(tileKey(0,0,0,Layer::Cave,68,36),0,0,0));
        });
        test("cave band resume and cache identity", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            Scanner scan;
            std::array v{View{0, 0, 16, Layer::Cave, 68, 36}};
            scan.request(s, v);
            Source source;
            Settings set;
            set.scanMilliseconds = 4;
            set.readsPerTick = 1;
            for (int i = 0; i < 10; ++i)
                scan.tick(c, source, set, i + 1);
            auto k = tileKey(0, 0, 0, Layer::Cave, 68, 36);
            auto a = c.find(k, 0, 0, 0);
            CHECK(a);
            CHECK(a->height == 63);
            CHECK(!c.find(tileKey(0, 0, 0, Layer::Cave, 60, 28), 0, 0, 0));
        });
        test("waypoint add edit delete and duplicate", [] {
            Waypoints w;
            Waypoint a{1, "Home", -1, 64, -513, 0};
            CHECK(w.add(a));
            CHECK(!w.add(a));
            a.name = "Mine";
            CHECK(w.edit(a));
            CHECK(w.items()[0].name == "Mine");
            CHECK(w.remove(1));
            CHECK(!w.remove(1));
        });
        test("waypoint malformed transactional validation", [] {
            Waypoints w;
            CHECK(w.add({1, "Home", 0, 64, 0, 0}));
            auto bad = w.items();
            bad.push_back(bad.front());
            CHECK(!w.replace(bad));
            CHECK(w.items().size() == 1);
            for (auto name : {"", "\nname"})
                CHECK(!w.add({2, name, 0, 64, 0, 0}));
            Waypoint nan{2, "NaN", std::numeric_limits<double>::quiet_NaN(), 64, 0, 0};
            CHECK(!w.add(nan));
        });
        test("waypoint codec exact world corruption truncation", [] {
            std::vector<Waypoint> w{{1, "Home", -1, 64, -513, 0}, {2, "Nether", 32, 80, 4, 1}};
            auto bytes = encodeWaypoints("world", w);
            auto copy = decodeWaypoints(bytes, "world");
            CHECK(copy && *copy == w);
            CHECK(!decodeWaypoints(bytes, "world-prefix"));
            bytes[10] ^= 1;
            CHECK(!decodeWaypoints(bytes, "world"));
            bytes.resize(12);
            CHECK(!decodeWaypoints(bytes, "world"));
            CHECK(encodeWaypoints("", w).empty());
        });
        test("terrain persistence checksum identity restore", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            CHECK(c.apply(s, tileKey(-1, -17, 0, Layer::Surface), -1, -17, sample(), 1));
            auto tiles = c.exportTiles(512);
            auto bytes = encodeTiles(s.world, tiles);
            auto copy = decodeTiles(bytes, s.world);
            CHECK(copy && copy->size() == 1);
            Cache restored;
            restored.begin(s);
            CHECK(restored.restore(s, copy->front()));
            CHECK(restored.find(tileKey(-1, -17, 0, Layer::Surface), -1, -17, 0));
            CHECK(!decodeTiles(bytes, "wrong"));
            bytes.back() ^= 1;
            CHECK(!decodeTiles(bytes, s.world));
        });
        test("terrain encoder and checksum-valid malformed documents", [] {
            Tile t;
            t.key = tileKey(0, 0, 0, Layer::Surface);
            t.samples[0] = sample();
            auto bytes = encodeTiles("world", {&t, 1});
            CHECK(decodeTiles(bytes, "world"));
            // Header: magic4, version2, world length2 + world5, count2.
            std::size_t key = 15;
            for (auto [offset, value] :
                 {std::pair{key + 20, std::uint8_t(2)}, std::pair{key + 8, std::uint8_t(255)}}) {
                auto malformed = bytes;
                malformed[offset] = value;
                reseal(malformed);
                CHECK(!decodeTiles(malformed, "world"));
            }
            auto trailing = bytes;
            trailing.insert(trailing.end() - 8, 0);
            reseal(trailing);
            CHECK(!decodeTiles(trailing, "world"));
            t.samples[0].biome.fill('x');
            CHECK(encodeTiles("world", {&t, 1}).empty());
            t.samples[0] = sample();
            t.samples[0].height = 2048;
            CHECK(encodeTiles("world", {&t, 1}).empty());
            t.samples[0] = sample();
            t.key.layer = Layer(2);
            CHECK(encodeTiles("world", {&t, 1}).empty());
            t.key = tileKey(0, 0, 0, Layer::Surface);
            std::array duplicate{t, t};
            CHECK(encodeTiles("world", duplicate).empty());
        });
        test("settings codec bounds", [] {
            Settings s;
            auto bytes = encodeSettings(s);
            auto copy = decodeSettings(bytes);
            CHECK(copy && *copy == s);
            s.radius = 257;
            CHECK(!s.valid());
            CHECK(encodeSettings(s).empty());
            bytes[10] ^= 1;
            CHECK(!decodeSettings(bytes));
        });
        test("historical terrain cannot overwrite live samples", [] {
            Cache c;
            auto s = session();
            c.begin(s);
            auto k = tileKey(0, 0, 0, Layer::Surface);
            auto live = sample();
            live.height = 99;
            CHECK(c.apply(s, k, 0, 0, live, 5));
            Tile historical;
            historical.key = k;
            historical.samples[0] = sample();
            historical.samples[1] = sample();
            CHECK(c.restore(s, historical));
            CHECK(c.find(k, 0, 0, 0)->height == 99);
            CHECK(c.find(k, 1, 0, 0)->height == 64);
            CHECK(!c.restore(session("other"), historical));
        });
        test("waypoint maximum and filenames", [] {
            Waypoints w;
            for (std::size_t i = 1; i <= maxWaypoints; ++i)
                CHECK(w.add({i, "point", double(i), 64, 0, 0}));
            CHECK(!w.add({maxWaypoints + 1, "overflow", 0, 64, 0, 0}));
            CHECK(worldFilename("world-A") != worldFilename("world-B"));
            CHECK(worldFilename("../world/path").find('/') == std::string::npos);
        });
        test("persistence joined write read and stale rejection", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapTests-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto s = session();
            Persistence io(root);
            std::vector<Waypoint> w{{1, "Home", 0, 64, 0, 0}};
            CHECK(io.enqueue({s, FileKind::Waypoints, 1, true, w}));
            auto ack = wait(io, s);
            CHECK(ack.front().okay);
            CHECK(io.enqueue({s, FileKind::Waypoints, 0, false, {}}));
            auto data = wait(io, s);
            CHECK(data.front().okay);
            CHECK(std::get<std::vector<Waypoint>>(data.front().payload) == w);
            CHECK(!io.enqueue({session("", 1), FileKind::Terrain, 0, false, {}}));
            CHECK(io.enqueue({s, FileKind::Waypoints, 0, false, {}}));
            io.stopAndJoin();
            CHECK(io.take(session("other", 2)).empty());
            CHECK(io.staleRejected() >= 1);
            CHECK(!io.enqueue({s, FileKind::Waypoints, 1, true, w}));
        });
        test("persistence corruption preserves source file", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapCorrupt-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto s = session();
            auto dir = root / worldFilename(s.world);
            std::filesystem::create_directories(dir);
            auto path = dir / "waypoints.ppm";
            {
                std::ofstream f(path);
                f << "broken";
            }
            Persistence io(root);
            CHECK(io.enqueue({s, FileKind::Waypoints, 0, false, {}}));
            auto data = wait(io, s);
            CHECK(!data.front().okay);
            CHECK(std::filesystem::file_size(path) == 6);
            io.stopAndJoin();
        });
        test("persistence write refuses corrupted existing document", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapWriteReject-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto s = session();
            auto dir = root / worldFilename(s.world);
            std::filesystem::create_directories(dir);
            auto path = dir / "waypoints.ppm";
            {
                std::ofstream f(path);
                f << "original-corrupted-evidence";
            }
            Persistence io(root);
            std::vector<Waypoint> w{{1, "New", 0, 64, 0, 0}};
            CHECK(io.enqueue({s, FileKind::Waypoints, 1, true, w}));
            auto data = wait(io, s);
            CHECK(!data.front().okay);
            CHECK(data.front().recovered);
            CHECK(std::filesystem::file_size(path) == 27);
            auto recovery = path;
            recovery += ".recovery.ppm";
            Bytes recovered(std::size_t(std::filesystem::file_size(recovery)));
            std::ifstream input(recovery, std::ios::binary);
            input.read(reinterpret_cast<char *>(recovered.data()), std::streamsize(recovered.size()));
            CHECK(decodeWaypoints(recovered, s.world) == std::optional{w});
            io.stopAndJoin();
        });
        test("persistence oversized job rejected", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapBudget-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Persistence io(root);
            std::vector<Tile> enormous(1024);
            CHECK(!io.enqueue({session(), FileKind::Terrain, 1, true, std::move(enormous)}));
            CHECK(io.queuedBytes() <= 8 * 1024 * 1024);
            io.stopAndJoin();
        });
        test("failed retired-world save preserves corrupt and foreign recovery evidence", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapForeignRecovery-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto s = session("unique-failed-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto dir = root / worldFilename(s.world);
            std::filesystem::create_directories(dir);
            auto path = dir / "waypoints.ppm";
            {
                std::ofstream f(path);
                f << "corrupt";
            }
            auto recovery = path;
            recovery += ".recovery.ppm";
            std::vector<Waypoint> w{{1, "Foreign", 0, 64, 0, 0}};
            auto foreign = encodeWaypoints("different-world", w);
            {
                std::ofstream f(recovery, std::ios::binary);
                f.write(reinterpret_cast<char const *>(foreign.data()), std::streamsize(foreign.size()));
            }
            auto emergency = std::filesystem::temp_directory_path() / "PraxisMapRecovery" /
                             worldFilename(s.world) / "waypoints.ppm";
            std::filesystem::create_directories(emergency.parent_path());
            {
                std::ofstream f(emergency, std::ios::binary);
                f.write(reinterpret_cast<char const *>(foreign.data()), std::streamsize(foreign.size()));
            }
            Persistence io(root);
            CHECK(io.enqueue({s, FileKind::Waypoints, 1, true, w}));
            io.flush();
            auto errors = io.take(session("next-world", 2));
            CHECK(errors.size() == 1 && !errors[0].okay && !errors[0].recovered);
            CHECK(errors[0].session == s); // Retired-world errors are still reported.
            CHECK(std::get<std::vector<Waypoint>>(errors[0].payload) == w); // Failed job transfers ownership.
            CHECK(std::filesystem::file_size(path) == 7);
            Bytes preserved(foreign.size());
            std::ifstream input(recovery, std::ios::binary);
            input.read(reinterpret_cast<char *>(preserved.data()), std::streamsize(preserved.size()));
            CHECK(preserved == foreign);
            io.stopAndJoin();
        });
        test("unwritable mod root saves owned edits to application temp recovery", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapUnavailable-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            {
                std::ofstream f(root);
                f << "not-a-directory";
            }
            auto s = session("unavailable-" + root.filename().string());
            std::vector<Waypoint> w{{1, "Recover me", -1, 64, -512, 1}};
            Persistence io(root);
            CHECK(io.enqueue({s, FileKind::Waypoints, 1, true, w}));
            io.flush();
            auto results = io.take(session("replacement", 2));
            CHECK(results.size() == 1 && !results[0].okay && results[0].recovered);
            auto path = std::filesystem::temp_directory_path() / "PraxisMapRecovery" /
                        worldFilename(s.world) / "waypoints.ppm";
            Bytes bytes(std::size_t(std::filesystem::file_size(path)));
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
            CHECK(decodeWaypoints(bytes, s.world) == std::optional{w});
            CHECK(!io.enqueue({s, FileKind::Options, 0, false, {}}));
            io.stopAndJoin();
        });
        test("shutdown flush drains jobs without disabling admission", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapFlush-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Persistence io(root);
            auto s = session();
            std::vector<Waypoint> values{{1, "Saved", 0, 64, 0, 0}};
            CHECK(io.enqueue({s, FileKind::Waypoints, 1, true, values}));
            io.flush();
            auto first = io.take(s);
            CHECK(first.size() == 1 && first.front().okay);
            CHECK(io.enqueue({s, FileKind::Waypoints, 0, false, {}}));
            io.flush();
            auto loaded = io.take(s);
            CHECK(loaded.size() == 1 && loaded.front().okay);
            io.stopAndJoin();
        });
        test("completion saturation preserves every accepted load and acknowledgement", [] {
            auto root = std::filesystem::temp_directory_path() /
                        ("PraxisMapCompletion-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Persistence io(root);
            auto s = session();
            int accepted = 0;
            for (int n = 0; n < 80; ++n) {
                if (io.enqueue({s, FileKind::Waypoints, 0, false, {}}))
                    ++accepted;
                CHECK(io.queuedBytes() <= 8 * 1024 * 1024);
            }
            CHECK(accepted > 0 && accepted <= 32);
            io.flush();
            auto completed = io.take(s);
            CHECK(completed.size() == std::size_t(accepted));
            for (auto const &r : completed)
                CHECK(r.okay && r.missing);
            CHECK(io.staleRejected() == 0);
            // A dimension/generation transition must save before reloading.
            std::vector<Waypoint> w{{1, "Newest", -1, 64, -16, 0}};
            auto next = session(s.world, 2, 1);
            CHECK(io.enqueue({s, FileKind::Waypoints, 7, true, w}));
            CHECK(io.enqueue({next, FileKind::Waypoints, 0, false, {}}));
            io.flush();
            auto data = io.take(next);
            CHECK(data.size() == 2 && data[0].okay && data[0].write && data[0].session == s);
            CHECK(data[1].okay && !data[1].write && data[1].session == next);
            CHECK(std::get<std::vector<Waypoint>>(data[1].payload) == w);
            io.stopAndJoin();
            CHECK(io.queuedBytes() <= 8 * 1024 * 1024);
        });
        test("pointer zoom preserves world coordinate and bounds", [] {
            for (double angle : {0., .7, -1.2}) for (double scale : {.2, 1., 8.}) {
                Transform t{{-513, 90}, {400, 300}, scale, angle};
                Point mouse{450, 260};
                auto p = t.unproject(mouse);
                auto changed = t.zoomAt(mouse, scale * 2);
                auto q = changed.unproject(mouse);
                CHECK(std::abs(p.x - q.x) < 1e-9);
                CHECK(std::abs(p.z - q.z) < 1e-9);
                auto snap = t.projectPixel(p);
                CHECK(scale >= 1 || (snap.x == std::round(snap.x) && snap.z == std::round(snap.z)));
            }
            Transform edge{{coordinateLimit, coordinateLimit}, {10,10}, 1, 0};
            CHECK(coordinate(edge.zoomAt({100,100}, 8).worldCenter.x));
            CHECK(edge.zoomAt({1,1}, 0).pixelsPerBlock == 1);
        });
        test("both historical v2 layouts migrate without resetting old or new options", [] {
            Settings s;s.enabled=true;s.columnsPerTick=32;s.zoom=2;s.minimap=false;s.radarNames=true;
            s.anchor=Anchor::BottomLeft;s.offsetX=-12;s.actionKeys={77,187,189,67,72,78};
            auto v3=encodeSettings(s);CHECK(v3[4]==3&&v3.size()==96);
            auto longV2=v3;longV2[4]=2;reseal(longV2);CHECK(decodeSettings(longV2)==s);
            auto shortV2=longV2;shortV2.erase(shortV2.begin()+59,shortV2.end()-8);reseal(shortV2);
            auto old=decodeSettings(shortV2);CHECK(old&&old->enabled&&old->columnsPerTick==32&&old->zoom==2);
            CHECK(old->minimap&&!old->radarNames&&old->anchor==Anchor::TopRight);
            CHECK(decodeSettings(encodeSettings(*old))==old);
            auto malformed=longV2;malformed.erase(malformed.end()-9);reseal(malformed);CHECK(!decodeSettings(malformed));
            auto shortV3=shortV2;shortV3[4]=3;reseal(shortV3);CHECK(!decodeSettings(shortV3));
            auto root=std::filesystem::temp_directory_path()/
                ("PraxisMapV2-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(root);auto path=root/"map-options.ppm";
            {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(longV2.data()),longV2.size());}
            Persistence io(root);CHECK(io.enqueue({{},FileKind::Options,0,false,{}}));
            auto loaded=wait(io,{});CHECK(loaded[0].okay&&loaded[0].optionsMigrated);
            CHECK(std::get<Settings>(loaded[0].payload)==s);
            CHECK(io.enqueue({{},FileKind::Options,1,true,s}));CHECK(wait(io,{})[0].okay);
            CHECK(std::filesystem::file_size(path)==v3.size());
            auto backup=path;backup+=".v2-extended-backup";CHECK(std::filesystem::file_size(backup)==longV2.size());
            Bytes copy(longV2.size());std::ifstream f(backup,std::ios::binary);f.read(reinterpret_cast<char*>(copy.data()),copy.size());
            CHECK(copy==longV2);io.stopAndJoin();
        });
        test("anchors offsets and options v1 migration", [] {
            Settings s;
            for (auto a : {Anchor::TopRight, Anchor::TopLeft, Anchor::BottomRight, Anchor::BottomLeft}) {
                s.anchor = a;
                s.offsetX = 42; s.offsetY = 20;
                auto center = minimapCenter(s, {800, 600});
                CHECK(center.x >= 96 && center.x <= 704);
                CHECK(center.z >= 96 && center.z <= 464);
                auto copy = decodeSettings(encodeSettings(s));
                CHECK(copy && *copy == s);
            }
            s.offsetX = -4096; s.offsetY = 4096;
            CHECK(minimapCenter(s, {80,60}).x == 96);
            auto legacy = encodeSettings(Settings{});
            // v1 ends after the original 59-byte payload, before appended v2 fields.
            legacy.erase(legacy.begin() + 59, legacy.end() - 8);
            legacy[4] = 1;
            reseal(legacy);
            auto old = decodeSettings(legacy);
            CHECK(old && *old == Settings{});
            s.anchor = Anchor(4); CHECK(!s.valid());
        });
        test("dense demand preserves one-block movement and shared immutable publication", [] {
            Cache c;c.begin(session());Scanner scanner;TerrainPublication publication;
            std::array views{View{0,0,64},View{0,0,64}};
            scanner.request(c.session(),views);
            CHECK(scanner.demandSize()==2*129*129);
            auto builds=scanner.builds();auto a=publication.get(c,views);CHECK(a->empty());
            scanner.request(c.session(),views);CHECK(scanner.builds()==builds);
            CHECK(publication.get(c,views)==a);
            views[0].x=views[1].x=1;scanner.request(c.session(),views);
            CHECK(scanner.builds()==builds+1); // 1-block art must follow exact world movement.
            auto moved=publication.get(c,views);CHECK(moved!=a);
            auto s=sample();CHECK(c.apply(c.session(),tileKey(0,0,0,Layer::Surface),0,0,s,1));
            auto b=publication.get(c,views);CHECK(b->size()==1&&b->front().viewMask==3);
            auto revision=c.revision();CHECK(c.apply(c.session(),tileKey(0,0,0,Layer::Surface),0,0,s,5));
            CHECK(c.revision()==revision&&publication.get(c,views)==b);
            views[1].x=100;scanner.request(c.session(),views);CHECK(scanner.builds()==builds+2);
            auto unionCells=publication.get(c,views);CHECK(unionCells->size()==1&&unionCells->front().viewMask==1);
        });
        test("copied biome containment layer band and readable names", [] {
            Cell known{0,0,4,sample(),tileKey(0,0,0,Layer::Cave,68,36)};
            std::array cells{known, Cell{20,20,4,{}}};
            CHECK(sampleAt(cells, {1,1}, Layer::Cave,68,36));
            CHECK(!sampleAt(cells, {1,1}, Layer::Surface));
            CHECK(!sampleAt(cells, {1,1}, Layer::Cave,64,32));
            CHECK(!sampleAt(cells, {20,20}, Layer::Cave,68,36));
            CHECK(!sampleAt(cells, {3,3}, Layer::Cave,68,36));
            CHECK(readableBiome("minecraft:cherry_grove") == "Cherry Grove");
            CHECK(readableBiome("") == "Unknown biome");
        });
        test("cave hysteresis negative Y Nether and rapid changes", [] {
            CaveBand b;
            auto first = b.select(64,32);
            CHECK(first == std::pair(68,36));
            CHECK(b.select(63.5,32) == first);
            CHECK(b.select(64.5,32) == first);
            CHECK(b.select(62,32).first == 64);
            CHECK(b.select(-63.5,32) == std::pair(-60,-92));
            CHECK(b.select(120,64) == std::pair(124,60));
            for (double y : {-2048.,-128.,0.,2047.,-700.,60.}) {
                auto [top,bottom] = b.select(y,64);
                CHECK(top >= bottom && top - bottom == 64 && bottom >= -2048 && top <= 2047);
            }
        });
        test("cave thin floor vegetation water unloaded and deep hole", [] {
            struct Cave : Source {
                int floor{-70}, unknown{9999};
                bool water{}, plant{};
                std::optional<Voxel> voxel(int, int y, int) override {
                    ++calls;
                    if (y == unknown) return {};
                    if (y == floor) return Voxel{false,0xffa0a0a0};
                    return Voxel{!water && !plant,0xffc08030,water || plant,water};
                }
            } source;
            auto s = session("cave",1,1);
            Settings settings;
            settings.readsPerTick = 1; settings.columnsPerTick = 1; settings.scanMilliseconds = 4;
            for (int variant = 0; variant < 5; ++variant) {
                Cache cache; cache.begin(s); Scanner scan;
                std::array view{View{0,0,1,Layer::Cave,-60,-92}};
                source.unknown = variant == 3 ? -65 : 9999;
                source.water = variant == 1; source.plant = variant == 2;
                source.floor = variant == 4 ? -200 : -70;
                scan.request(s,view);
                for (int tick = 1; tick < 34; ++tick) {
                    auto stats = scan.tick(cache,source,settings,tick);
                    CHECK(stats.reads <= 1 && stats.columns <= 1);
                }
                auto floor = cache.find(tileKey(0,0,1,Layer::Cave,-60,-92),0,0,0);
                if (variant >= 3) CHECK(!floor);
                else {
                    CHECK(floor && floor->height == -70);
                    CHECK(variant != 1 || floor->color != 0xffa0a0a0);
                }
                auto other = View{0,0,1,Layer::Cave,120,88};
                scan.request(s,{&other,1});
                CHECK(!cache.find(tileKey(0,0,1,Layer::Cave,120,88),0,0,0));
            }
        });
        test("native cost reservation and resumable demand movement", [] {
            struct Weighted : Source {
                std::uint32_t queryCost(Query) const noexcept override { return 6; }
            } source;
            Cache c; c.begin(session()); Scanner scan;
            std::array views{View{0,0,64,Layer::Cave,68,36}};
            scan.request(c.session(),views);
            Settings settings; settings.readsPerTick = 5; settings.scanMilliseconds = 4;
            CHECK(scan.tick(c,source,settings,1).reads == 0);
            CHECK(source.calls == 0);
            settings.readsPerTick = 6;
            CHECK(scan.tick(c,source,settings,2).reads == 6);
            views[0].x = 1;
            scan.request(c.session(),views);
            for (int t = 3; t < 10; ++t) CHECK(scan.tick(c,source,settings,t).reads <= 6);
            CHECK(c.find(tileKey(0,0,0,Layer::Cave,68,36),0,0,0));
        });
        test("rapid cave band replacement and 64 vertical read ceiling", [] {
            Cache c; c.begin(session()); Scanner scan; Source source;
            Settings settings; settings.readsPerTick = 1; settings.scanMilliseconds = 4;
            View old{0,0,1,Layer::Cave,68,36};
            scan.request(c.session(),{&old,1});
            CHECK(scan.tick(c,source,settings,1).reads == 1);
            View next{0,0,1,Layer::Cave,-60,-92};
            source.airBelow = -70;
            scan.request(c.session(),{&next,1});
            for (int t = 2; t < 18; ++t) CHECK(scan.tick(c,source,settings,t).reads <= 1);
            CHECK(!c.find(tileKey(0,0,0,Layer::Cave,68,36),0,0,0));
            CHECK(c.find(tileKey(0,0,0,Layer::Cave,-60,-92),0,0,0)->height == -71);
            c.begin(session("deep",2)); source.calls = 0;
            source.airBelow = -2048;
            next = {0,0,1,Layer::Cave,128,64};
            scan.request(c.session(),{&next,1});
            settings.readsPerTick = 1024;
            auto stats = scan.tick(c,source,settings,1);
            CHECK(stats.columns == 0 && stats.reads == 64*9 && source.calls == 64*9);
        });
        test("waypoint visibility v1 migration and nearest dimension ordering", [] {
            std::vector<Waypoint> w{{2,"far",30,64,0,0},{1,"near",1,64,0,0},{3,"other",0,64,0,1}};
            w[1].visible = false;
            auto order = orderedWaypoints(w,0,{0,0},true);
            CHECK(order == std::vector<std::size_t>{0});
            order = orderedWaypoints(w,0,{0,0},false);
            CHECK(order.size() == 2 && order[0] == 1);
            CHECK(decodeWaypoints(encodeWaypoints("world",w),"world") == w);
            auto legacy = encodeWaypoints("world", {w.data(),1});
            legacy.erase(legacy.end()-9); // Remove v2 visibility byte from a single entry.
            legacy[4] = 1; reseal(legacy);
            auto old = decodeWaypoints(legacy,"world");
            CHECK(old && old->front().visible);
            auto bad = encodeWaypoints("world", {w.data(),1});
            bad[bad.size()-9] = 2; reseal(bad);
            CHECK(!decodeWaypoints(bad,"world"));
        });
        test("Map-only bounded settings undo and typed color validation", [] {
            SettingsHistory history;
            Settings s;
            for (int i = 0; i < 40; ++i) { s.radius = 16+i; history.remember(s); }
            CHECK(history.size() == 16);
            CHECK(history.undo()->radius == 55);
            ColorCache colors;
            for (std::uint64_t i = 0; i < 10000; ++i) colors.store(i,0xff123456);
            CHECK(colors.size() == 4096);
            CHECK(colors.find(9999) == 0xff123456);
            CHECK(mapColor(1,0,.5f,1) == 0xff8000ff);
            CHECK(!mapColor(1,0,0,0));
            CHECK(!mapColor(-1,0,0,1));
            CHECK(!mapColor(std::numeric_limits<float>::quiet_NaN(),0,0,1));
            CHECK(elevation(69,64) == Elevation::Above);
            CHECK(elevation(59,64) == Elevation::Below);
            CHECK(elevation(68,64) == Elevation::Level);
        });
        test("death edge uses last owned alive position and exact session", [] {
            DeathTracker tracker;
            auto s = session();
            CHECK(!tracker.observe(s,1,64,2,false));
            CHECK(!tracker.observe(s,2,65,3,true));
            auto death = tracker.observe(s,100,10,100,false);
            CHECK(death && death->x == 2 && death->y == 65 && death->z == 3);
            CHECK(!tracker.observe(s,100,10,100,false));
            CHECK(!tracker.observe(s,3,64,4,true));
            CHECK(!tracker.observe(session("other",2),100,10,100,false));
        });
        test("host action settings are unbound bounded and independently persisted", [] {
            Settings s;
            for (int key : s.actionKeys) CHECK(key == 0);
            auto on = actionSettings(s,MapAction::ToggleMinimap);
            CHECK(on.enabled && on.minimap);
            CHECK(!actionSettings(on,MapAction::ToggleMinimap).minimap);
            s.zoom = 8; CHECK(actionSettings(s,MapAction::ZoomIn).zoom == 8);
            s.zoom = .25; CHECK(actionSettings(s,MapAction::ZoomOut).zoom == .25);
            CHECK(actionSettings(s,MapAction::ToggleCave).cave);
            s.actionKeys[0] = 77;
            CHECK(decodeSettings(encodeSettings(s))->actionKeys[0] == 77);
            s.actionKeys[0] = 256; CHECK(!s.valid());
        });
        test("two dense views share sample while retaining independent coverage", [] {
            Cache c; c.begin(session());
            CHECK(c.apply(c.session(),tileKey(0,0,0,Layer::Surface),0,0,sample(),1));
            std::array views{View{0,0,64},View{0,0,256}};
            auto cells = c.view(views);
            CHECK(cells.size() <= maxCells);
            auto cell = sampleAt(cells,{0,0},Layer::Surface);
            CHECK(cell && cell->viewMask == 3 && cell->strideFor(1) == 1 && cell->strideFor(2) == 1);
            CHECK(!sampleAt(cells,{4,4},Layer::Surface,0,0,1));
            CHECK(!sampleAt(cells,{4,4},Layer::Surface,0,0,2));
            views[0] = {-coordinateLimit,-coordinateLimit,256};
            Scanner scan; scan.request(c.session(),{views.data(),1});
            CHECK(scan.demandSize() == 513*513);
        });
        test("headless unchanged demand publication measurement", [] {
            Cache cache; cache.begin(session());
            std::array views{View{0,0,256},View{0,0,256}};
            Scanner scan; TerrainPublication publication;
            scan.request(cache.session(),views);
            auto terrain = publication.get(cache,views);
            std::vector<double> times;
            times.reserve(1000);
            for (int i = 0; i < 1000; ++i) {
                // Exact stationary demand: movement legitimately rebuilds dense art.
                auto start = std::chrono::steady_clock::now();
                scan.request(cache.session(),views);
                auto same = publication.get(cache,views);
                times.push_back(std::chrono::duration<double,std::micro>(
                    std::chrono::steady_clock::now()-start).count());
                CHECK(same == terrain);
            }
            CHECK(scan.builds() == 1 && publication.builds() == 1);
            std::sort(times.begin(),times.end());
            std::cout << "OFFLINE unchanged demand+publication CPU us p50=" << times[500]
                      << " p95=" << times[950] << " max=" << times.back()
                      << "; 1000 calls, one demand/terrain build; no game/runtime claim\n";
        });
        test("region codec exact world dimension layer band negative coordinates", [] {
            std::vector<Tile> tiles;
            for (int x = -4; x < 0; ++x) for (int z = -4; z < 0; ++z) {
                Tile tile;
                tile.key = {x,z,1,-60,-92,Layer::Cave};
                tile.samples[0] = sample();
                tiles.push_back(tile);
            }
            auto region = regionKey(tiles.front().key);
            CHECK(region.x == -1 && region.z == -1);
            auto bytes = encodeRegion("world",region,tiles);
            CHECK(!bytes.empty() && bytes.size() <= maxRegionFileBytes);
            CHECK(decodeRegion(bytes,"world",region)->size() == 16);
            auto tileBytes = encodeTiles("world",tiles);
            CHECK(!decodeTiles(tileBytes,"world",15));
            CHECK(decodeTiles(tileBytes,"world",16)->size() == 16);
            CHECK(!decodeTiles(tileBytes,"world",0));
            CHECK(!decodeRegion(bytes,"foreign",region));
            auto other = region; other.dimension = 0;
            CHECK(!decodeRegion(bytes,"world",other));
            other = region; other.caveTop -= 4; other.caveBottom -= 4;
            CHECK(!decodeRegion(bytes,"world",other));
            other = region; other.x = 0;
            CHECK(!decodeRegion(bytes,"world",other));
            tiles.push_back(tiles.front());
            CHECK(encodeRegion("world",region,tiles).empty());
            bytes.back() ^= 1;
            CHECK(!decodeRegion(bytes,"world",region));
        });
        test("rolling terrain migration copies into bounded region jobs preserves legacy", [] {
            Cache cache(128); auto s = session(); cache.begin(s);
            for (int x = -8; x < 8; ++x) for (int z = -4; z < 4; ++z)
                CHECK(cache.apply(s,tileKey(x*16,z*16,0,Layer::Surface),x*16,z*16,sample(),1));
            auto tiles = cache.exportTiles(128);
            auto legacy = encodeTiles(s.world,tiles);
            auto decoded = decodeTiles(legacy,s.world);
            auto jobs = regionMigrationJobs(s,*decoded);
            CHECK(jobs && jobs->size() == 8);
            auto root = std::filesystem::temp_directory_path() /
                ("PraxisMapRegionMigration-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto original = root / worldFilename(s.world) / "terrain-d0.ppm";
            std::filesystem::create_directories(original.parent_path());
            { std::ofstream file(original,std::ios::binary); file.write(reinterpret_cast<char const *>(legacy.data()),legacy.size()); }
            Persistence io(root);
            for (auto const &job : *jobs) {
                CHECK(std::get<std::vector<Tile>>(job.payload).size() <= 16);
                CHECK(io.enqueue(job));
                CHECK(io.queuedBytes() <= 8*1024*1024);
            }
            io.flush(); auto writes = io.take(s);
            CHECK(writes.size() == 8);
            for (auto const &r : writes) CHECK(r.okay && r.write);
            for (auto job : *jobs) {
                job.write = false; job.payload = {};
                CHECK(io.enqueue(std::move(job)));
            }
            io.flush(); auto reads = io.take(s);
            CHECK(reads.size() == 8);
            for (auto const &r : reads) CHECK(r.okay && std::get<std::vector<Tile>>(r.payload).size() == 16);
            std::ifstream file(original,std::ios::binary);
            Bytes retained(std::istreambuf_iterator<char>{file},{});
            CHECK(retained == legacy);
            CHECK(!regionMigrationJobs(session("",0),tiles));
            tiles.push_back(tiles.front()); CHECK(!regionMigrationJobs(s,tiles));
            io.stopAndJoin();
        });
        test("regional partial updates preserve samples and reject foreign corrupt evidence", [] {
            auto root = std::filesystem::temp_directory_path() /
                ("PraxisMapRegions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto s = session();
            Persistence io(root);
            Tile a; a.key = tileKey(-1,-1,0,Layer::Surface); a.samples[0] = sample();
            auto region = regionKey(a.key);
            CHECK(io.enqueue({s,FileKind::RegionTerrain,1,true,std::vector<Tile>{a},region}));
            io.flush(); CHECK(io.take(s).front().okay);
            Tile b; b.key = a.key; b.samples[1] = sample(); b.samples[1].height = 99;
            CHECK(io.enqueue({s,FileKind::RegionTerrain,2,true,std::vector<Tile>{b},region}));
            CHECK(io.enqueue({s,FileKind::RegionTerrain,3,true,std::vector<Tile>{a},region}));
            io.flush(); auto ack = io.take(s);
            CHECK(ack.size() == 2 && ack[0].okay && ack[1].okay); // Partial writes are FIFO, never replaced wholesale.
            CHECK(io.enqueue({s,FileKind::RegionTerrain,0,false,{},region}));
            io.flush(); auto data = io.take(s);
            auto tile = std::get<std::vector<Tile>>(data.front().payload).front();
            CHECK(tile.samples[0].known && tile.samples[1].height == 99);
            auto dir = root/worldFilename(s.world)/"regions-v1";
            auto path = std::filesystem::directory_iterator(dir)->path();
            auto foreign = encodeRegion("foreign",region,{&a,1});
            { std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write(reinterpret_cast<char const *>(foreign.data()),foreign.size()); }
            CHECK(io.enqueue({s,FileKind::RegionTerrain,4,true,std::vector<Tile>{a},region}));
            io.flush(); CHECK(!io.take(s).front().okay);
            { std::ifstream file(path,std::ios::binary); Bytes retained(std::istreambuf_iterator<char>{file},{}); CHECK(retained == foreign); }
            { std::ofstream file(path,std::ios::binary|std::ios::trunc); file << "corrupt"; }
            CHECK(io.enqueue({s,FileKind::RegionTerrain,5,true,std::vector<Tile>{a},region}));
            io.flush(); CHECK(!io.take(s).front().okay);
            { std::ifstream file(path,std::ios::binary); std::string retained(std::istreambuf_iterator<char>{file},{}); CHECK(retained == "corrupt"); }
            auto other = region; other.dimension = 1;
            CHECK(!io.enqueue({s,FileKind::RegionTerrain,0,false,{},other}));
            io.stopAndJoin();
            CHECK(!io.enqueue({s,FileKind::RegionTerrain,0,false,{},region}));
        });
        std::cout << "PraxisCompanionMapTests: " << passed << " tests, " << assertions << " checks PASS\n";
        return 0;
    } catch (std::exception const &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
