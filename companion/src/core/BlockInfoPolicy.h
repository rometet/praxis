#pragma once
// Crosshair block information (JADE/WAILA-style) -- pure formatting policy.
//
// What this is: the *decision* layer that turns a block under the crosshair into a
// short text line. It deliberately owns no game objects, so it can be unit tested and
// so the runtime adapter stays a thin mapper to the SDK's block/state API.
//
// What it is not: a claim that every Bedrock block has a wiki-grade description.
// Unknown properties are simply omitted -- the line never invents a value.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace praxis::companion {

inline constexpr std::size_t blockInfoMaxLine = 96;
inline constexpr std::size_t blockInfoMaxParts = 6;

struct BlockInfoInput {
    std::string name;            // SDK type name, e.g. "minecraft:stone"
    std::string displayName;     // localized/pretty name if the caller has one
    std::optional<int> hardness; // tenths, omitted when unknown
    bool air{};
    bool liquid{};
    std::vector<std::string> states;   // "facing=north", already validated by the caller
    bool operator==(BlockInfoInput const&) const = default;
};

struct BlockInfoOptions {
    bool showStates{true};
    bool showHardness{true};
    bool useDisplayName{true};
    std::size_t maxParts{4};
    bool operator==(BlockInfoOptions const&) const = default;
};

inline bool validBlockInfoOptions(BlockInfoOptions const& o) noexcept {
    return o.maxParts >= 1 && o.maxParts <= blockInfoMaxParts;
}

// The name shown to the player: never empty, never a raw empty string.
inline std::string blockInfoTitle(BlockInfoInput const& in, BlockInfoOptions const& o) {
    if (!o.useDisplayName || in.displayName.empty()) return in.name.empty() ? "?" : in.name;
    return in.displayName;
}

// One bounded line: "<title> <state> <state> (hardness N)".
// Unknown values are skipped; the result is truncated at blockInfoMaxLine.
inline std::string blockInfoLine(BlockInfoInput const& in, BlockInfoOptions const& o) {
    std::string line = blockInfoTitle(in, o);
    if (in.air) return line;
    std::size_t parts = 0;
    if (o.showStates) {
        for (auto const& s : in.states) {
            if (parts >= o.maxParts || s.empty()) break;
            if (s.find_first_of("\r\n\t") != std::string::npos) continue;   // never inject layout
            if (!line.empty()) line += ' ';
            line += s;
            ++parts;
        }
    }
    if (o.showHardness && in.hardness) {
        line += " (" + std::to_string(*in.hardness / 10) + "." +
                std::to_string(*in.hardness % 10) + ")";
    }
    if (line.size() > blockInfoMaxLine) {
        line.resize(blockInfoMaxLine);
    }
    return line;
}
// The "<title> <states> (<hardness>)" body plus the " N.Nm" sightline suffix in one pass,
// so the caller never does a second, unclamped concatenation on a finished line.
inline std::string blockInfoLineWithDistance(BlockInfoInput const& in, BlockInfoOptions const& o,
                                             double distance, bool showDistance) {
    std::string line = blockInfoLine(in, o);
    if (!showDistance || in.air || line.empty() || !(distance >= 0.0) || distance > 1.0e6) {
        return line;
    }
    char suffix[32]{};
    std::snprintf(suffix, sizeof(suffix), " %.1fm", distance);
    std::size_t const suffixLength = std::char_traits<char>::length(suffix);
    std::size_t const room         = blockInfoMaxLine > line.size() ? blockInfoMaxLine - line.size() : 0;
    line.append(suffix, std::min(suffixLength, room));
    return line;
}

// Should the overlay draw at all? Never over air, and never when the caller has no name.
inline bool blockInfoVisible(BlockInfoInput const& in, BlockInfoOptions const& o) noexcept {
    if (!validBlockInfoOptions(o)) return false;
    if (in.air) return false;
    return !in.name.empty() || !in.displayName.empty();
}
} // namespace praxis::companion
