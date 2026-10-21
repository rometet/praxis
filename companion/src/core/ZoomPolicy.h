#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace praxis::companion {
struct ZoomOptions {
    float targetFov{30.0f};
    bool hold{true};
    bool animation{true};
    float animationSpeed{16.0f};
    bool scrollAdjust{true};
    float scrollSensitivity{2.0f};
    bool adjustCameraSensitivity{true};
    bool smoothCamera{};
    float cameraSmoothingMultiplier{1.0f};
    bool operator==(ZoomOptions const&) const = default;
};

inline bool validZoomOptions(ZoomOptions const& value) noexcept {
    return std::isfinite(value.targetFov) && value.targetFov >= 3.5f && value.targetFov <= 70.0f
        && std::isfinite(value.animationSpeed) && value.animationSpeed >= 1.0f && value.animationSpeed <= 30.0f
        && std::isfinite(value.scrollSensitivity) && value.scrollSensitivity >= 0.5f && value.scrollSensitivity <= 10.0f
        && std::isfinite(value.cameraSmoothingMultiplier)
        && value.cameraSmoothingMultiplier >= 0.1f && value.cameraSmoothingMultiplier <= 5.0f;
}

class ZoomState {
    float factor_{1.0f};
    float scrollOffset_{};
    float smoothX_{};
    float smoothY_{};
    float fractionX_{};
    float fractionY_{};
    bool engaged_{};
    bool smoothReady_{};

public:
    void reset() noexcept {
        factor_ = 1.0f;
        scrollOffset_ = smoothX_ = smoothY_ = 0.0f;
        fractionX_ = fractionY_ = 0.0f;
        engaged_ = smoothReady_ = false;
    }
    bool engaged() const noexcept { return engaged_; }
    float factor() const noexcept { return factor_; }
    float effectiveFov(ZoomOptions const& options) const noexcept {
        return std::clamp(options.targetFov + scrollOffset_, 3.5f, 70.0f);
    }
    float update(bool enabled, bool allowed, bool keyHeld, ZoomOptions const& options,
                 float seconds) noexcept {
        engaged_ = enabled && allowed && (!options.hold || keyHeld);
        if (!engaged_) {
            // Release, focus loss, menu entry and disable restore vanilla in
            // the same update, even when animation is enabled.
            factor_ = 1.0f;
            smoothReady_ = false;
            fractionX_ = fractionY_ = 0.0f;
            return factor_;
        }
        float const target = effectiveFov(options) / 70.0f;
        if (!options.animation) factor_ = target;
        else factor_ += (target - factor_) * std::clamp(seconds * options.animationSpeed, 0.0f, 1.0f);
        return factor_;
    }
    bool scroll(int direction, ZoomOptions const& options) noexcept {
        if (!engaged_ || !options.scrollAdjust || direction == 0) return false;
        scrollOffset_ = std::clamp(effectiveFov(options) - options.targetFov
            - (direction > 0 ? options.scrollSensitivity : -options.scrollSensitivity),
            3.5f - options.targetFov, 70.0f - options.targetFov);
        return true;
    }
    void adjustMouse(short& dx, short& dy, ZoomOptions const& options, float seconds) noexcept {
        if (!engaged_) { smoothReady_ = false; return; }
        float x = dx, y = dy;
        if (options.adjustCameraSensitivity) { x *= factor_; y *= factor_; }
        if (options.smoothCamera) {
            float const blend = std::clamp(seconds * options.cameraSmoothingMultiplier * 12.0f, 0.0f, 1.0f);
            if (!smoothReady_) { smoothX_ = x; smoothY_ = y; smoothReady_ = true; }
            else { smoothX_ += (x - smoothX_) * blend; smoothY_ += (y - smoothY_) * blend; }
            x = smoothX_; y = smoothY_;
        } else smoothReady_ = false;
        x += fractionX_; y += fractionY_;
        dx = static_cast<short>(std::clamp(std::lround(x),
            static_cast<long>(std::numeric_limits<short>::min()),
            static_cast<long>(std::numeric_limits<short>::max())));
        dy = static_cast<short>(std::clamp(std::lround(y),
            static_cast<long>(std::numeric_limits<short>::min()),
            static_cast<long>(std::numeric_limits<short>::max())));
        fractionX_ = x - dx;
        fractionY_ = y - dy;
    }
};
} // namespace praxis::companion
