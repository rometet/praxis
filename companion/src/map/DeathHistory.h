#pragma once
// Session-only death history, independently implemented for Praxis.
// No Tsukuyomi source code or native game pointers are retained here.
#include "map/MapTypes.h"
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace praxis::companion::map {
struct DeathHistoryEntry {
    std::uint64_t unixSeconds{};
    int dimension{};
    double x{}, y{}, z{};
};
class DeathHistory {
public:
    static constexpr std::size_t capacity = 64;
    bool record(Session const& session, Waypoint const& death, std::uint64_t unixSeconds) {
        if (!session.valid() || death.dimension != session.dimension ||
            !coordinate(death.x) || !coordinate(death.y) || !coordinate(death.z))
            return false;
        return recordAnyDimension(session, death, unixSeconds);
    }
    // World-scoped logs may contain deaths from several dimensions. The
    // session remains the identity; each entry carries its own dimension.
    bool recordAnyDimension(Session const& session, Waypoint const& death, std::uint64_t unixSeconds) {
        if (!session.valid() || death.dimension < -128 || death.dimension > 128 ||
            !coordinate(death.x) || !coordinate(death.y) || !coordinate(death.z))
            return false;
        if (session != session_) {
            session_ = session;
            entries_.clear();
        }
        entries_.push_front({unixSeconds, death.dimension, death.x, death.y, death.z});
        if (entries_.size() > capacity) entries_.pop_back();
        return true;
    }
    // Replace the session history from owned persistence data. Invalid entries
    // are rejected individually; a foreign session is never adopted.
    bool preload(Session const& session, std::vector<DeathHistoryEntry> entries,
                 bool requireDimension = true) {
        if (!session.valid()) return false;
        session_ = session;
        entries_.clear();
        for (auto& entry : entries) {
            if (entry.dimension < -128 || entry.dimension > 128
                || (requireDimension && entry.dimension != session.dimension)
                || !coordinate(entry.x) || !coordinate(entry.y) || !coordinate(entry.z))
                continue;
            entries_.push_back(entry);
            if (entries_.size() >= capacity) break;
        }
        return true;
    }
    std::vector<DeathHistoryEntry> entries(Session const& session) const {
        if (session != session_) return {};
        return {entries_.begin(), entries_.end()};
    }
    void clear() noexcept {
        session_ = {};
        entries_.clear();
    }
private:
    Session session_{};
    std::deque<DeathHistoryEntry> entries_;
};
} // namespace praxis::companion::map
