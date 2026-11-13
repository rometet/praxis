#include "runtime/DeathLog.h"
#include "runtime/DeathLogCodec.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <span>
#include <thread>
namespace praxis::companion {
namespace {
struct Job {
    enum class Kind { Load, Save } kind{};
    map::Session session;
    std::uint64_t revision{};
    std::vector<map::DeathHistoryEntry> entries;
};
std::mutex mutex;
std::condition_variable ready;
std::deque<Job> jobs;
std::thread worker;
bool starting{};
bool running{};
bool stopping{};
std::filesystem::path root;
map::DeathHistory history;
map::Session active;
std::uint64_t revision{};
bool blocked{};
std::string status{"Not started"};
std::atomic<std::shared_ptr<DeathLogSnapshot const>> published{std::make_shared<DeathLogSnapshot>()};

std::filesystem::path logPath(map::Session const& session) {
    auto bytes = std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const*>(session.world.data()),
        session.world.size());
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << deathlog::fnv1a(bytes);
    return root / (name.str() + ".pdl");
}

void publishLocked() {
    auto next = std::make_shared<DeathLogSnapshot>();
    next->session = active;
    next->persistent = active.persistent;
    next->blocked = blocked;
    next->revision = revision;
    next->status = status;
    if (active.valid()) next->entries = history.entries(active);
    published.store(std::move(next), std::memory_order_release);
}

std::vector<map::DeathHistoryEntry> mergeEntries(std::vector<map::DeathHistoryEntry> first,
                                                 std::vector<map::DeathHistoryEntry> second) {
    first.insert(first.end(), second.begin(), second.end());
    std::stable_sort(first.begin(), first.end(), [](auto const& a, auto const& b) {
        return a.unixSeconds > b.unixSeconds;
    });
    std::vector<map::DeathHistoryEntry> result;
    result.reserve(std::min<std::size_t>(first.size(), map::DeathHistory::capacity));
    for (auto const& entry : first) {
        bool duplicate = false;
        for (auto const& existing : result)
            if (existing.unixSeconds == entry.unixSeconds && existing.dimension == entry.dimension
                && existing.x == entry.x && existing.y == entry.y && existing.z == entry.z) {
                duplicate = true;
                break;
            }
        if (duplicate) continue;
        result.push_back(entry);
        if (result.size() >= map::DeathHistory::capacity) break;
    }
    return result;
}

bool atomicWrite(std::filesystem::path const& path, deathlog::Bytes const& bytes, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "cannot create death log directory"; return false; }
    auto temporary = path;
    temporary += "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetTickCount64()) + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) { error = "cannot open temporary death log"; return false; }
        file.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) { error = "death log write failed"; return false; }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
        error = "death log atomic replacement failed";
        return false;
    }
    return true;
}

void run() noexcept {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [] { return stopping || !jobs.empty(); });
            if (jobs.empty()) {
                if (stopping) return;
                continue;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        std::string error;
        bool okay = false;
        bool missing = false;
        std::vector<map::DeathHistoryEntry> loaded;
        if (job.kind == Job::Kind::Load) {
            try {
                auto path = logPath(job.session);
                std::error_code ec;
                if (!std::filesystem::exists(path, ec)) {
                    okay = true;
                    missing = true;
                } else {
                    auto size = std::filesystem::file_size(path, ec);
                    if (ec || size < 14 || size > deathlog::maxFileBytes) {
                        error = "existing death log size rejected; original preserved";
                    } else {
                        deathlog::Bytes bytes(static_cast<std::size_t>(size));
                        std::ifstream input(path, std::ios::binary);
                        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                        auto decoded = input ? deathlog::decode(bytes, job.session.world) : std::nullopt;
                        if (decoded) {
                            okay = true;
                            loaded = std::move(*decoded);
                        } else {
                            error = "death log checksum/identity rejected; original preserved";
                        }
                    }
                }
            } catch (...) { error = "death log load failed"; }
        } else {
            try {
                auto bytes = deathlog::encode(job.session.world, job.entries);
                okay = !bytes.empty() && atomicWrite(logPath(job.session), bytes, error);
                if (bytes.empty()) error = "invalid owned death log payload";
            } catch (...) { error = "death log save failed"; }
        }
        {
            std::lock_guard lock(mutex);
            if (!running) continue;
            if (job.session != active) continue;
            if (job.kind == Job::Kind::Load) {
                if (okay) {
                    auto merged = mergeEntries(std::move(loaded), history.entries(active));
                    history.preload(active, std::move(merged), false);
                    status = missing ? "New world / no saved death log" : "Loaded";
                    blocked = false;
                } else {
                    blocked = true;
                    status = error;
                }
                ++revision;
            } else if (okay) {
                status = "Saved";
            } else {
                status = error;
            }
            publishLocked();
        }
    }
}

void queueLocked(Job job) {
    if (!running) return;
    if (job.kind == Job::Kind::Save) {
        // Coalesce only a pending write for the same session.
        for (auto& old : jobs)
            if (old.kind == Job::Kind::Save && old.session == job.session) {
                old = std::move(job);
                ready.notify_one();
                return;
            }
    }
    if (jobs.size() >= 8) return;
    jobs.push_back(std::move(job));
    ready.notify_one();
}
} // namespace

bool startDeathLog(std::filesystem::path ownedRoot) noexcept {
    std::lock_guard lock(mutex);
    if (running) return true;
    if (starting) return false;
    auto candidate = std::move(ownedRoot);
    if (candidate.empty()) return false;
    root = std::move(candidate);
    status = "Not started";
    stopping = false;
    starting = true;
    try {
        worker = std::thread(run);
    } catch (...) {
        starting = false;
        return false;
    }
    running = true;
    starting = false;
    publishLocked();
    return true;
}

bool stopDeathLog() noexcept {
    {
        std::lock_guard lock(mutex);
        if (!running) return true;
        stopping = true;
        running = false;
        active = {};
        history.clear();
        jobs.clear();
        blocked = false;
        status = "Not started";
        publishLocked();
    }
    ready.notify_all();
    bool joined = false;
    try {
        if (worker.joinable()) {
            worker.join();
            joined = true;
        }
    } catch (...) {}
    std::lock_guard lock(mutex);
    stopping = false;
    return joined;
}

void resetDeathLog() noexcept {
    std::lock_guard lock(mutex);
    active = {};
    history.clear();
    blocked = false;
    status = "Not started";
    ++revision;
    publishLocked();
}

bool recordDeath(map::Session const& session, map::DeathHistoryEntry entry) noexcept {
    try {
        std::lock_guard lock(mutex);
        if (!running || !session.valid()) return false;
        // The dedicated log is world-scoped; each entry carries its dimension.
        map::Session identity = session;
        identity.dimension = 0;
        if (identity != active) {
            active = identity;
            history.clear();
            blocked = false;
            status = identity.persistent ? "Loading" : "Session only";
            ++revision;
            if (identity.persistent) queueLocked({Job::Kind::Load, identity, 0, {}});
        }
        map::Waypoint death{1, "Death", entry.x, entry.y, entry.z, entry.dimension, 0xFF55CCFFu, true};
        auto existing = history.entries(active);
        for (auto const& prior : existing)
            if (prior.dimension == entry.dimension && prior.unixSeconds <= entry.unixSeconds
                && entry.unixSeconds - prior.unixSeconds <= 2
                && std::abs(prior.x - entry.x) < 0.01 && std::abs(prior.y - entry.y) < 0.01
                && std::abs(prior.z - entry.z) < 0.01)
                return false; // PlayerDieEvent and tick fallback describe one death.
        if (!history.recordAnyDimension(active, death, entry.unixSeconds)) return false;
        ++revision;
        auto entries = history.entries(active);
        if (active.persistent && !blocked) {
            status = "Saving";
            queueLocked({Job::Kind::Save, active, revision, std::move(entries)});
        } else {
            status = blocked ? "Persistence unavailable (existing file preserved)" : "Session only: stable world ID unavailable";
        }
        publishLocked();
        return true;
    } catch (...) {
        return false;
    }
}

std::shared_ptr<DeathLogSnapshot const> deathLogSnapshot() noexcept {
    return published.load(std::memory_order_acquire);
}

bool clearDeathLog() noexcept {
    try {
        std::lock_guard lock(mutex);
        if (!running) return false;
        history.clear();
        ++revision;
        if (active.valid() && active.persistent) {
            blocked = false;
            status = "Saving";
            queueLocked({Job::Kind::Save, active, revision, {}});
        } else {
            status = "Session only";
        }
        publishLocked();
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace praxis::companion
