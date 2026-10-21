#include "core/ChunkBorderGeometry.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;

int main() {
    int checks = 0;
    auto require = [&](bool value, char const* message) {
        ++checks;
        if (!value) throw std::runtime_error(message);
    };

    try {
        constexpr std::array positions{-17.0f, -16.0f, -1.0f, 0.0f, 15.0f, 16.0f};
        constexpr std::array expectedChunks{-2, -1, -1, 0, 0, 1};
        for (std::size_t index = 0; index < positions.size(); ++index)
            require(chunkAt(positions[index]) == expectedChunks[index], "signed chunk coordinate");
        ChunkBorderOptions options;
        require(makeChunkBorderLines(-1, 64, -17, options).empty(), "disabled draws nothing");

        options.enabled = true;
        options.lineBudget = 2048;
        auto lines = makeChunkBorderLines(-1, 64, -17, options);

        require(lines.size() == 832, "legacy Praxis geometry has exact line count");
        require(std::all_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.y0 >= kLegacyChunkMinY && line.y0 <= kLegacyChunkMaxY
                && line.y1 >= kLegacyChunkMinY && line.y1 <= kLegacyChunkMaxY;
        }), "legacy world-height span is bounded");
        require(std::any_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.x0 == -16 && line.z0 == -32
                && line.y0 == kLegacyChunkMinY && line.y1 == kLegacyChunkMaxY
                && line.color == kLegacyChunkBlue;
        }), "current chunk corner is legacy blue");

        require(std::any_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.x0 == -16 && line.x1 == 0
                && line.z0 == -32 && line.z1 == -32
                && line.y0 == -62 && line.y1 == -62
                && line.color == kLegacyChunkYellow;
        }), "two-block horizontal grid is legacy yellow");
        require(std::any_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.x0 == -16 && line.x1 == 0
                && line.z0 == -32 && line.z1 == -32
                && line.y0 == -48 && line.y1 == -48
                && line.color == kLegacyChunkBlue;
        }), "sixteen-block horizontal grid is legacy blue");

        require(std::any_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.x0 == -14 && line.x1 == -14
                && line.z0 == -32 && line.z1 == -32
                && line.y0 == kLegacyChunkMinY && line.y1 == kLegacyChunkMaxY
                && line.color == kLegacyChunkYellow;
        }), "two-block face subdivision is legacy yellow");
        require(std::any_of(lines.begin(), lines.end(), [](BorderLine const& line) {
            return line.x0 == -32 && line.z0 == -48
                && line.y0 == kLegacyChunkMinY && line.y1 == kLegacyChunkMaxY
                && line.color == kLegacyChunkRed;
        }), "surrounding chunk corner is legacy red");

        options.lineBudget = 3;
        require(makeChunkBorderLines(0, 64, 0, options).size() == 3,
                "tight line budget remains bounded");

        std::cout << "Chunk border legacy parity checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
