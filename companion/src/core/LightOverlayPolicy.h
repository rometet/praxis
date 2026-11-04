#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace praxis::companion {
struct LightOverlayOptions {
    int range{16};
    int spawnThreshold{0};
    int scanBudget{64};
    int drawBudget{1280};
    bool showAllLevels{true};
    bool onlyTopSurface{true};
    bool ignoreSkyLight{};
    bool numericDisplay{true};
    bool operator==(LightOverlayOptions const&) const = default;
};

struct LightSample {
    int x{}, y{}, z{};
    std::uint8_t sky{}, block{};
    bool operator==(LightSample const&) const = default;
};

inline int effectiveLight(LightSample const& sample, LightOverlayOptions const& options) noexcept {
    return options.ignoreSkyLight ? sample.block : std::max(sample.sky, sample.block);
}
inline bool displayLight(LightSample const& sample, LightOverlayOptions const& options) noexcept {
    return options.showAllLevels || effectiveLight(sample, options) <= options.spawnThreshold;
}
inline bool acceptLightSurface(bool aboveAir, bool floorAir, bool floorSolid,
                               bool excludedFloor, LightOverlayOptions const& options) noexcept {
    return aboveAir && (!options.onlyTopSurface || (!floorAir && floorSolid && !excludedFloor));
}
inline bool lightInRange(LightSample const& sample, int x, int y, int z,
                         LightOverlayOptions const& options) noexcept {
    return std::abs(sample.x - x) <= options.range && std::abs(sample.y - y) <= 4
        && std::abs(sample.z - z) <= options.range;
}
inline constexpr std::array<int, 5> lightVerticalOffsets{-1, 0, -2, 1, -3};

// Preserve the legacy center-first ring order without generating an allocation
// for each refresh. The caller bounds its reads per frame.
inline bool lightColumnOffset(std::size_t column, int radius, int& dx, int& dz) noexcept {
    if (!column) { dx = dz = 0; return true; }
    std::size_t local = column - 1;
    for (int ring = 1; ring <= radius; ++ring) {
        auto const edge = static_cast<std::size_t>(ring * 2);
        if (local >= edge * 4) { local -= edge * 4; continue; }
        if (local < edge) { dx = -ring + static_cast<int>(local); dz = -ring; }
        else if ((local -= edge) < edge) { dx = ring; dz = -ring + static_cast<int>(local); }
        else if ((local -= edge) < edge) { dx = ring - static_cast<int>(local); dz = ring; }
        else { local -= edge; dx = -ring; dz = ring - static_cast<int>(local); }
        return true;
    }
    return false;
}
} // namespace praxis::companion
