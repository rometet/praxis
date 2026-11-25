#include "core/BlockInfoPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace praxis::companion;
namespace {
BlockInfoInput block(std::string name, std::string display, std::vector<std::string> states = {},
                     std::optional<int> hardness = std::nullopt) {
    BlockInfoInput in;
    in.name = std::move(name);
    in.displayName = std::move(display);
    in.states = std::move(states);
    in.hardness = hardness;
    return in;
}
}
int main() {
    unsigned checks = 0;
    auto require = [&](bool good, char const* why) {
        ++checks;
        if (!good) throw std::runtime_error(why);
    };
    try {
        // 1) default: display name wins, nothing invented
        {
            BlockInfoOptions o;
            require(validBlockInfoOptions(o), "default options valid");
            auto in = block("minecraft:stone", "Stone", {}, 15);
            require(blockInfoTitle(in, o) == "Stone", "display name used");
            require(blockInfoLine(in, o) == "Stone (1.5)", "hardness formatted");
        }
        // 2) raw name when no display name
        {
            BlockInfoOptions o;
            auto in = block("minecraft:oak_stairs", "", {}, std::nullopt);
            require(blockInfoTitle(in, o) == "minecraft:oak_stairs", "raw name kept");
            require(blockInfoLine(in, o) == "minecraft:oak_stairs", "no hardness when unknown");
        }
        // 3) states are appended up to maxParts, control characters dropped
        {
            BlockInfoOptions o;
            o.maxParts = 2;
            auto in = block("minecraft:oak_stairs", "Oak Stairs",
                            {"facing=north", "half=bottom", "waterlogged=false"}, std::nullopt);
            auto line = blockInfoLine(in, o);
            require(line.find("facing=north") != std::string::npos, "first state kept");
            require(line.find("half=bottom") != std::string::npos, "second state kept");
            require(line.find("waterlogged") == std::string::npos, "maxParts respected");
        }
        {
            BlockInfoOptions o;
            auto in = block("minecraft:stone", "Stone", {"bad\nstate", "ok=1"});
            auto line = blockInfoLine(in, o);
            require(line.find('\n') == std::string::npos, "newline never injected");
            require(line.find("ok=1") != std::string::npos, "valid state kept");
        }
        // 4) air is never shown; unknown everything still yields a safe title
        {
            BlockInfoOptions o;
            BlockInfoInput air;
            air.air = true;
            require(!blockInfoVisible(air, o), "air not visible");
            BlockInfoInput nothing;
            require(!blockInfoVisible(nothing, o), "nameless block not visible");
            require(blockInfoTitle(nothing, o) == "?", "fallback title");
        }
        // 5) the line is bounded
        {
            BlockInfoOptions o;
            o.maxParts = 4;
            auto in = block("minecraft:x", std::string(200, 'n'),
                            {std::string(40, 'a'), std::string(40, 'b')});
            require(blockInfoLine(in, o).size() <= blockInfoMaxLine, "line bounded");
        }
        // 6) invalid options fail closed
        {
            BlockInfoOptions o;
            o.maxParts = 0;
            require(!validBlockInfoOptions(o), "zero maxParts rejected");
            o.maxParts = blockInfoMaxParts + 1;
            require(!validBlockInfoOptions(o), "too many parts rejected");
            BlockInfoInput in = block("minecraft:stone", "Stone");
            require(!blockInfoVisible(in, o), "invalid options hide the overlay");
        }
        std::cout << checks << " BlockInfoPolicy checks passed\n";
    } catch (std::exception const& e) {
        std::cerr << "BlockInfoPolicy FAILED after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
