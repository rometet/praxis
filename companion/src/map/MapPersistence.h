#pragma once
#include "map/MapCore.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <variant>

namespace praxis::companion::map {
enum class FileKind { Waypoints, Terrain, Options, RegionTerrain };
using Payload = std::variant<std::monostate, std::vector<Waypoint>, std::vector<Tile>, Settings>;
struct IoJob {
    Session session;
    FileKind kind{};
    std::uint64_t revision{};
    bool write{};
    Payload payload;
    TileKey region{};
};
struct IoResult {
    Session session;
    FileKind kind{};
    std::uint64_t revision{};
    bool write{}, okay{}, missing{}, recovered{};
    Payload payload;
    std::string error;
    TileKey region{};
    bool optionsMigrated{};
};
// Storage foundation only: caller drains these bounded jobs; legacy file is read-only.
std::optional<std::vector<IoJob>> regionMigrationJobs(Session const &, std::span<Tile const>);
class Persistence {
  public:
    explicit Persistence(std::filesystem::path root);
    ~Persistence();
    Persistence(Persistence const &) = delete;
    Persistence &operator=(Persistence const &) = delete;
    bool enqueue(IoJob);
    std::vector<IoResult> take(Session const &active);
    void stopAndJoin() noexcept;
    void flush(); // Shutdown-only, after callback admission has stopped.
    std::size_t queuedBytes() const;
    std::uint64_t staleRejected() const;

  private:
    void run() noexcept;
    std::filesystem::path path(IoJob const &) const;
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<IoJob> jobs_;
    std::deque<IoResult> results_;
    std::size_t bytes_{};
    std::size_t resultBytes_{};
    std::size_t inFlight_{};
    bool stopping_{};
    std::uint64_t stale_{};
    std::thread worker_;
};
} // namespace praxis::companion::map
