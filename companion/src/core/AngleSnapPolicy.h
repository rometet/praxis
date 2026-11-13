#pragma once
#include <cmath>
namespace praxis::companion {
struct AngleSnapOptions {
    bool lockYaw = false;
    bool lockPitch = false;
    bool snapYaw = false;
    bool snapPitch = false;
    float yawStep = 15.0f;
    float pitchStep = 15.0f;
    bool snapWhileCameraFeature = false;
    bool operator==(AngleSnapOptions const&) const = default;
};
inline bool validAngleSnapOptions(AngleSnapOptions const& o) noexcept {
    auto step = [](float v) { return std::isfinite(v) && v >= 1.0f && v <= 90.0f; };
    return step(o.yawStep) && step(o.pitchStep);
}
struct LookAngles { float pitch{}, yaw{}; };
inline double normalizeDegrees(double value) noexcept {
    double v = std::fmod(value, 360.0);
    if (v < 0) v += 360.0;
    return v;
}
inline double snapDegrees(double value, double step) noexcept {
    if (!std::isfinite(value) || !std::isfinite(step) || step < 1.0 || step > 90.0) return value;
    return std::round(value / step) * step;
}
// Minecraft pitch is clamped by the game; only publish a bounded snap.
inline LookAngles snapLookAngles(LookAngles current, AngleSnapOptions const& o) noexcept {
    LookAngles result = current;
    if (!validAngleSnapOptions(o)) return result;
    if (o.snapYaw && std::isfinite(current.yaw)) {
        double yaw = normalizeDegrees(static_cast<double>(snapDegrees(current.yaw, o.yawStep)));
        // Prefer the representation closest to the current angle (e.g. 359.5 -> 360).
        if (yaw > 180.0 && current.yaw <= 180.0) yaw -= 360.0;
        if (yaw < -180.0 && current.yaw >= -180.0) yaw += 360.0;
        result.yaw = static_cast<float>(yaw);
    }
    if (o.snapPitch && std::isfinite(current.pitch)) {
        double pitch = snapDegrees(current.pitch, o.pitchStep);
        result.pitch = static_cast<float>(pitch < -89.0 ? -89.0 : pitch > 89.0 ? 89.0 : pitch);
    }
    return result;
}
// MouseInputEvent is a public writable input channel. Axis locks zero the
// relative delta before Minecraft consumes it; no rotation state is hooked.
inline void applyAngleLocks(short& dx, short& dy, AngleSnapOptions const& o) noexcept {
    if (!validAngleSnapOptions(o)) return;
    if (o.lockYaw) dx = 0;
    if (o.lockPitch) dy = 0;
}
} // namespace praxis::companion
