#pragma once
#include "map/MapTypes.h"
#include <chrono>
#include <optional>
#include <span>
#include <unordered_map>

namespace praxis::companion::map {
class Cache {
  public:
    explicit Cache(std::size_t maxTiles = 512, std::size_t maxBytes = 24 * 1024 * 1024);
    void begin(Session const &);
    void setTileLimit(std::size_t);
    bool apply(Session const &, TileKey, int x, int z, Sample, std::uint64_t tick);
    std::optional<Sample> find(TileKey, int x, int z, std::uint64_t touch);
    bool fresh(TileKey, int x, int z, std::uint64_t tick, std::uint64_t ttl) const;
    bool known(TileKey, int x, int z) const noexcept;
    bool restore(Session const &, Tile const &);
    std::vector<Tile> exportTiles(std::size_t limit) const;
    std::vector<Cell> view(std::span<View const>, std::size_t limit = maxCells);
    std::size_t size() const noexcept { return entries_.size(); }
    std::size_t bytes() const noexcept;
    Session const &session() const noexcept { return session_; }
    std::uint64_t revision() const noexcept { return revision_; }
    // Changes only when a previously known cell might become unavailable.
    std::uint64_t knownStateRevision() const noexcept { return knownStateRevision_; }

  private:
    struct Entry {
        Tile tile;
        std::uint64_t use{};
    };
    std::unordered_map<TileKey, Entry, TileHash> entries_;
    Session session_;
    std::size_t maxTiles_, maxBytes_;
    std::uint64_t clock_{}, revision_{};
    std::uint64_t knownStateRevision_{};
    bool admit(TileKey);
};
struct Voxel {
    bool air{};
    std::uint32_t color{};
    bool cavePassable{}, water{}, mapVisible{true}, waterPassable{};
};
struct SurfaceColumn { int top{}, bottom{}; std::optional<int> overlay; };
// Implementations borrow runtime objects only for the duration of tick().
// Each method performs a bounded query group and cannot store borrowed data.
// queryCost reserves its worst-case native operation count before invoking it.
class DataSource {
  public:
    enum class Query { Height, Voxel, Biome };
    virtual ~DataSource() = default;
    virtual std::optional<int> surfaceHeight(int x, int z) = 0;
    virtual std::optional<SurfaceColumn> surfaceColumn(int x, int z) {
        auto top = surfaceHeight(x, z);
        return top ? std::optional<SurfaceColumn>{{*top, -2048, {}}} : std::nullopt;
    }
    virtual void completed(int, int, Sample const &) {}
    virtual std::optional<Voxel> voxel(int x, int y, int z) = 0;
    virtual std::array<char, 64> biome(int x, int y, int z) = 0;
    virtual std::uint32_t queryCost(Query) const noexcept { return 1; }
};
struct ScanStats {
    std::uint32_t reads{}, columns{}, skipped{};
    std::uint32_t unavailable{}, noColor{}, air{}, exhausted{}, black{};
    double milliseconds{};
    std::uint32_t missing{}, refreshed{}, visited{};
    std::uint32_t missingAttempts{}, refreshAttempts{};
    std::uint32_t prefixSkipped{}; // Logical skips needing no cache lookup.
    enum Stop { Idle, Columns, Reads, Time, Sweep } stop{Idle};
};
class Scanner {
  public:
    void request(Session const &, std::span<View const>);
    ScanStats tick(Cache &, DataSource &, Settings const &, std::uint64_t tick);
    void cancel() noexcept;
    std::size_t demandSize() const noexcept { return demand_; }
    std::uint64_t builds() const noexcept { return builds_; }
    bool samplingColumn() const noexcept { return demand_ && (current_.phase != 0||missingCurrent_.phase!=0); }

  private:
    struct Request {
        int x{}, z{}, y{}, top{}, bottom{};
        Layer layer{};
        std::uint8_t phase{};
        bool airSeen{};
        int verticalReads{};
        Sample sample;
        std::uint32_t waterColor{};
        int waterBottom{};
    };
    Session session_;
    // Dense rings are represented by two views and an index. Only the active
    // column needs sampling state; retargeting must not allocate 526k Requests
    // on the game's tick thread.
    std::array<View, 2> views_{};
    std::size_t viewCount_{}, demand_{};
    Request current_{};
    std::size_t cursor_{};
    Request missingCurrent_{};
    std::size_t missingCursor_{}, missingVisited_{};
    std::size_t verifiedPrefix_{};
    std::uint64_t knownStateRevision_{};
    std::uint64_t missingRetryAt_{}, schedulingColumn_{};
    std::uint64_t builds_{};
    bool missingActive_{}, useMissing_{}, missingResumed_{};
    Request requestAt(std::size_t) const noexcept;
    Request missingAt(std::size_t) const noexcept;
};
// Owned immutable terrain is rebuilt only when content or normalized demand changes.
class TerrainPublication {
  public:
    std::shared_ptr<std::vector<Cell> const> get(Cache &, std::span<View const>);
    void clear() noexcept;
    std::uint64_t builds() const noexcept { return builds_; }
  private:
    Session session_;
    std::uint64_t revision_{}, builds_{};
    std::vector<View> views_;
    std::shared_ptr<std::vector<Cell> const> cells_;
};
class CaveBand {
  public:
    std::pair<int, int> select(double y, int depth) noexcept;
    void reset() noexcept { top_ = 2048; }
  private:
    int top_{2048}, depth_{};
};
class SettingsHistory {
  public:
    void remember(Settings const &);
    std::optional<Settings> undo();
    std::size_t size() const noexcept { return history_.size(); }
  private:
    std::vector<Settings> history_;
};
class DeathTracker {
  public:
    std::optional<Waypoint> observe(Session const &, double x, double y, double z, bool alive);
    void clear() { session_ = {}; wasAlive_ = false; }
  private:
    Session session_;
    bool wasAlive_{};
    Waypoint last_;
};
Point minimapCenter(Settings const &, Point display) noexcept;
Cell const *sampleAt(std::span<Cell const>, Point, Layer, int top = 0, int bottom = 0,
                     std::uint8_t viewMask = 3) noexcept;
Settings actionSettings(Settings const &, MapAction) noexcept;
std::string readableBiome(std::string_view);
std::vector<std::size_t> orderedWaypoints(std::span<Waypoint const>, int dimension, Point origin,
                                        bool visibleOnly);
std::optional<std::uint32_t> mapColor(float r, float g, float b, float a) noexcept;
class ColorCache {
  public:
    std::optional<std::uint32_t> find(std::uint64_t) const;
    void store(std::uint64_t, std::uint32_t);
    void clear() { values_.clear(); }
    std::size_t size() const noexcept { return values_.size(); }
  private:
    std::unordered_map<std::uint64_t, std::uint32_t> values_;
};
class Waypoints {
  public:
    bool add(Waypoint);
    bool edit(Waypoint);
    bool remove(std::uint64_t);
    bool replace(std::vector<Waypoint>);
    std::vector<Waypoint> const &items() const noexcept { return items_; }
    std::uint64_t revision() const noexcept { return revision_; }
    void clear();

  private:
    std::vector<Waypoint> items_;
    std::uint64_t revision_{};
};
bool validWaypoint(Waypoint const &) noexcept;
// Only an explicitly local connection with an absolute storage path and stable
// level ID is eligible. Display name/seed and remote addresses are not identity.
std::optional<std::string> localWorldIdentity(std::string_view storagePath, std::string_view levelId,
                                              bool localConnection);
// Checksummed bounded formats use exact world identity and transactional decode.
using Bytes = std::vector<std::uint8_t>;
Bytes encodeWaypoints(std::string const &world, std::span<Waypoint const>);
std::optional<std::vector<Waypoint>> decodeWaypoints(std::span<std::uint8_t const>,
                                                     std::string const &expectedWorld);
Bytes encodeTiles(std::string const &world, std::span<Tile const>);
std::optional<std::vector<Tile>> decodeTiles(std::span<std::uint8_t const>, std::string const &expectedWorld,
                                            std::size_t tileLimit = 512, bool allowLegacy = false);
// 4x4 tiles per file; layer/band/dimension are part of its identity, not separate caches.
inline constexpr std::size_t maxRegionTiles = 16, maxRegionFileBytes = 512 * 1024;
TileKey regionKey(TileKey) noexcept;
bool validRegionKey(TileKey const &) noexcept;
std::vector<Tile> mergeRegion(std::span<Tile const> old, std::span<Tile const> incoming);
Bytes encodeRegion(std::string const &, TileKey region, std::span<Tile const>);
std::optional<std::vector<Tile>> decodeRegion(std::span<std::uint8_t const>, std::string const &, TileKey region);
std::string worldFilename(std::string const &world);
Bytes encodeSettings(Settings const &);
std::optional<Settings> decodeSettings(std::span<std::uint8_t const>);
} // namespace praxis::companion::map
