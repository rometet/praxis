#include "map/TerrainBaker.h"
#include "resourcepacks/WorldPackParticipant.h"
namespace praxis::companion::map {
TerrainBaker::TerrainBaker():worker_([this]{run();}){}
TerrainBaker::~TerrainBaker() {
    {std::lock_guard lock(mutex_);stopping_=true;pending_.reset();++epoch_;}
    condition_.notify_all();worker_.join();
}
void TerrainBaker::enqueue(TerrainBake job) {
    if(!packs::resourcePublicationCurrent(job.resourceEpoch))return;
    if(!job.session.valid()||job.viewCount<1||job.viewCount>2||job.tiles.size()>1024)return;
    job.enqueued=std::chrono::steady_clock::now();
    {std::lock_guard lock(mutex_);if(stopping_)return;pending_=std::move(job);}
    condition_.notify_one();
}
void TerrainBaker::cancel() {
    std::lock_guard lock(mutex_);pending_.reset();result_.reset();++epoch_;
}
std::shared_ptr<BakedTerrain const> TerrainBaker::take(Session const& s,View const& v,std::uint64_t resourceEpoch) const {
    std::lock_guard lock(mutex_);
    if(!result_||result_->resourceEpoch!=resourceEpoch||!packs::resourcePublicationCurrent(resourceEpoch)||result_->session!=s||result_->layer!=v.layer
       ||(v.layer==Layer::Cave&&(result_->caveTop!=v.top||result_->caveBottom!=v.bottom)))return {};
    return result_;
}
void TerrainBaker::run() {
    for(;;) {
        TerrainBake job;std::uint64_t epoch;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock,[&]{return stopping_||pending_.has_value();});
            if(stopping_)return;
            epoch=epoch_;job=std::move(*pending_);pending_.reset();
        }
        packs::WorldPackParticipant::AsyncLease resourceLease(packs::praxisWorldParticipant(),job.resourceEpoch);
        if(!resourceLease.admitted())continue;
        try {
            auto start=std::chrono::steady_clock::now();
            Cache values(1024);values.begin(job.session);
            for(auto const& tile:job.tiles)values.restore(job.session,tile);
            auto out=std::make_shared<BakedTerrain>();
            out->resourceEpoch=job.resourceEpoch;
            out->queueMilliseconds=std::chrono::duration<double,std::milli>(start-job.enqueued).count();
            out->session=job.session;out->layer=job.views[0].layer;
            out->caveTop=job.views[0].top;out->caveBottom=job.views[0].bottom;out->revision=job.revision;out->caveShade=job.caveShade;
            out->cells=std::make_shared<std::vector<Cell> const>(values.view({job.views.data(),job.viewCount}));
            for(std::size_t i=0;i<job.viewCount;++i)out->images[i]=rasterTerrain(job.views[i],*out->cells,std::uint8_t(1u<<i),job.caveShade);
            out->milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            out->published=std::chrono::steady_clock::now();
            std::lock_guard lock(mutex_);
            // cancel/retire may have occurred while this value-only bake ran.
            if(!stopping_&&epoch==epoch_&&resourceLease.current())result_=std::move(out);
        } catch(...) { /* Retain the previous complete value image on allocation failure. */ }
    }
}
}
