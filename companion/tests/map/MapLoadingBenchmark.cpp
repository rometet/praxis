#include "map/MapCore.h"
#include "map/TerrainBaker.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
using namespace praxis::companion::map;
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now()-start).count();
}
std::uint64_t hashCells(std::span<Cell const> cells) {
    std::uint64_t h=14695981039346656037ull;
    for(auto const& c:cells) {
        for(auto v:{std::uint32_t(c.x),std::uint32_t(c.z),c.sample.color,std::uint32_t(c.sample.height),std::uint32_t(c.viewMask),
            std::uint32_t(c.stride),std::uint32_t(c.key.x),std::uint32_t(c.key.z),std::uint32_t(c.key.dimension),
            std::uint32_t(c.key.layer),std::uint32_t(c.key.caveTop),std::uint32_t(c.key.caveBottom)}) {
            h^=v;h*=1099511628211ull;
        }
        for(auto b:c.sample.biome){h^=std::uint8_t(b);h*=1099511628211ull;}
    }
    return h;
}
int main() {
    Session s{"loading-fixture",1,0,false};
    for(int count:{32,4096,16641,65536}) {
        Cache cache(512);cache.begin(s);
        int side=count==65536?256:count==16641?129:64;
        for(int i=0;i<count;++i) {
            int x=i%side-side/2,z=i/side-side/2;
            Sample value;value.known=true;value.height=std::int16_t(64+(x+z)%7);
            value.color=0xff000000u|std::uint32_t((i*57)&0xffffff);std::strcpy(value.biome.data(),"plains");
            cache.apply(s,tileKey(x,z,0,Layer::Surface),x,z,value,1);
        }
        for(int radius:{64,256}) {
            std::array views{View{0,0,radius},View{64,-64,radius}};
            std::vector<double> timings;std::uint64_t hash{},imageHash{};std::size_t known{};
            for(int trial=0;trial<11;++trial) {
                auto start=Clock::now();auto cells=cache.view(views);auto images=rasterTerrain(views[0],cells,1);
                double elapsed=ms(start);hash=hashCells(cells);known=cells.size();
                imageHash=14695981039346656037ull;
                for(auto rgba:images->rgba){imageHash^=rgba;imageHash*=1099511628211ull;}
                if(trial)timings.push_back(elapsed);
            }
            std::sort(timings.begin(),timings.end());
            TerrainBaker baker;TerrainBake job{s,views,2,cache.revision(),cache.exportTiles(512)};
            auto start=Clock::now();baker.enqueue(std::move(job));std::shared_ptr<BakedTerrain const> result;
            while(!(result=baker.take(s,views[0]))&&ms(start)<5000)std::this_thread::yield();
            if(!result)return 1;
            std::cout<<"{\"samples\":"<<count<<",\"radius\":"<<radius<<",\"cells\":"<<known
                <<",\"hash\":"<<hash<<",\"image_hash\":"<<imageHash<<",\"view_raster_median_ms\":"<<timings[timings.size()/2]
                <<",\"bake_ms\":"<<result->milliseconds<<",\"bake_latency_ms\":"<<ms(start)<<"}\n";
        }
    }
    // Native-cost reservation model only: the engine's latency and loaded
    // chunks are not reproduced. Detect an accidental scan-budget/fill change.
    struct Flat : DataSource {
        std::optional<int> surfaceHeight(int,int) override {return 65;}
        std::optional<Voxel> voxel(int,int y,int) override {return Voxel{y>=65,0xff448866};}
        std::array<char,64> biome(int,int,int) override {std::array<char,64> out{};std::strcpy(out.data(),"plains");return out;}
        std::uint32_t queryCost(Query q) const noexcept override {return q==Query::Height?4:q==Query::Voxel?24:2;}
    } source;
    Cache cache;cache.begin(s);Scanner scanner;View v{0,0,64};scanner.request(s,{&v,1});Settings settings;
    std::uint64_t reads=0,columns=0;std::uint32_t maxReads=0,maxColumns=0;int ticks=0;
    auto start=Clock::now();
    for(int t=1;t<=2000;++t) {
        auto stats=scanner.tick(cache,source,settings,t);reads+=stats.reads;columns+=stats.columns;
        maxReads=std::max(maxReads,stats.reads);maxColumns=std::max(maxColumns,stats.columns);
        if(maxReads>unsigned(settings.readsPerTick)||maxColumns>unsigned(settings.columnsPerTick))return 2;
        if(t%20==0&&cache.view({&v,1}).size()==129*129){ticks=t;break;}
    }
    if(!ticks)return 3;
    std::cout<<"{\"scan_model_ticks\":"<<ticks<<",\"reads\":"<<reads<<",\"columns\":"<<columns
        <<",\"max_reads_per_tick\":"<<maxReads<<",\"max_columns_per_tick\":"<<maxColumns
        <<",\"scan_model_cpu_ms\":"<<ms(start)<<",\"game\":\"NOT_RUN\"}\n";
}
