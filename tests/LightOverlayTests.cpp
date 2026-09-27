#include "core/LightOverlayPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;
int main() {
    int checks{};
    auto require = [&](bool condition, char const* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    };
    try {
        LightOverlayOptions options;
        LightSample sample{-1, 64, -17, 15, 0};
        require(effectiveLight(sample, options) == 15, "sky contributes to effective light");
        require(displayLight(sample, options), "show all includes daylight");
        options.showAllLevels = false;
        require(!displayLight(sample, options), "threshold filters daylight");
        options.ignoreSkyLight = true;
        require(effectiveLight(sample, options) == 0, "ignore sky uses block light");
        require(displayLight(sample, options), "dark block light passes threshold");
        sample.block = 15;
        require(!displayLight(sample, options), "maximum block light filtered");
        options.spawnThreshold = 15;
        require(displayLight(sample, options), "maximum threshold includes 15");
        options.range = 16;
        require(lightInRange(sample, -1, 64, -17, options), "center retained");
        require(lightInRange(sample, 15, 68, -1, options), "inclusive range retained");
        require(!lightInRange(sample, 16, 64, -17, options), "horizontal range pruned");
        require(!lightInRange(sample, -1, 69, -17, options), "vertical range pruned");
        require(acceptLightSurface(true, false, true, false, options), "solid top surface accepted");
        require(!acceptLightSurface(false, false, true, false, options), "covered top surface rejected");
        require(!acceptLightSurface(true, true, false, false, options), "air floor rejected");
        require(!acceptLightSurface(true, false, true, true, options), "decorative floor rejected");
        options.onlyTopSurface = false;
        require(acceptLightSurface(true, true, false, true, options), "top surface filter can be disabled");
        int dx{}, dz{};
        require(lightColumnOffset(0, 1, dx, dz) && dx == 0 && dz == 0, "center first");
        constexpr std::array<std::array<int, 2>, 8> ring{{
            {-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}}};
        for (std::size_t i = 0; i < ring.size(); ++i)
            require(lightColumnOffset(i+1, 1, dx, dz) && dx == ring[i][0] && dz == ring[i][1],
                    "first ring sequence");
        require(!lightColumnOffset(9, 1, dx, dz), "scan ends after configured radius");
        require(lightVerticalOffsets.size() == 5 && lightVerticalOffsets[0] == -1,
                "legacy near-floor scan order");
        std::cout << "Light overlay checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
