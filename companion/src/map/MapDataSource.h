#pragma once
#include "map/MapCore.h"
#include "map/chiyan/TerrainPalette.h"
#include "map/TickValueCache.h"
class LocalPlayer;
class BlockSource;
namespace praxis::companion::map {
// Stack-only adapter. It is constructed/destroyed in the existing player tick.
class NativeDataSource final : public DataSource {
  public:
    explicit NativeDataSource(BlockSource &source, bool trace = false,int centerX=0,int centerZ=0) : source_(source), trace_(trace),centerX_(centerX),centerZ_(centerZ) {}
    std::optional<int> surfaceHeight(int, int) override;
    std::optional<SurfaceColumn> surfaceColumn(int, int) override;
    std::uint32_t queryCost(Query q) const noexcept override {
        return q == Query::Height ? 4 : q == Query::Voxel ? 24 : 2;
    }
    std::optional<Voxel> voxel(int, int, int) override;
    std::array<char, 64> biome(int, int, int) override;
    void completed(int,int,Sample const&) override;
    void logTrace() const noexcept;
    std::uint32_t biomeQueries() const noexcept {return biomeValues_.queries();}
    std::uint32_t biomeCacheHits() const noexcept {return biomeValues_.hits();}
    void setTraceViews(std::span<View const> views) noexcept {
        traceViewCount_=std::min(views.size(),traceViews_.size());
        std::copy_n(views.begin(),traceViewCount_,traceViews_.begin());
    }

  private:
    BlockSource &source_;
    bool trace_{};
    unsigned tracedHeight_{}, tracedColor_{},tracedFinal_{};
    int centerX_{},centerZ_{};
    std::array<View,2> traceViews_{};
    std::size_t traceViewCount_{};
    std::array<std::array<char,512>,7> traceMessages_{};
    struct BiomeValue {bool valid{};std::array<char,64> name{};chiyan::Tints tints;};
    TickValueCache<BiomeValue> biomeValues_;
    BiomeValue const& cachedBiome(int,int,int);
};
Session captureSession(LocalPlayer &, std::uint64_t generation);
std::vector<RadarPoint> captureRadar(LocalPlayer &, int radius, bool names, std::uint32_t &visited);
} // namespace praxis::companion::map
