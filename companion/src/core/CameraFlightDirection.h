#pragma once
#include "core/CameraCandidatePolicy.h"
namespace praxis::companion {
struct CameraDirectionContext {
    std::uint64_t epoch{},generation{};
    int dimension{};
    float yaw{},pitch{};
};
class CameraFlightDirection {
    struct Sample {CameraDirectionContext context;std::array<float,3> value;std::uint64_t at;};
    std::optional<Sample> sample_;
    static bool matches(CameraDirectionContext const& a,CameraDirectionContext const& b) noexcept {
        return a.epoch&&a.epoch==b.epoch&&a.generation==b.generation&&a.dimension==b.dimension
            &&std::isfinite(a.yaw)&&std::isfinite(a.pitch)&&std::isfinite(b.yaw)&&std::isfinite(b.pitch)
            &&std::abs(cameraWrappedYaw(a.yaw-b.yaw))<.0001f&&std::abs(a.pitch-b.pitch)<.0001f;
    }
public:
    static constexpr std::uint64_t maximumAgeMs=100;
    void invalidate() noexcept {sample_.reset();}
    bool capture(CameraDirectionContext context,std::array<float,3> value,std::uint64_t at) noexcept {
        float length{};
        for(auto v:value){if(!std::isfinite(v)){invalidate();return false;}length+=v*v;}
        if(!context.epoch||!std::isfinite(context.yaw)||!std::isfinite(context.pitch)
            ||!std::isfinite(length)||length<.5f||length>1.5f){invalidate();return false;}
        auto magnitude=std::sqrt(length);for(auto& v:value)v/=magnitude;
        if(sample_&&matches(sample_->context,context)) {
            float dot{};for(unsigned i=0;i<3;++i)dot+=sample_->value[i]*value[i];
            if(dot<.95f){invalidate();return false;}
        }
        sample_=Sample{context,value,at};return true;
    }
    bool read(CameraDirectionContext context,std::uint64_t now,std::array<float,3>& value) noexcept {
        if(!sample_||!matches(sample_->context,context)||now<sample_->at||now-sample_->at>maximumAgeMs) {
            invalidate();return false;
        }
        value=sample_->value;return true;
    }
};
}
