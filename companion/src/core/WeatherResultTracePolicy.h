#pragma once
#include "core/WeatherOptions.h"
#include "core/WeatherDiagnosticSession.h"
#include <memory>
#include <sstream>
#include <limits>

namespace praxis::companion {
struct WeatherResultTraceKey {
    std::uint64_t generation{};
    unsigned subClient{},renderMask{};
    int dimension{};
    WeatherOptions requested{};
    bool effectiveOn{},ready{},available{},eligible{},hasWeather{},panorama{},shadow{},underLiquid{};
    bool timeChanger{},fullbright{};
    bool operator==(WeatherResultTraceKey const&) const noexcept = default;
};
struct WeatherResultValues {
    bool draw{};
    float rain{},snow{},rainAlpha{},snowAlpha{},fog{},hdr{},skyDarken{};
    bool operator==(WeatherResultValues const&) const noexcept = default;
};
// SDK-named resource presence is not readiness, GPU submission or pixels.
struct WeatherResultResources {
    bool mesh{},textureClient{},rainMaterial{},snowMaterial{};
    int particleCount{};
    float boxSize{},rainWidth{},rainLength{},snowWidth{},snowLength{};
};
struct WeatherProducerResult {
    WeatherResultTraceKey key;
    unsigned frame{};
    WeatherContextResult context=WeatherContextResult::Accepted;
    WeatherDecision decision=WeatherDecision::ScopeRejected;
    bool ownerVerified{},applied{},skyApplied{},worldValid{};
    std::array<float,4> world{};
    WeatherResultValues native,owned;
    WeatherResultResources resources;
};
struct WeatherSkyInputResult {
    WeatherResultTraceKey key;
    unsigned frame{};
    bool originSelected{};
    WeatherResultValues values;
    WeatherResultResources resources;
};
enum class WeatherTrialEnd : unsigned { None, Deadline, ReportLimit, RuntimeEnded, UserStopped, WriterError };
inline char const* weatherTrialEndName(WeatherTrialEnd end) noexcept {
    switch(end) {
    case WeatherTrialEnd::None:return "active";
    case WeatherTrialEnd::Deadline:return "deadline";
    case WeatherTrialEnd::ReportLimit:return "report-limit";
    case WeatherTrialEnd::RuntimeEnded:return "runtime-ended";
    case WeatherTrialEnd::UserStopped:return "user-stopped";
    case WeatherTrialEnd::WriterError:return "writer-error";
    }
    return "unknown";
}
inline constexpr unsigned weatherTrialReportLimit=32;
inline constexpr auto weatherTrialDuration=std::chrono::seconds(30);
struct WeatherTrialSnapshot {
    std::uint64_t session{},producerCalls{},skyInputCalls{},sameFrameScalarMatches{},sameFrameScalarDifferences{},dropped{};
    unsigned flushedReports{};
    WeatherTrialEnd end=WeatherTrialEnd::None;
    bool pending{},writerClaimed{},producerSeen{},skyInputSeen{},lastFlushSucceeded{};
    WeatherProducerResult producer;
    WeatherSkyInputResult skyInput;
};
class WeatherResultTrial {
public:
    using Clock=std::chrono::steady_clock;
private:
    std::atomic_bool accepting_{},pending_{},claimed_{};
    std::atomic_uint64_t dropped_{};
    std::mutex mutex_;
    WeatherTrialSnapshot state_;
    Clock::time_point deadline_{};
    void closeLocked(WeatherTrialEnd reason) noexcept {
        if(state_.end==WeatherTrialEnd::None)state_.end=reason;
        accepting_.store(false,std::memory_order_release);
    }
public:
    bool accepting() const noexcept { return accepting_.load(std::memory_order_acquire); }
    bool pending() const noexcept { return pending_.load(std::memory_order_acquire); }
    bool start(Clock::time_point now=Clock::now()) {
        std::lock_guard lock(mutex_);
        if(pending())return false;
        auto session=state_.session+1;if(!session)++session;
        state_={};state_.session=session;state_.pending=true;
        dropped_=0;deadline_=now+weatherTrialDuration;
        pending_.store(true,std::memory_order_release);
        accepting_.store(true,std::memory_order_release);
        return true;
    }
    // Render callbacks never wait for the writer/UI, allocate, format or do I/O.
    // Contention drops are explicit rather than represented as fresh evidence.
    void producer(WeatherProducerResult const& result,Clock::time_point now=Clock::now()) noexcept {
        if(!accepting())return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){dropped_.fetch_add(1,std::memory_order_relaxed);return;}
        if(!accepting())return;
        if(now>=deadline_){closeLocked(WeatherTrialEnd::Deadline);return;}
        state_.producer=result;state_.producerSeen=true;++state_.producerCalls;
    }
    void skyInput(WeatherSkyInputResult const& result,Clock::time_point now=Clock::now()) noexcept {
        if(!accepting())return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){dropped_.fetch_add(1,std::memory_order_relaxed);return;}
        if(!accepting())return;
        if(now>=deadline_){closeLocked(WeatherTrialEnd::Deadline);return;}
        state_.skyInput=result;state_.skyInputSeen=true;++state_.skyInputCalls;
        auto const& source=state_.producer;
        if(state_.producerSeen && source.frame==result.frame && source.key==result.key) {
            if(source.owned==result.values)++state_.sameFrameScalarMatches;
            else ++state_.sameFrameScalarDifferences;
        }
    }
    std::uint64_t claimWriter() {
        if(!pending() || claimed_.load(std::memory_order_acquire))return 0;
        std::lock_guard lock(mutex_);
        if(!pending() || claimed_.load(std::memory_order_relaxed))return 0;
        claimed_.store(true,std::memory_order_release);state_.writerClaimed=true;
        return state_.session;
    }
    WeatherTrialSnapshot snapshot(Clock::time_point now=Clock::now()) {
        std::lock_guard lock(mutex_);
        if(accepting() && now>=deadline_)closeLocked(WeatherTrialEnd::Deadline);
        auto result=state_;result.dropped=dropped_.load(std::memory_order_relaxed);return result;
    }
    void stop(WeatherTrialEnd reason) {
        std::lock_guard lock(mutex_);
        if(pending())closeLocked(reason);
    }
    void flushed(std::uint64_t session) {
        std::lock_guard lock(mutex_);
        if(state_.session!=session)return;
        state_.lastFlushSucceeded=true;++state_.flushedReports;
        // Reserve one final record for the explicit end status and latest data.
        if(state_.flushedReports>=weatherTrialReportLimit-1)closeLocked(WeatherTrialEnd::ReportLimit);
    }
    void finishWriter(std::uint64_t session,bool failed=false) {
        std::lock_guard lock(mutex_);
        if(state_.session!=session)return;
        if(failed){state_.end=WeatherTrialEnd::WriterError;state_.lastFlushSucceeded=false;accepting_=false;}
        state_.pending=false;state_.writerClaimed=false;
        pending_.store(false,std::memory_order_release);claimed_.store(false,std::memory_order_release);
    }
};
inline auto weatherResultTrial=std::make_shared<WeatherResultTrial>();

inline std::string formatWeatherTrialReport(WeatherTrialSnapshot const& s,char const* kind) {
    std::ostringstream out;out.precision(std::numeric_limits<float>::max_digits10);
    out<<"WeatherTrial session="<<s.session<<" kind="<<kind<<" end="<<weatherTrialEndName(s.end)
        <<" limit="<<weatherTrialReportLimit<<" previousFlushed="<<s.flushedReports
        <<" producerCalls="<<s.producerCalls<<" skyInputCalls="<<s.skyInputCalls<<" dropped="<<s.dropped
        <<" sameFrameScalarMatches="<<s.sameFrameScalarMatches<<" sameFrameScalarDifferences="<<s.sameFrameScalarDifferences;
    auto key=[&](WeatherResultTraceKey const& k,unsigned frame) {
        out<<" mode="<<k.requested.mode<<" intensity="<<k.requested.intensity<<" effectiveOn="<<k.effectiveOn
            <<" generation="<<k.generation<<" subClient="<<k.subClient<<" dimension="<<k.dimension<<" cameraFrame="<<frame
            <<" mask="<<k.renderMask<<" ready="<<k.ready<<" available="<<k.available<<" eligible="<<k.eligible
            <<" hasWeather="<<k.hasWeather<<" panorama="<<k.panorama<<" shadow="<<k.shadow<<" liquid="<<k.underLiquid
            <<" Time="<<k.timeChanger<<" Fullbright="<<k.fullbright;
    };
    auto values=[&](char const* label,WeatherResultValues const& v) {
        out<<' '<<label<<"(draw,rain,snow,rainAlpha,snowAlpha,fog,hdr,sky)=("<<v.draw<<','<<v.rain<<','<<v.snow
            <<','<<v.rainAlpha<<','<<v.snowAlpha<<','<<v.fog<<','<<v.hdr<<','<<v.skyDarken<<')';
    };
    auto resources=[&](WeatherResultResources const& r) {
        out<<" resourcePresence(mesh,textureClient,rainMat,snowMat)=("<<r.mesh<<','<<r.textureClient<<','<<r.rainMaterial
            <<','<<r.snowMaterial<<") particleCount="<<r.particleCount<<" box="<<r.boxSize
            <<" rain(width,length)=("<<r.rainWidth<<','<<r.rainLength<<") snow(width,length)=("<<r.snowWidth<<','<<r.snowLength<<')';
    };
    out<<" producerSeen="<<s.producerSeen;
    if(s.producerSeen) {
        auto const& p=s.producer;out<<" PRODUCER{";key(p.key,p.frame);
        out<<" owner="<<p.ownerVerified<<" decision="<<static_cast<unsigned>(p.decision)
            <<" context="<<static_cast<unsigned>(p.context)
            <<" weatherApplied="<<p.applied<<" skyApplied="<<p.skyApplied<<" worldValid="<<p.worldValid
            <<" world(rainOld,rain,thunderOld,thunder)=("<<p.world[0]<<','<<p.world[1]<<','<<p.world[2]<<','<<p.world[3]<<')';
        values("native",p.native);values("owned",p.owned);resources(p.resources);out<<'}';
    }
    out<<" skyInputSeen="<<s.skyInputSeen;
    if(s.skyInputSeen) {
        auto const& c=s.skyInput;out<<" SKY_INPUT{";key(c.key,c.frame);
        out<<" skyOriginSelected="<<c.originSelected;values("input",c.values);resources(c.resources);out<<'}';
    }
    out<<" correlation=scalar_only resourceReadiness=UNVERIFIED weatherFinalDraw=UNVERIFIED visual=UNVERIFIED";
    return out.str();
}

// Sink returns true only after its record is made readable (stream flush).
// Called by the dedicated writer, never by a game/render callback.
template<class Sink,class Wait,class Now>
void runWeatherTrialReporter(WeatherResultTrial& trial,std::uint64_t session,Sink&& sink,Wait&& wait,Now&& now) noexcept {
    try {
        bool first=true;
        for(;;) {
            auto const snapshot=trial.snapshot(now());
            if(snapshot.session!=session || !snapshot.pending)return;
            bool const ended=snapshot.end!=WeatherTrialEnd::None;
            if(!sink(snapshot,ended?"end":first?"start":"latest")) {trial.finishWriter(session,true);return;}
            trial.flushed(session);
            if(ended){trial.finishWriter(session);return;}
            first=false;wait();
        }
    } catch(...) {try{trial.finishWriter(session,true);}catch(...) {}}
}
} // namespace praxis::companion
