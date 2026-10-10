#pragma once
#include "map/MapTypes.h"
struct ImDrawList;
namespace praxis::companion::map {
struct Canvas {
    Transform transform;
    double halfSize{};
    bool circular{};
    bool snap{true}, radarNames{}, distanceRadar{true}, waypoints{true}, caveShade{true};
    std::array<bool, 5> radarCategories{true, false, false, false, false};
    std::uint8_t viewMask{3};
};
void drawMapLabel(ImDrawList &, char const *, float x, float y, float width, float height);
std::size_t drawMapCanvas(ImDrawList &, Snapshot const &, Canvas const &);
void configureCanvas(Canvas &, Settings const &) noexcept;
} // namespace praxis::companion::map
