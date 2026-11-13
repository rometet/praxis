#pragma once
#include "map/DeathHistory.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace praxis::companion {
// Dedicated, bounded death log. Session-only worlds stay in memory; only a
// validated local world identity with safe owned I/O is persisted.
struct DeathLogSnapshot {
    map::Session session;
    bool persistent{};
    bool blocked{};
    std::uint64_t revision{};
    std::string status{"Not started"};
    std::vector<map::DeathHistoryEntry> entries;
};
bool startDeathLog(std::filesystem::path ownedRoot) noexcept;
bool stopDeathLog() noexcept;
void resetDeathLog() noexcept;
bool recordDeath(map::Session const&, map::DeathHistoryEntry) noexcept;
std::shared_ptr<DeathLogSnapshot const> deathLogSnapshot() noexcept;
bool clearDeathLog() noexcept;
} // namespace praxis::companion
