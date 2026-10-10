// Offline freshness accounting: actual Scanner/Cache, no Minecraft objects.
#include "map/MapCore.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
using namespace praxis::companion::map;
using Coord=std::pair<int,int>;
struct Change {
    int x{},z{},version{},start{},group{},observed{},lastVisible{};
    bool knownAtChange{},superseded{};
    int maxVisibleAge{};
};
std::uint32_t color(int v){return 0xff000000u|std::uint32_t((v+1)*0x010203);}
int height(int v){return 64+3*(v%2);}
Sample value(int v){Sample s;s.known=true;s.color=color(v);s.height=height(v);std::strcpy(s.biome.data(),"plains");return s;}
struct Fixture:DataSource {
    int tick{};Cache& cache;
    std::map<Coord,int> current;
    std::map<Coord,Sample> sampled;
    std::vector<Change> events;
    std::array<std::vector<int>,3> visibleStaleAges;
    std::uint64_t heights{},voxels{},biomes{},acquired{},same{},changed{},correct{};
    std::uint64_t trace=14695981039346656037ull;
    std::set<Coord> repeatCells,changedCells;
    explicit Fixture(Cache& c):cache(c){}
    int version(int x,int z)const{auto i=current.find({x,z});return i==current.end()?0:events[i->second].version;}
    void stamp(int type,int x,int y,int z){
        for(unsigned v:{unsigned(type),unsigned(x),unsigned(y),unsigned(z),unsigned(tick)})
            for(int byte=0;byte<4;++byte){trace^=(v>>(8*byte))&255;trace*=1099511628211ull;}
    }
    std::optional<int> surfaceHeight(int x,int z)override{++heights;stamp(1,x,0,z);return height(version(x,z))+1;}
    std::optional<SurfaceColumn> surfaceColumn(int x,int z)override{auto h=surfaceHeight(x,z);return SurfaceColumn{*h-1,-64,*h};}
    std::optional<Voxel> voxel(int x,int y,int z)override{++voxels;stamp(2,x,y,z);int v=version(x,z);return Voxel{y>height(v),color(v)};}
    std::array<char,64> biome(int x,int y,int z)override{++biomes;stamp(3,x,y,z);return value(0).biome;}
    std::uint32_t queryCost(Query q)const noexcept override{return q==Query::Height?4:q==Query::Voxel?24:2;}
    void change(int centre,int heading,int v){
        for(int group=0;group<3;++group)for(int dz=-2;dz<=2;++dz)for(int dx=-2;dx<=2;++dx){
            int x=centre+dx-(group==2?heading*56:0),z=dz+(group==1?56:0);
            auto c=Coord{x,z};auto old=current.find(c);
            if(old!=current.end())events[old->second].superseded=true;
            current[c]=int(events.size());
            events.push_back({x,z,v,tick,group,0,0,cache.known(tileKey(x,z,0,Layer::Surface),x,z)});
        }
    }
    void completed(int x,int z,Sample const& s)override{
        auto c=Coord{x,z};auto i=sampled.find(c);
        if(i==sampled.end())++acquired;
        else{repeatCells.insert(c);if(i->second==s)++same;else{++changed;changedCells.insert(c);}}
        sampled[c]=s;
        if(auto e=current.find(c);e!=current.end()){
            auto& event=events[e->second];
            if(!event.observed&&s==value(event.version)){event.observed=tick;++correct;}
        }
    }
    void observe(int centre){
        for(auto const& [coord,index]:current){
            auto& e=events[index];
            if(std::abs(e.x-centre)>64||std::abs(e.z)>64)continue;
            e.lastVisible=tick;
            if(e.knownAtChange&&!e.observed){int age=tick-e.start;e.maxVisibleAge=std::max(e.maxVisibleAge,age);visibleStaleAges[e.group].push_back(age);}
        }
    }
};
int percentile(std::vector<int> v,double q){if(v.empty())return -1;std::sort(v.begin(),v.end());return v[std::min(v.size()-1,std::size_t(q*v.size()))];}
int main(int argc,char**argv){
    std::string movement=argc>1?argv[1]:"moving",warm=argc>2?argv[2]:"visible";
    int ticks=argc>3?std::atoi(argv[3]):1600;
    bool changes=argc>4?std::atoi(argv[4])!=0:true;
    Cache cache;Session session{"freshness-fixture",1,0,false};cache.begin(session);Fixture source(cache);
    if(warm!="cold"){
        int left=warm=="corridor"?-128:-64,right=warm=="corridor"?384:64;
        for(int z=-64;z<=64;++z)for(int x=left;x<=right;++x){
            auto s=value(0);cache.apply(session,tileKey(x,z,0,Layer::Surface),x,z,s,0);source.sampled[{x,z}]=s;
        }
    }
    Scanner scanner;Settings settings;
    std::uint64_t reads{},columns{},skips{},visited{},missing{},refresh{},prefixSkipped{};unsigned timeStops{};
    std::vector<double> ms;
    auto centre=[&](int t){return movement=="oscillating"?std::abs((t/10)%16-8):t/10;};
    for(int t=1;t<=ticks;++t){
        source.tick=t;int x=centre(t);
        if(changes&&(t==200||t==600||t==1000))source.change(x,x>=centre(t-10)?1:-1,1+(t-200)/400);
        View view{x,0,64};scanner.request(session,{&view,1});
        auto s=scanner.tick(cache,source,settings,1000+t);
        if(s.reads>1024||s.columns>128||s.visited>scanner.demandSize())return 2;
        reads+=s.reads;columns+=s.columns;skips+=s.skipped;visited+=s.visited;missing+=s.missing;refresh+=s.refreshed;
#ifdef PRAXIS_KNOWN_PREFIX_PROBE
        prefixSkipped+=s.prefixSkipped;
#endif
        timeStops+=s.stop==ScanStats::Time;ms.push_back(s.milliseconds);source.observe(x);
    }
    std::sort(ms.begin(),ms.end());View end{centre(ticks),0,64};
    std::cout<<"{\"movement\":\""<<movement<<"\",\"warm\":\""<<warm<<"\",\"ticks\":"<<ticks<<",\"changes\":"<<changes
        <<",\"reads\":"<<reads<<",\"columns\":"<<columns<<",\"known_skips\":"<<skips<<",\"visited\":"<<visited
        <<",\"prefix_skipped\":"<<prefixSkipped
        <<",\"query_trace\":\""<<source.trace<<"\""
        <<",\"missing_lane\":"<<missing<<",\"refresh_lane\":"<<refresh<<",\"new_acquisitions\":"<<source.acquired
        <<",\"repeat_same\":"<<source.same<<",\"repeat_changed\":"<<source.changed<<",\"repeat_distinct_cells\":"<<source.repeatCells.size()
        <<",\"changed_distinct_cells\":"<<source.changedCells.size()<<",\"correct_change_events\":"<<source.correct
        <<",\"heights\":"<<source.heights<<",\"voxels\":"<<source.voxels<<",\"biomes\":"<<source.biomes
        <<",\"visible_final\":"<<cache.view({&end,1}).size()<<",\"time_stop_ticks\":"<<timeStops
        <<",\"scan_ms_p95\":"<<ms[ms.size()*95/100]<<",\"groups\":[";
    for(int g=0;g<3;++g){
        std::vector<int> delays;int known=0,unseen=0,maxAge=0,supersededUnseen=0;
        for(auto const&e:source.events)if(e.group==g&&e.knownAtChange){++known;if(e.observed)delays.push_back(e.observed-e.start);else{++unseen;supersededUnseen+=e.superseded;}maxAge=std::max(maxAge,e.maxVisibleAge);}
        if(g)std::cout<<",";
        std::cout<<"{\"group\":\""<<(g==0?"near":g==1?"side_edge":"behind")<<"\",\"known_changes\":"<<known<<",\"observed\":"<<delays.size()
            <<",\"unseen\":"<<unseen<<",\"superseded_unseen\":"<<supersededUnseen<<",\"update_age_p95\":"<<percentile(delays,.95)<<",\"update_age_max\":"<<percentile(delays,1)
            <<",\"visible_stale_age_p95\":"<<percentile(source.visibleStaleAges[g],.95)<<",\"visible_stale_age_max\":"<<maxAge<<"}";
    }
    std::cout<<"],\"events\":[";
    for(std::size_t i=0;i<source.events.size();++i){auto const&e=source.events[i];if(i)std::cout<<",";
        std::cout<<"{\"x\":"<<e.x<<",\"z\":"<<e.z<<",\"version\":"<<e.version<<",\"group\":"<<e.group<<",\"start\":"<<e.start
            <<",\"known\":"<<e.knownAtChange<<",\"observed\":"<<e.observed<<",\"last_visible\":"<<e.lastVisible<<",\"max_visible_age\":"<<e.maxVisibleAge<<",\"superseded\":"<<e.superseded<<"}";}
    std::cout<<"],\"native_time\":\"NOT_MEASURED\",\"game\":\"NOT_RUN\"}\n";
}
