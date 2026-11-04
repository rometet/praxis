#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace praxis::companion::map {
inline constexpr int tileSide = 16;
inline constexpr std::size_t maxWorldBytes = 512, maxWaypoints = 1024;
// Two full-resolution bounded 513x513 views, generated only on the value worker.
inline constexpr std::size_t maxCells = 2 * 513 * 513;
inline constexpr int coordinateLimit = 30000000;
inline int floorDiv(int value, int divisor) noexcept {
    // Divisors are positive tile/grid sizes. Invalid sizes fail closed.
    if (divisor <= 0)
        return 0;
    int const q = value / divisor, r = value % divisor;
    return q - (r < 0);
}
inline int floorMod(int value, int divisor) noexcept {
    if (divisor <= 0)
        return 0;
    int const r = value % divisor;
    return r < 0 ? r + divisor : r;
}
inline bool coordinate(double v) noexcept { return std::isfinite(v) && std::abs(v) <= coordinateLimit; }
struct Session {
    std::string world;
    std::uint64_t generation{};
    int dimension{};
    bool persistent{};
    bool operator==(Session const &) const = default;
    bool valid() const noexcept {
        return !world.empty() && world.size() <= maxWorldBytes && generation != 0 &&
               dimension >= -128 && dimension <= 128;
    }
};
enum class Layer : std::uint8_t { Surface, Cave };
struct TileKey {
    int x{}, z{}, dimension{}, caveTop{}, caveBottom{};
    Layer layer{};
    bool operator==(TileKey const &) const = default;
};
struct TileHash {
    std::size_t operator()(TileKey const &key) const noexcept;
};
inline TileKey tileKey(int x, int z, int dim, Layer layer, int top = 0, int bottom = 0) noexcept {
    return {floorDiv(x, tileSide),
            floorDiv(z, tileSide),
            dim,
            layer == Layer::Cave ? top : 0,
            layer == Layer::Cave ? bottom : 0,
            layer};
}
inline std::size_t cellIndex(int x, int z) noexcept {
    return std::size_t(floorMod(z, tileSide) * tileSide + floorMod(x, tileSide));
}
inline std::optional<std::uint32_t> packMapColor(float r,float g,float b,float a=1.0f) noexcept {
    // A no-color native sample must not become opaque black. Real black with
    // positive alpha remains valid; alpha is visibility, not terrain shading.
    if(!std::isfinite(r)||!std::isfinite(g)||!std::isfinite(b)||!std::isfinite(a)||a<=0)return {};
    auto byte=[](float v){return std::uint32_t(std::lround(std::clamp(v,0.0f,1.0f)*255));};
    return 0xff000000u|byte(r)|(byte(g)<<8)|(byte(b)<<16);
}
struct Sample {
    std::uint32_t color{};
    std::int16_t height{};
    bool known{};
    std::array<char, 64> biome{};
    bool operator==(Sample const &) const = default;
};
struct Cell {
    int x{}, z{}, stride{1};
    Sample sample;
    TileKey key{};
    std::uint8_t viewMask{3};
    std::array<int, 2> viewStrides{};
    int strideFor(std::uint8_t mask) const noexcept {
        if (mask == 1 && viewStrides[0]) return viewStrides[0];
        if (mask == 2 && viewStrides[1]) return viewStrides[1];
        return stride;
    }
};
struct Tile {
    TileKey key;
    std::array<Sample, tileSide * tileSide> samples{};
    std::array<std::uint64_t, tileSide * tileSide> sampledAt{};
    std::uint64_t revision{};
};
struct Point {
    double x{}, z{};
};
struct Transform {
    Point worldCenter, screenCenter;
    double pixelsPerBlock{1}, angle{};
    Point project(Point p) const noexcept;
    Point unproject(Point p) const noexcept;
    Point projectPixel(Point p, bool snap = true) const noexcept;
    Transform zoomAt(Point pointer, double newPixelsPerBlock) const noexcept;
};
inline double mapAngle(double yawDegrees, bool northLocked) noexcept {
    return northLocked ? 0 : -(yawDegrees + 180.0) * 3.14159265358979323846 / 180.0;
}
struct View {
    int x{}, z{}, radius{64};
    Layer layer{};
    int top{}, bottom{};
    bool operator==(View const &) const = default;
};
struct Waypoint {
    std::uint64_t id{};
    std::string name;
    double x{}, y{}, z{};
    int dimension{};
    std::uint32_t color{0xff55ccffu};
    bool visible{true};
    bool operator==(Waypoint const &) const = default;
};
enum class RadarCategory : std::uint8_t { Players, Hostile, Passive, Items, Other };
enum class Elevation : std::uint8_t { Below, Level, Above };
inline Elevation elevation(double y, double playerY) noexcept {
    return y > playerY + 4 ? Elevation::Above : y < playerY - 4 ? Elevation::Below : Elevation::Level;
}
struct RadarPoint {
    double x{}, y{}, z{};
    std::string name;
    std::uint32_t color{0xffeeee55u};
    RadarCategory category{RadarCategory::Players};
    bool operator==(RadarPoint const &) const = default;
};
struct TerrainImage {
    int x{},z{},radius{},side{};
    std::size_t known{};
    std::vector<std::uint32_t> rgba;
};
inline std::shared_ptr<TerrainImage const> rasterTerrain(View const& v,std::span<Cell const> cells, std::uint8_t viewMask = 3, bool caveShade = false) {
    if(v.radius<1||v.radius>256)return {};
    auto out=std::make_shared<TerrainImage>();out->x=v.x;out->z=v.z;out->radius=v.radius;out->side=2*v.radius+1;
    out->rgba.resize(std::size_t(out->side)*out->side);
    for(auto const& c:cells) {
        int x=c.x-v.x+v.radius,z=c.z-v.z+v.radius;
        if(x<0||z<0||x>=out->side||z>=out->side||!c.sample.known||!(c.viewMask&viewMask)
            ||c.key.layer!=v.layer||(v.layer==Layer::Cave&&(c.key.caveTop!=v.top||c.key.caveBottom!=v.bottom)))continue;
        auto index=std::size_t(z)*out->side+x;
        if(out->rgba[index]==0)++out->known;
        auto color=c.sample.color;
        if(caveShade&&v.layer==Layer::Cave) {
            double depth=std::clamp(double(v.top-c.sample.height)/std::max(1,v.top-v.bottom),0.,1.);
            double brightness=1-.3*depth;
            color=(color&0xff000000u)|std::uint32_t((color&255)*brightness)|
                (std::uint32_t(((color>>8)&255)*brightness)<<8)|(std::uint32_t(((color>>16)&255)*brightness)<<16);
        }
        out->rgba[index]=color;
    }
    return out;
}
struct Snapshot {
    Session session;
    std::uint64_t resourceEpoch{1};
    std::shared_ptr<std::vector<Cell> const> cells{std::make_shared<std::vector<Cell>>()};
    std::array<std::shared_ptr<TerrainImage const>, 2> terrainImages;
    std::vector<Waypoint> waypoints;
    std::vector<RadarPoint> radar;
    double playerX{}, playerY{}, playerZ{}, yaw{};
    std::uint64_t revision{};
    std::size_t cacheTiles{}, cacheBytes{};
    std::uint32_t reads{}, columns{}, radarVisited{};
    double scanMilliseconds{}, publicationMilliseconds{}, radarMilliseconds{};
    double renderMilliseconds{}, firstAppearanceMilliseconds{-1};
    std::string persistenceStatus;
    Layer layer{};
    int caveTop{}, caveBottom{};
    std::uint64_t demandBuilds{}, terrainPublications{};
    std::uint64_t waypointRevision{};
    bool waypointsEditable{};
};
enum class Anchor : std::uint8_t { TopRight, TopLeft, BottomRight, BottomLeft };
enum class MapAction : std::uint8_t { ToggleMinimap, ZoomIn, ZoomOut, ToggleCave, CenterOnPlayer, AddWaypoint };
inline constexpr std::size_t mapActionCount = 6;
struct Settings {
    bool enabled{}, circular{true}, northLocked{true}, radar{true}, cave{};
    int radius{64}, pixelSize{192}, columnsPerTick{128}, readsPerTick{1024}, maxTiles{512};
    double zoom{1}, scanMilliseconds{0.75};
    int caveDepth{32}, refreshTicks{100}, radarMilliseconds{250};
    bool minimap{true}, coordinates{true}, biomeText{true}, radarNames{}, distanceRadar{true},
        snapPixels{true}, waypointMarkers{true}, lastDeath{true}, caveShade{true};
    std::array<bool, 5> radarCategories{true, false, false, false, false};
    Anchor anchor{Anchor::TopRight};
    int offsetX{16}, offsetY{34};
    std::array<int, mapActionCount> actionKeys{};
    bool valid() const noexcept;
    bool operator==(Settings const &) const = default;
};
} // namespace praxis::companion::map
