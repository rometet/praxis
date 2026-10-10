#pragma once
#include "map/MapCore.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace praxis::companion::map {
struct TerrainBake {
    Session session;
    std::array<View,2> views;
    std::size_t viewCount{};
    std::uint64_t revision{};
    std::vector<Tile> tiles;
    bool caveShade{};
    std::chrono::steady_clock::time_point enqueued{};
};
struct BakedTerrain {
    Session session;
    Layer layer{};
    int caveTop{},caveBottom{};
    std::uint64_t revision{};
    bool caveShade{};
    double milliseconds{};
    double queueMilliseconds{};
    std::chrono::steady_clock::time_point published{};
    std::shared_ptr<std::vector<Cell> const> cells;
    std::array<std::shared_ptr<TerrainImage const>,2> images;
};
// The worker receives copied tiles only. No BlockSource, actor, UI context,
// borrowed item, GPU device or engine callback is admitted to this queue.
class TerrainBaker {
public:
    TerrainBaker();
    ~TerrainBaker();
    TerrainBaker(TerrainBaker const&)=delete;
    void enqueue(TerrainBake);
    std::shared_ptr<BakedTerrain const> take(Session const&,View const&) const;
    void cancel();
private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<TerrainBake> pending_;
    std::shared_ptr<BakedTerrain const> result_;
    std::uint64_t epoch_{};
    bool stopping_{};
    std::thread worker_;
    void run();
};
}
