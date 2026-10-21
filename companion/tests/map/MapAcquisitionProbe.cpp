// Real Scanner/Cache with the native adapter's reservation weights; no engine.
#include "map/MapCore.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
using namespace praxis::companion::map;
using Clock = std::chrono::steady_clock;
struct Fixture : DataSource {
    int tick{}, delayUs{}, depth{};
    bool pending{};
    Layer layer{Layer::Surface};
    std::uint64_t heights{}, voxels{}, biomes{}, newPixels{}, refreshes{};
    std::set<std::pair<int,int>> seen;
    int centreTick{}, outerTick{};
    void delay() {
        if (!delayUs) return;
        auto until=Clock::now()+std::chrono::microseconds(delayUs);
        while(Clock::now()<until)std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    std::optional<int> surfaceHeight(int,int) override {
        ++heights;delay();return pending?std::nullopt:std::optional<int>{65};
    }
    std::optional<SurfaceColumn> surfaceColumn(int x,int z) override {
        auto h=surfaceHeight(x,z);
        return h?std::optional<SurfaceColumn>{{64,-64,65}}:std::nullopt;
    }
    std::optional<Voxel> voxel(int,int y,int) override {
        ++voxels;delay();if(pending)return {};
        if(layer==Layer::Cave)return Voxel{y>76-depth,0xff448866u};
        if(y>=65)return Voxel{true,0};
        return depth&&y>=64-depth?Voxel{false,0xffaa7733u,true,true,true,true}:Voxel{false,0xff448866u};
    }
    std::array<char,64> biome(int,int,int) override {
        ++biomes;delay();std::array<char,64>b{};std::strcpy(b.data(),"plains");return b;
    }
    std::uint32_t queryCost(Query q) const noexcept override {
        return q==Query::Height?4:q==Query::Voxel?24:2;
    }
    void completed(int x,int z,Sample const&) override {
        if(seen.emplace(x,z).second)++newPixels;else ++refreshes;
        if(x==0&&z==0&&!centreTick)centreTick=tick;
        if(x==64&&z==64&&!outerTick)outerTick=tick;
    }
};
int main(int argc,char**argv) {
    std::string mode=argc>1?argv[1]:"stationary";
    int ticks=argc>2?std::atoi(argv[2]):1600;
    Fixture source;source.delayUs=argc>3?std::atoi(argv[3]):0;
    source.depth=mode=="water"?8:mode=="cave"?12:0;
    source.pending=mode=="unready";
    source.layer=mode=="cave"?Layer::Cave:Layer::Surface;
    Settings settings;Cache cache;Session session{"acquisition-fixture",1,0,false};cache.begin(session);
    Scanner scanner;
    std::uint64_t reads{},columns{},missing{},refresh{},skipped{},visited{},unavailable{},missingAttempts{},refreshAttempts{};
    std::array<std::uint64_t,5> stops{};std::vector<double> times;
    int fullTick{}, fullAfterStop{};std::size_t visible{},expected{};
    std::string checkpoints;
    for(int t=1;t<=ticks;++t) {
        source.tick=t;int x=0,z=0,radius=64;
        if(mode=="moving"||mode=="move-stop")x=(mode=="move-stop"?std::min(t,1600):t)/10;
        if(mode=="backforth")x=std::abs((t/10)%16-8);
        if(mode=="teleport")x=t<401?0:10000;
        if(mode=="zoom")radius=t<401?64:32;
        std::array views{View{x,z,radius,source.layer,76,44},View{-320+x,80,32,source.layer,76,44}};
        std::size_t count=mode=="two-view"?2:1;
        if(mode=="two-view")views[0].x=t/10;
        scanner.request(session,{views.data(),count});
        auto stats=scanner.tick(cache,source,settings,t);
        if(stats.reads>unsigned(settings.readsPerTick)||stats.columns>unsigned(settings.columnsPerTick)||stats.visited>scanner.demandSize())return 2;
        reads+=stats.reads;columns+=stats.columns;missing+=stats.missing;refresh+=stats.refreshed;
        skipped+=stats.skipped;visited+=stats.visited;unavailable+=stats.unavailable;
        missingAttempts+=stats.missingAttempts;refreshAttempts+=stats.refreshAttempts;
        stops[stats.stop]++;times.push_back(stats.milliseconds);
        if(t%20==0) {
            visible=cache.view({views.data(),count}).size();
            expected=std::size_t(2*radius+1)*(2*radius+1)+(count==2?65*65:0);
            if(visible==expected&&!fullTick)fullTick=t;
            if(mode=="move-stop"&&t>=1600&&visible==expected&&!fullAfterStop)fullAfterStop=t;
            if(t%200==0) {
                if(!checkpoints.empty())checkpoints+=",";
                checkpoints+="{\"tick\":"+std::to_string(t)+",\"visible\":"+std::to_string(visible)+",\"new_pixels\":"+std::to_string(source.newPixels)+"}";
            }
        }
    }
    std::sort(times.begin(),times.end());
    std::cout<<"{\"mode\":\""<<mode<<"\",\"ticks\":"<<ticks<<",\"query_delay_us\":"<<source.delayUs
        <<",\"reads\":"<<reads<<",\"columns\":"<<columns<<",\"heights\":"<<source.heights<<",\"voxels\":"<<source.voxels<<",\"biomes\":"<<source.biomes
        <<",\"new_pixels\":"<<source.newPixels<<",\"known_refreshes\":"<<source.refreshes<<",\"known_skips\":"<<skipped<<",\"visited\":"<<visited
        <<",\"missing_lane\":"<<missing<<",\"refresh_lane\":"<<refresh<<",\"missing_attempts\":"<<missingAttempts<<",\"refresh_attempts\":"<<refreshAttempts
        <<",\"unavailable\":"<<unavailable<<",\"visible_final\":"<<visible<<",\"expected_view\":"<<expected<<",\"full_tick\":"<<fullTick<<",\"full_after_stop_tick\":"<<fullAfterStop
        <<",\"centre_first_tick\":"<<source.centreTick<<",\"fixed_outer_first_tick\":"<<source.outerTick
        <<",\"read_stop_ticks\":"<<stops[ScanStats::Reads]<<",\"time_stop_ticks\":"<<stops[ScanStats::Time]
        <<",\"scan_ms_p50\":"<<times[times.size()/2]<<",\"scan_ms_p95\":"<<times[times.size()*95/100]<<",\"scan_ms_max\":"<<times.back()
        <<",\"checkpoint_step\":20,\"checkpoints\":["<<checkpoints<<"],\"native_time\":\"NOT_MEASURED\",\"game\":\"NOT_RUN\"}\n";
}
