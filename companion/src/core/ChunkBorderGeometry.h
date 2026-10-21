#pragma once

#include "core/RenderPolicy.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace praxis::companion {

struct ChunkBorderOptions {
    bool enabled{};
    int renderDistance{3};
    int heightAbove{24};
    int heightBelow{8};
    std::uint32_t chunkColor{0x9900FFFF};
    std::uint32_t regionColor{0xCC0000FF};
    float lineWidth{1.5f};
    int lineBudget{2048};
    bool showRegions{true};
    bool showYCoordinates{};
    bool showHud{true};
    bool operator==(ChunkBorderOptions const&) const = default;
};

struct BorderLine {
    float x0{}, y0{}, z0{}, x1{}, y1{}, z1{};
    std::uint32_t color{};
};
inline int blockFloor(float value) noexcept { return static_cast<int>(std::floor(value)); }
inline int chunkAt(float value) noexcept { return chunkCoordinate(blockFloor(value)); }
inline int positiveMod(int value, int divisor) noexcept {
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}
inline bool isRegionBorder(int blockCoordinate) noexcept {
    return positiveMod(chunkCoordinate(blockCoordinate), 32) == 0;
}

inline constexpr float kLegacyChunkMinY = -64.0f;
inline constexpr float kLegacyChunkMaxY = 320.0f;

// Exact colors used by the completed 1.21.132 Praxis native ChunkBorders path.
// Packed as ABGR, matching Tessellator::color decoding in the 26.51 renderer.
inline constexpr std::uint32_t kLegacyChunkRed    = 0xFF0000FFu;
inline constexpr std::uint32_t kLegacyChunkYellow = 0xFF00FFFFu;
inline constexpr std::uint32_t kLegacyChunkBlue   = 0xFFFF0000u;
inline std::vector<BorderLine> makeChunkBorderLines(
    float playerX, float, float playerZ, ChunkBorderOptions const& options
) {
    std::vector<BorderLine> lines;
    if (!options.enabled || options.lineBudget <= 0) return lines;
    auto const budget = static_cast<std::size_t>(std::max(options.lineBudget, 0));
    lines.reserve(std::min<std::size_t>(budget, 832));

    auto add = [&](float x0, float y0, float z0,
                   float x1, float y1, float z1,
                   std::uint32_t color) {
        if (lines.size() < budget)
            lines.push_back({x0, y0, z0, x1, y1, z1, color});
    };

    int const centerX = chunkAt(playerX);
    int const centerZ = chunkAt(playerZ);
    float const minX = static_cast<float>(centerX * 16);
    float const minZ = static_cast<float>(centerZ * 16);
    // Legacy Praxis first drew the four full-height red corner lines for
    // each of the eight chunks surrounding the current chunk.
    constexpr int surrounding[8][2] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0},          {1,  0},
        {-1,  1}, {0,  1}, {1,  1},
    };
    for (auto const& offset : surrounding) {
        float const x = minX + static_cast<float>(offset[0] * 16);
        float const z = minZ + static_cast<float>(offset[1] * 16);
        add(x,        kLegacyChunkMinY, z,        x,        kLegacyChunkMaxY, z,        kLegacyChunkRed);
        add(x + 16.0f,kLegacyChunkMinY, z,        x + 16.0f,kLegacyChunkMaxY, z,        kLegacyChunkRed);
        add(x,        kLegacyChunkMinY, z + 16.0f,x,        kLegacyChunkMaxY, z + 16.0f,kLegacyChunkRed);
        add(x + 16.0f,kLegacyChunkMinY, z + 16.0f,x + 16.0f,kLegacyChunkMaxY, z + 16.0f,kLegacyChunkRed);
    }
    auto face = [&](float x1, float z1, float x2, float z2) {
        add(x1, kLegacyChunkMinY, z1, x1, kLegacyChunkMaxY, z1, kLegacyChunkBlue);

        for (float y = kLegacyChunkMinY + 2.0f; y <= kLegacyChunkMaxY; y += 2.0f) {
            bool const majorY = (static_cast<int>(y) % 16) == 0;
            add(x1, y, z1, x2, y, z2, majorY ? kLegacyChunkBlue : kLegacyChunkYellow);
        }

        if (std::fabs(x1 - x2) > 0.001f) {
            float const step = (x2 - x1) / 8.0f;
            if (step > 0.0f) {
                for (float x = x1 + step; x < x2; x += step)
                    add(x, kLegacyChunkMinY, z1, x, kLegacyChunkMaxY, z1, kLegacyChunkYellow);
            } else {
                for (float x = x1 + step; x > x2; x += step)
                    add(x, kLegacyChunkMinY, z1, x, kLegacyChunkMaxY, z1, kLegacyChunkYellow);
            }
            return;
        }
        float const step = (z2 - z1) / 8.0f;
        if (step > 0.0f) {
            for (float z = z1 + step; z < z2; z += step)
                add(x1, kLegacyChunkMinY, z, x1, kLegacyChunkMaxY, z, kLegacyChunkYellow);
        } else {
            for (float z = z1 + step; z > z2; z += step)
                add(x1, kLegacyChunkMinY, z, x1, kLegacyChunkMaxY, z, kLegacyChunkYellow);
        }
    };

    face(minX,         minZ,         minX + 16.0f, minZ);
    face(minX + 16.0f, minZ,         minX + 16.0f, minZ + 16.0f);
    face(minX + 16.0f, minZ + 16.0f, minX,         minZ + 16.0f);
    face(minX,         minZ + 16.0f, minX,         minZ);

    return lines;
}

} // namespace praxis::companion
