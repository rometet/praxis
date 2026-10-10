#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include "core/HotkeyPolicy.h"

namespace praxis::companion {
// Stable persisted IDs; actual detached sessions are never serialized.
enum class DetachedCameraMode : unsigned { None = 0, Freelook = 20, FreeCamera = 21 };
// Camera-only single-key chords require no additional modifier. Keep the
// persisted mask and every other owner's exact None/Any contract unchanged.
inline constexpr unsigned cameraHotkeyModifiers(unsigned modifiers) noexcept {
    return modifiers == 0 ? anyModifiers : modifiers;
}
inline constexpr bool cameraModifiersMatch(unsigned wanted, unsigned actual) noexcept {
    return modifiersMatch(cameraHotkeyModifiers(wanted), actual);
}
struct CameraOptions {
    bool hold = true;
    float degreesPerPixel = 0.15f;
    float wheelDistance = 1.0f;
    float flightSpeed = 4.0f;
    bool operator==(CameraOptions const&) const = default;
};
inline bool validCameraOptions(CameraOptions const& o) noexcept {
    return std::isfinite(o.degreesPerPixel) && o.degreesPerPixel >= .01f && o.degreesPerPixel <= 1.f
        && std::isfinite(o.wheelDistance) && o.wheelDistance >= .1f && o.wheelDistance <= 4.f
        && std::isfinite(o.flightSpeed) && o.flightSpeed >= .25f && o.flightSpeed <= 16.f;
}
struct CameraCandidateOptions {
    bool enabled = false;
    int key = 0;
    bool hold = true;
    float degreesPerPixel = 0.15f;
    float wheelDistance = 1.0f;
    float flightSpeed = 4.0f;
    unsigned modifiers = anyModifiers;
    bool operator==(CameraCandidateOptions const&) const = default;
};
inline bool validCameraCandidateOptions(CameraCandidateOptions const& o) noexcept {
    return o.key >= 0 && o.key <= 255 && std::isfinite(o.degreesPerPixel)
        && o.degreesPerPixel >= 0.01f && o.degreesPerPixel <= 1.0f
        && std::isfinite(o.wheelDistance) && o.wheelDistance >= 0.1f && o.wheelDistance <= 4.0f
        && std::isfinite(o.flightSpeed) && o.flightSpeed >= .25f && o.flightSpeed <= 16.f
        && validModifiers(o.modifiers);
}
class CameraActivation {
    bool down_{}, wanted_{}, requireRelease_{};
public:
    void cancel() noexcept { wanted_ = false; requireRelease_ = down_; }
    bool wanted() const noexcept { return wanted_; }
    void release(CameraCandidateOptions const& o) noexcept {
        down_=false;requireRelease_=false;
        if(o.hold)wanted_=false;
    }
    bool update(CameraCandidateOptions const& o, bool allowed, bool down) noexcept {
        bool edge = down && !down_; down_ = down;
        if (!down) requireRelease_ = false;
        if (!allowed || !o.enabled || !o.key) { cancel(); return false; }
        if (requireRelease_) return false;
        if (o.hold) wanted_ = down;
        else if (edge) wanted_ = !wanted_;
        return wanted_;
    }
};
inline float cameraWrappedYaw(float yaw) noexcept {
    return std::remainder(yaw, 2.0f * std::numbers::pi_v<float>);
}
inline bool validCameraPitchBounds(float minimum,float maximum) noexcept {
    return std::isfinite(minimum)&&std::isfinite(maximum)&&minimum<maximum
        &&minimum>=-std::numbers::pi_v<float>/2-.01f
        &&maximum<=std::numbers::pi_v<float>/2+.01f;
}
inline std::optional<float> cameraRestorePitch(float degrees,float minimum,float maximum) noexcept {
    if(!std::isfinite(degrees)||!validCameraPitchBounds(minimum,maximum))return {};
    auto pitch=degrees*(std::numbers::pi_v<float>/180.0f);
    if(!std::isfinite(pitch))return {};
    return std::clamp(pitch,minimum,maximum);
}
struct CameraPose {
    float yaw{}, pitch{};
    std::array<float,3> displacement{};
};
class CameraCandidateMotion {
    std::optional<CameraPose> pose_;
    float minimum_{}, maximum_{};
public:
    bool begin(float yaw, float pitch, float minimum, float maximum) noexcept {
        if (pose_ || !std::isfinite(yaw) || !std::isfinite(pitch)
            || !validCameraPitchBounds(minimum,maximum)
            || pitch < minimum || pitch > maximum) return false;
        minimum_=minimum; maximum_=maximum; pose_=CameraPose{cameraWrappedYaw(yaw),pitch,{}};
        return true;
    }
    bool look(int dx, int dy, float degreesPerPixel) noexcept {
        if (!pose_ || !std::isfinite(degreesPerPixel) || degreesPerPixel < .01f
            || degreesPerPixel > 1 || std::abs(static_cast<long long>(dx)) > 4096
            || std::abs(static_cast<long long>(dy)) > 4096) return false;
        float scale=degreesPerPixel * std::numbers::pi_v<float> / 180.0f;
        pose_->yaw=cameraWrappedYaw(pose_->yaw+dx*scale);
        pose_->pitch=std::clamp(pose_->pitch+dy*scale,minimum_,maximum_);
        return true;
    }
    bool move(int steps, float distance, std::array<float,3> forward) noexcept {
        if (!pose_ || steps < -32 || steps > 32 || !std::isfinite(distance)
            || distance < .1f || distance > 4) return false;
        float length{};
        for (float v : forward) { if (!std::isfinite(v)) return false; length+=v*v; }
        if (!std::isfinite(length) || length < .5f || length > 1.5f) return false;
        length=std::sqrt(length);
        auto next=pose_->displacement;
        for (unsigned i=0;i<3;++i) {
            next[i]+=steps*distance*forward[i]/length;
            if(!std::isfinite(next[i]))return false;
        }
        pose_->displacement=next; return true;
    }
    bool moveTimed(int forwardAxis, int rightAxis, int upAxis, float speed, float seconds,
                   std::array<float,3> forward) noexcept {
        if (!pose_ || forwardAxis < -1 || forwardAxis > 1 || rightAxis < -1 || rightAxis > 1
            || upAxis < -1 || upAxis > 1 || !std::isfinite(speed) || speed < .25f || speed > 16.f
            || !std::isfinite(seconds) || seconds < 0 || seconds > .1f) return false;
        float norm{};for(auto v:forward){if(!std::isfinite(v))return false;norm+=v*v;}
        if(!std::isfinite(norm)||norm<.5f||norm>1.5f)return false;
        auto length=std::sqrt(norm);for(auto& v:forward)v/=length;
        auto horizontal=std::hypot(forward[0],forward[2]);
        if(rightAxis && horizontal<.001f)return false;
        std::array<float,3> direction{};
        for(unsigned i=0;i<3;++i)direction[i]=forwardAxis*forward[i];
        if(rightAxis){direction[0]-=rightAxis*forward[2]/horizontal;direction[2]+=rightAxis*forward[0]/horizontal;}
        direction[1]+=upAxis;
        float squared{};for(auto v:direction)squared+=v*v;
        if(squared<.000001f)return true;
        auto scale=speed*seconds/std::sqrt(squared);
        auto next=pose_->displacement;
        for(unsigned i=0;i<3;++i) {
            next[i]+=direction[i]*scale;
            if(!std::isfinite(next[i]))return false;
        }
        pose_->displacement=next;return true;
    }
    std::optional<CameraPose> const& pose() const noexcept { return pose_; }
    void reset() noexcept { pose_.reset(); }
};
}
