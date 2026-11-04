#include "map/MapPersistence.h"
#include <fstream>
#include <system_error>
#include <unordered_set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace praxis::companion::map {
namespace {
constexpr std::size_t byteLimit = 8 * 1024 * 1024;
std::size_t cost(Payload const &p) {
    if (auto v = std::get_if<std::vector<Tile>>(&p))
        return v->capacity() * sizeof(Tile);
    if (auto v = std::get_if<std::vector<Waypoint>>(&p)) {
        std::size_t bytes = v->capacity() * sizeof(Waypoint);
        for (auto const &w : *v)
            bytes += w.name.capacity();
        return bytes;
    }
    return sizeof(Settings);
}
std::size_t completionReservation(IoJob const &j) {
    // Reserve the completion before admitting work. Active load results and
    // write acknowledgements can never be silently evicted under saturation.
    if ((!j.write && j.kind == FileKind::Terrain))
        return 4 * 1024 * 1024;
    if (j.kind == FileKind::RegionTerrain)
        return maxRegionFileBytes;
    if (j.kind == FileKind::Waypoints)
        return maxWaypoints * (sizeof(Waypoint) + 128) + 2048;
    return 2048;
}
std::size_t jobCost(IoJob const &j) {
    return cost(j.payload) + j.session.world.capacity() + sizeof(IoJob) + completionReservation(j);
}
std::size_t resultCost(IoResult const &r) {
    return cost(r.payload) + r.session.world.capacity() + r.error.capacity() + sizeof(IoResult);
}
Bytes encode(IoJob const &j) {
    if (j.kind == FileKind::Waypoints && std::holds_alternative<std::vector<Waypoint>>(j.payload))
        return encodeWaypoints(j.session.world, std::get<std::vector<Waypoint>>(j.payload));
    if (j.kind == FileKind::Terrain && std::holds_alternative<std::vector<Tile>>(j.payload))
        return encodeTiles(j.session.world, std::get<std::vector<Tile>>(j.payload));
    if (j.kind == FileKind::Options && std::holds_alternative<Settings>(j.payload))
        return encodeSettings(std::get<Settings>(j.payload));
    if (j.kind == FileKind::RegionTerrain && std::holds_alternative<std::vector<Tile>>(j.payload))
        return encodeRegion(j.session.world, j.region, std::get<std::vector<Tile>>(j.payload));
    return {};
}
Payload decode(IoJob const &j, Bytes const &bytes) {
    if (j.kind == FileKind::Waypoints) {
        auto p = decodeWaypoints(bytes, j.session.world);
        if (p)
            return std::move(*p);
    }
    if (j.kind == FileKind::Terrain) {
        auto p = decodeTiles(bytes, j.session.world, 128);
        if (p)
            return std::move(*p);
    }
    if (j.kind == FileKind::Options) {
        auto p = decodeSettings(bytes);
        if (p)
            return *p;
    }
    if (j.kind == FileKind::RegionTerrain) {
        auto p = decodeRegion(bytes, j.session.world, j.region);
        if (p)
            return std::move(*p);
    }
    return {};
}
bool validExisting(std::filesystem::path const &path, IoJob const &job) {
    if (!std::filesystem::exists(path))
        return true;
    auto size = std::filesystem::file_size(path);
    auto max = job.kind == FileKind::RegionTerrain ? maxRegionFileBytes
               : job.kind == FileKind::Terrain     ? 12 * 1024 * 1024
               : job.kind == FileKind::Waypoints ? 1024 * 1024
                                                 : 256;
    if (size < 8 || size > std::uint64_t(max))
        return false;
    Bytes existing(std::size_t(size), 0);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(existing.data()), std::streamsize(existing.size()));
    return bool(input) && !std::holds_alternative<std::monostate>(decode(job, existing));
}
bool atomicWrite(std::filesystem::path const &path, Bytes const &bytes, std::string &error) {
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".nightly.tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot open temporary file";
            return false;
        }
        file.write(reinterpret_cast<char const *>(bytes.data()), std::streamsize(bytes.size()));
        file.flush();
        if (!file) {
            error = "temporary file write failed";
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "atomic replacement failed: " + std::to_string(GetLastError());
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
#endif
    return true;
}
bool preserveLegacyOptions(std::filesystem::path const& path,std::string& error) {
    if(!std::filesystem::exists(path))return true;
    Bytes original(std::size_t(std::filesystem::file_size(path)),0);
    std::ifstream in(path,std::ios::binary);in.read(reinterpret_cast<char*>(original.data()),original.size());
    if(!in){error="cannot read legacy options for backup";return false;}
    if(original.size()<6||original[5]!=0||original[4]>=3)return true;
    auto backup=path;
    backup+=original[4]==1?".v1-backup":(original.size()==67?".v2-short-backup":".v2-extended-backup");
    if(std::filesystem::exists(backup)) {
        if(std::filesystem::file_size(backup)!=original.size()) {error="existing legacy options backup differs; both originals preserved";return false;}
        Bytes previous(original.size());std::ifstream saved(backup,std::ios::binary);
        saved.read(reinterpret_cast<char*>(previous.data()),previous.size());
        if(!saved||previous!=original){error="existing legacy options backup differs; both originals preserved";return false;}
        return true;
    }
    std::error_code code;
    if(!std::filesystem::copy_file(path,backup,std::filesystem::copy_options::none,code)) {
        error="legacy options backup failed: "+code.message();return false;
    }
    return true;
}
} // namespace
Persistence::Persistence(std::filesystem::path root) : root_(std::move(root)), worker_([this] { run(); }) {}
Persistence::~Persistence() { stopAndJoin(); }
std::filesystem::path Persistence::path(IoJob const &j) const {
    if (j.kind == FileKind::Options)
        return root_ / "map-options.ppm";
    if (j.kind == FileKind::RegionTerrain) {
        auto const &r = j.region;
        auto name = "r" + std::to_string(r.x) + "_" + std::to_string(r.z) + "_d" +
                    std::to_string(r.dimension) + "_l" + std::to_string(int(r.layer)) + "_t" +
                    std::to_string(r.caveTop) + "_b" + std::to_string(r.caveBottom) + ".ppm";
        return root_ / worldFilename(j.session.world) / "regions-v1" / name;
    }
    return root_ / worldFilename(j.session.world) /
           (j.kind == FileKind::Waypoints ? "waypoints.ppm"
                                          : ("terrain-chiyan-v1-d" + std::to_string(j.session.dimension) + ".ppm"));
}
bool Persistence::enqueue(IoJob j) {
    if (j.kind != FileKind::Waypoints && j.kind != FileKind::Terrain && j.kind != FileKind::Options &&
        j.kind != FileKind::RegionTerrain)
        return false;
    if (j.kind == FileKind::Options &&
        (!j.session.world.empty() || j.session.generation || j.session.dimension || j.session.persistent))
        return false;
    if (j.kind != FileKind::Options && (!j.session.valid() || !j.session.persistent))
        return false;
    if (j.kind == FileKind::RegionTerrain) {
        if (!validRegionKey(j.region) || j.region.dimension != j.session.dimension)
            return false;
        if (j.write) {
            auto tiles = std::get_if<std::vector<Tile>>(&j.payload);
            if (!tiles || tiles->empty() || tiles->size() > maxRegionTiles)
                return false;
            for (auto const &tile : *tiles)
                if (regionKey(tile.key) != j.region)
                    return false;
        }
    }
    if (j.kind == FileKind::Waypoints && cost(j.payload) + 2048 > completionReservation(j))
        return false; // A failed write must still fit its reserved owned completion.
    auto size = jobCost(j);
    if (size > byteLimit)
        return false;
    std::lock_guard lock(mutex_);
    if (stopping_)
        return false;
    // Coalesce only pending writes; never replace another world's data or a load.
    for (auto &old : jobs_)
        if (j.kind != FileKind::RegionTerrain && j.write && old.write && old.kind == j.kind && old.session == j.session) {
            if (old.revision > j.revision)
                return false;
            auto oldSize = jobCost(old);
            if (bytes_ - oldSize + resultBytes_ + size > byteLimit)
                return false;
            bytes_ = bytes_ - oldSize + size;
            old = std::move(j);
            ready_.notify_one();
            return true;
        }
    if (jobs_.size() + results_.size() + inFlight_ >= 32 || bytes_ + resultBytes_ + size > byteLimit)
        return false;
    bytes_ += size;
    jobs_.push_back(std::move(j));
    ready_.notify_one();
    return true;
}
void Persistence::run() noexcept {
    for (;;) {
        IoJob job;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                if (stopping_)
                    return;
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++inFlight_;
        }
        IoResult result{job.session, job.kind, job.revision, job.write, false, false, false, {}, {}, job.region};
        auto const admittedCost = jobCost(job);
        try {
            auto filePath = path(job);
            if (job.write) {
                if (job.kind == FileKind::RegionTerrain && std::filesystem::exists(filePath)) {
                    auto size = std::filesystem::file_size(filePath);
                    if (size < 8 || size > maxRegionFileBytes)
                        result.error = "existing region size rejected; original preserved";
                    else {
                        Bytes bytes(std::size_t(size), 0);
                        std::ifstream input(filePath, std::ios::binary);
                        input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
                        auto old = input ? decodeRegion(bytes, job.session.world, job.region) : std::nullopt;
                        if (!old)
                            result.error = "existing region identity/checksum rejected; original preserved";
                        else
                            job.payload = mergeRegion(*old, std::get<std::vector<Tile>>(job.payload));
                    }
                }
                auto bytes = encode(job);
                // Preserve corrupted/foreign evidence. Never overwrite a file
                // whose existing world header, checksum or format is invalid.
                if (!result.error.empty()) {
                    // Region validation above also retains all foreign/corrupt evidence.
                } else if (job.kind != FileKind::RegionTerrain && !validExisting(filePath, job))
                    result.error = "existing persistence file rejected; original preserved";
                else if (bytes.empty())
                    result.error = "invalid owned persistence payload";
                else if(job.kind==FileKind::Options&&!preserveLegacyOptions(filePath,result.error)) {}
                else
                    result.okay = atomicWrite(filePath, bytes, result.error);
            } else if (!std::filesystem::exists(filePath)) {
                result.okay = true;
                result.missing = true;
            } else {
                auto size = std::filesystem::file_size(filePath);
                auto max = job.kind == FileKind::RegionTerrain ? maxRegionFileBytes
                           : job.kind == FileKind::Terrain     ? 12 * 1024 * 1024
                           : job.kind == FileKind::Waypoints ? 1024 * 1024
                                                             : 256;
                if (size > std::uint64_t(max) || size < 8)
                    result.error = "persistence size rejected";
                else {
                    Bytes bytes(std::size_t(size), 0);
                    std::ifstream input(filePath, std::ios::binary);
                    input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
                    if (!input)
                        result.error = "persistence read failed";
                    else {
                        result.payload = decode(job, bytes);
                        result.okay = !std::holds_alternative<std::monostate>(result.payload);
                        result.optionsMigrated=result.okay&&job.kind==FileKind::Options&&bytes.size()>6&&bytes[4]<3;
                        if (!result.okay)
                            result.error = "persistence checksum, format or identity rejected";
                    }
                }
            }
        } catch (std::exception const &e) {
            result.error = e.what();
        } catch (...) {
            result.error = "persistence worker error";
        }
        if (job.write && !result.okay && job.kind == FileKind::Waypoints) {
            try {
                // Keep the rejected/corrupt original and save owned edits in a
                // fixed sidecar. Never grow one recovery file per failure.
                auto bytes = encode(job);
                auto recovery = path(job);
                recovery += ".recovery.ppm";
                std::string error;
                result.recovered =
                    !bytes.empty() && validExisting(recovery, job) && atomicWrite(recovery, bytes, error);
                result.error +=
                    result.recovered ? "; edits saved to .recovery.ppm" : "; recovery failed: " + error;
            } catch (...) {
                result.error += "; recovery failed";
            }
            if (!result.recovered) {
                try {
                    auto emergency = std::filesystem::temp_directory_path() / "PraxisMapRecovery" /
                                     worldFilename(job.session.world) / "waypoints.ppm";
                    auto bytes = encode(job);
                    std::string error;
                    result.recovered = !bytes.empty() && validExisting(emergency, job) &&
                                       atomicWrite(emergency, bytes, error);
                    if (result.recovered)
                        result.error += "; edits saved to " + emergency.generic_string();
                } catch (...) {
                }
            }
            if (!result.recovered)
                result.payload = std::move(job.payload); // Caller owns unsaved edits after retirement too.
        }
        {
            std::lock_guard lock(mutex_);
            bytes_ -= admittedCost;
            --inFlight_;
            if (result.error.size() > 512)
                result.error = result.error.substr(0, 512);
            if (resultCost(result) > completionReservation(job)) {
                result.payload = {};
                result.okay = false;
                result.error = "decoded data exceeds completion budget; original preserved";
            }
            // All space/slots were reserved at enqueue; no result-drop path.
            resultBytes_ += resultCost(result);
            results_.push_back(std::move(result));
        }
        ready_.notify_all();
    }
}
std::vector<IoResult> Persistence::take(Session const &active) {
    std::lock_guard lock(mutex_);
    std::vector<IoResult> out;
    while (!results_.empty()) {
        auto const &old = results_.front();
        resultBytes_ -= resultCost(old);
        auto r = std::move(results_.front());
        results_.pop_front();
        if (r.kind == FileKind::Options || r.session == active || r.write)
            out.push_back(std::move(r));
        else
            ++stale_;
    }
    return out;
}
void Persistence::stopAndJoin() noexcept {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void Persistence::flush() {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [&] { return bytes_ == 0; });
}
std::size_t Persistence::queuedBytes() const {
    std::lock_guard lock(mutex_);
    return bytes_ + resultBytes_;
}
std::uint64_t Persistence::staleRejected() const {
    std::lock_guard lock(mutex_);
    return stale_;
}
std::optional<std::vector<IoJob>> regionMigrationJobs(Session const &session, std::span<Tile const> tiles) {
    if (!session.valid() || !session.persistent || tiles.size() > 128)
        return {};
    std::vector<IoJob> result;
    result.reserve(tiles.size());
    std::unordered_set<TileKey, TileHash> unique;
    for (auto const &tile : tiles) {
        auto region = regionKey(tile.key);
        if (tile.key.dimension != session.dimension || !unique.insert(tile.key).second ||
            encodeRegion(session.world, region, {&tile,1}).empty())
            return {};
        auto found = std::find_if(result.begin(),result.end(),[&](auto const &job) { return job.region == region; });
        if (found == result.end())
            result.push_back({session,FileKind::RegionTerrain,1,true,std::vector<Tile>{tile},region});
        else
            std::get<std::vector<Tile>>(found->payload).push_back(tile);
    }
    return result;
}
} // namespace praxis::companion::map
