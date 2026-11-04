#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace praxis::companion {
struct RedstoneOverlayOptions {
    int range{16};
    int scanBudget{64};
    int drawBudget{1280};
    bool showValues{true};
    bool operator==(RedstoneOverlayOptions const&) const = default;
};
struct RedstoneSample {
    int x{}, y{}, z{}, power{};
    bool operator==(RedstoneSample const&) const = default;
};
inline bool isRedstoneDust(std::string_view name) noexcept {
    return name == "minecraft:redstone_wire" || name == "redstone_wire"
        || name == "minecraft:redstone_dust" || name == "redstone_dust"
        || name == "minecraft:redstone" || name == "redstone"
        || name.find("redstone_wire") != std::string_view::npos
        || name.find("redstone_dust") != std::string_view::npos;
}
inline bool isRedstoneComparator(std::string_view name) noexcept {
    return name == "minecraft:powered_comparator" || name == "powered_comparator"
        || name == "minecraft:unpowered_comparator" || name == "unpowered_comparator";
}
inline bool isRedstoneRepeater(std::string_view name) noexcept {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name == "powered_repeater" || name == "unpowered_repeater";
}
inline bool isRedstoneTorch(std::string_view name) noexcept {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name == "redstone_torch" || name == "unlit_redstone_torch";
}
inline bool validRedstonePower(int power) noexcept { return power >= 0 && power <= 15; }
inline bool redstoneInRange(RedstoneSample const& sample, int x, int y, int z,
                           RedstoneOverlayOptions const& options) noexcept {
    return std::abs(sample.x - x) <= options.range && std::abs(sample.y - y) <= 4
        && std::abs(sample.z - z) <= options.range;
}
} // namespace praxis::companion
