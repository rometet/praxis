#pragma once
#include "core/FlexiblePlacementPolicy.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace praxis::companion {
// These points are never absolute world coordinates. A small integer block
// offset from the same anchor is converted to float before applying UV.
struct FlexibleOverlayPoint {
    float x{},y{},z{};
};
inline constexpr FlexibleOverlayPoint flexibleOverlayCellOffset(
    FlexibleCell cell,FlexibleCell anchor) noexcept {
    return {
        static_cast<float>(static_cast<std::int64_t>(cell.x)-anchor.x),
        static_cast<float>(static_cast<std::int64_t>(cell.y)-anchor.y),
        static_cast<float>(static_cast<std::int64_t>(cell.z)-anchor.z)
    };
}
inline constexpr FlexibleOverlayPoint flexibleOverlayFacePoint(
    FlexibleCell cell,FlexibleCell anchor,FlexibleFace face,float u,float v) noexcept {
    auto const p=flexibleOverlayCellOffset(cell,anchor);
    constexpr float epsilon=0.004f;
    switch(face) {
    case FlexibleFace::Down: return {p.x+u,p.y-epsilon,p.z+v};
    case FlexibleFace::Up: return {p.x+u,p.y+1.f+epsilon,p.z+v};
    case FlexibleFace::North: return {p.x+u,p.y+v,p.z-epsilon};
    case FlexibleFace::South: return {p.x+u,p.y+v,p.z+1.f+epsilon};
    case FlexibleFace::West: return {p.x-epsilon,p.y+v,p.z+u};
    case FlexibleFace::East: return {p.x+1.f+epsilon,p.y+v,p.z+u};
    }
    return {};
}
inline constexpr std::array<FlexibleOverlayPoint,8> flexibleOverlayCubeCorners(
    FlexibleCell cell,FlexibleCell anchor) noexcept {
    auto const p=flexibleOverlayCellOffset(cell,anchor);
    return {{
        {p.x,p.y,p.z},{p.x+1.f,p.y,p.z},
        {p.x+1.f,p.y+1.f,p.z},{p.x,p.y+1.f,p.z},
        {p.x,p.y,p.z+1.f},{p.x+1.f,p.y,p.z+1.f},
        {p.x+1.f,p.y+1.f,p.z+1.f},{p.x,p.y+1.f,p.z+1.f}
    }};
}
inline FlexibleOverlayPoint flexibleOverlayCameraTranslation(
    FlexibleCell anchor,double camX,double camY,double camZ) noexcept {
    // Subtract in double BEFORE conversion to float. The renderer's world
    // matrix stack receives a small camera-relative offset, not -30M.
    return {
        static_cast<float>(static_cast<double>(anchor.x)-camX),
        static_cast<float>(static_cast<double>(anchor.y)-camY),
        static_cast<float>(static_cast<double>(anchor.z)-camZ)
    };
}
inline bool validFlexibleOverlayCameraTranslation(FlexibleOverlayPoint p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)
        && std::abs(p.x)<1024.f&&std::abs(p.y)<1024.f&&std::abs(p.z)<1024.f;
}
} // namespace praxis::companion
