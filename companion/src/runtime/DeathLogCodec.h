#pragma once
// Owned, checksummed persistence format for the dedicated death log. This is
// not a game-memory format: it stores only plain coordinates, dimension,
// epoch seconds and the exact world identity used by the map persistence layer.
#include "map/DeathHistory.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace praxis::companion::deathlog {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::uint32_t magic = 0x314C4450u; // "PDL1"
inline constexpr int formatVersion = 1;
inline constexpr std::size_t maxFileBytes = 16 * 1024;

inline std::uint64_t fnv1a(std::span<std::uint8_t const> data) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

inline Bytes encode(std::string const& world, std::span<map::DeathHistoryEntry const> entries) {
    if (world.empty() || world.size() > map::maxWorldBytes || entries.size() > map::DeathHistory::capacity)
        return {};
    Bytes out;
    auto u = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    };
    auto real = [&](double value) { u(std::bit_cast<std::uint64_t>(value), 8); };
    u(magic, 4);
    u(formatVersion, 2);
    u(world.size(), 2);
    for (char c : world) out.push_back(static_cast<std::uint8_t>(c));
    u(entries.size(), 1);
    for (auto const& e : entries) {
        if (e.dimension < -128 || e.dimension > 128 || !std::isfinite(e.x) || !std::isfinite(e.y)
            || !std::isfinite(e.z) || std::abs(e.x) > map::coordinateLimit || std::abs(e.z) > map::coordinateLimit
            || std::abs(e.y) > 2048.0)
            return {};
        u(e.unixSeconds, 8);
        u(static_cast<std::uint32_t>(static_cast<std::int32_t>(e.dimension)), 4);
        real(e.x); real(e.y); real(e.z);
    }
    auto const hash = fnv1a(out);
    u(hash, 8);
    return out;
}

inline std::optional<std::vector<map::DeathHistoryEntry>> decode(std::span<std::uint8_t const> data,
                                                                std::string const& expectedWorld) {
    if (data.size() < 8 + 4 + 2 + 2 + 1 || data.size() > maxFileBytes) return {};
    std::size_t tail = data.size() - 8;
    std::uint64_t expectedHash = 0;
    for (int i = 0; i < 8; ++i) expectedHash |= std::uint64_t(data[tail + i]) << (i * 8);
    if (fnv1a(data.first(tail)) != expectedHash) return {};
    std::size_t at = 0;
    auto u = [&](int bytes) -> std::uint64_t {
        if (at + static_cast<std::size_t>(bytes) > tail) { at = tail + 1; return 0; }
        std::uint64_t value = 0;
        for (int i = 0; i < bytes; ++i) value |= std::uint64_t(data[at++]) << (i * 8);
        return value;
    };
    if (u(4) != magic || u(2) != formatVersion) return {};
    auto const worldSize = u(2);
    if (worldSize == 0 || worldSize > map::maxWorldBytes || at + worldSize > tail) return {};
    std::string world(reinterpret_cast<char const*>(data.data() + at), worldSize);
    at += worldSize;
    if (world != expectedWorld) return {};
    auto const count = u(1);
    if (count > map::DeathHistory::capacity) return {};
    std::vector<map::DeathHistoryEntry> entries;
    entries.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        map::DeathHistoryEntry e;
        e.unixSeconds = u(8);
        e.dimension = static_cast<std::int32_t>(u(4));
        auto real = [&]() { return std::bit_cast<double>(static_cast<std::uint64_t>(u(8))); };
        e.x = real(); e.y = real(); e.z = real();
        if (at > tail || e.dimension < -128 || e.dimension > 128 || !std::isfinite(e.x)
            || !std::isfinite(e.y) || !std::isfinite(e.z) || std::abs(e.x) > map::coordinateLimit
            || std::abs(e.z) > map::coordinateLimit || std::abs(e.y) > 2048.0)
            return {};
        entries.push_back(e);
    }
    if (at != tail) return {};
    return entries;
}
} // namespace praxis::companion::deathlog
