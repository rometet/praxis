#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace praxis::companion {
enum class WeatherProducer : unsigned { View, Rain, WeatherMove, SkyMove, Count };
enum class WeatherContextResult : unsigned {
    Accepted, NoClient, ClientMismatch, SubClientMismatch, NotPlaying,
    NoPlayer, RendererMismatch, CameraMismatch, DimensionMismatch, Count
};
enum class WeatherDecision : unsigned {
    ScopeRejected, GenerationChanged, NoWeather, AdmissionClosed, RuntimeNotReady,
    SettingOff, Shadow, ScopeAccepted, Applied, Observer, OriginIncomplete, Count
};
enum class WeatherSessionEnd : unsigned { None, Deadline, EventLimit, RuntimeEnded };
inline WeatherContextResult weatherDiagnosticContextResult(bool client, bool primaryClient, bool subClient,
    bool playing, bool player, bool renderer, bool camera, bool dimension) noexcept {
    if (!client) return WeatherContextResult::NoClient;
    if (!primaryClient) return WeatherContextResult::ClientMismatch;
    if (!subClient) return WeatherContextResult::SubClientMismatch;
    if (!playing) return WeatherContextResult::NotPlaying;
    if (!player) return WeatherContextResult::NoPlayer;
    if (!renderer) return WeatherContextResult::RendererMismatch;
    if (!camera) return WeatherContextResult::CameraMismatch;
    if (!dimension) return WeatherContextResult::DimensionMismatch;
    return WeatherContextResult::Accepted;
}
inline constexpr unsigned weatherDiagnosticEventLimit = 4096;
inline constexpr auto weatherDiagnosticDuration = std::chrono::seconds(30);
inline constexpr std::uint64_t weatherDiagnosticAmountLimit = 1048576;

// Scalars only: these never retain a native object, pointer, coordinate or
// source weather value. Booleans describe the reads at the observed boundary.
struct WeatherDiagnosticState {
    bool setting{}, ready{}, admission{}, capability{};
    bool noRenderWeather{}, noRenderParticles{}, noRenderFog{};
};
inline bool weatherDiagnosticOffNativePass(WeatherDiagnosticState const& before,
    WeatherDiagnosticState const& after, bool eligible, bool hasWeather, bool sameGeneration, bool applied) noexcept {
    // Two OFF snapshots can bracket a transient ON read. Never call an
    // actually modified return value an OFF native pass-through.
    return !applied && eligible && hasWeather && sameGeneration && !before.setting && !after.setting
        && before.ready && after.ready && before.admission && after.admission;
}
struct WeatherDiagnosticEntry {
    WeatherDiagnosticState state;
    WeatherContextResult context = WeatherContextResult::Accepted;
    WeatherDecision scope = WeatherDecision::Observer;
    bool eligible{}, hasWeather{}, playing{};
};
struct WeatherDiagnosticResult {
    WeatherDiagnosticState state;
    WeatherDecision decision = WeatherDecision::OriginIncomplete;
    bool applied{}, offNativePass{}, nativeDraw{}, returnedDraw{}, nativeFog{}, returnedFog{};
    bool nativeSplash{}, returnedSplash{}, mutationVerified{};
    std::uint64_t removed{};
};
struct WeatherProducerCounts {
    std::uint64_t calls{}, completed{}, eligible{}, playing{}, hasWeather{};
    std::uint64_t entryOn{}, exitOn{}, entryNotReady{}, entryAdmissionClosed{}, entryNoCapability{};
    std::uint64_t applied{}, offNativePass{}, nativeDraw{}, returnedDraw{}, nativeFog{}, returnedFog{};
    std::uint64_t nativeSplash{}, returnedSplash{}, removed{}, mutationVerified{};
    std::array<std::uint64_t, static_cast<unsigned>(WeatherContextResult::Count)> contexts{};
    std::array<std::uint64_t, static_cast<unsigned>(WeatherDecision::Count)> scopes{}, decisions{};
};
struct WeatherDiagnosticSummary {
    std::uint64_t session{}, events{}, inFlight{}, serviceSamples{}, requestedOn{}, publishedOn{};
    std::uint64_t requestMismatch{}, noRenderWeather{}, noRenderParticles{}, noRenderFog{};
    std::uint64_t amountSaturations{};
    unsigned hookMask{};
    WeatherSessionEnd end = WeatherSessionEnd::None;
    std::array<WeatherProducerCounts, static_cast<unsigned>(WeatherProducer::Count)> producers{};
};
struct WeatherDiagnosticTicket { std::uint64_t session{}; WeatherProducer producer{}; explicit operator bool() const { return session != 0; } };
struct WeatherDiagnosticPoll { std::optional<WeatherDiagnosticSummary> started, finished; };

class WeatherDiagnosticSession {
public:
    using Clock = std::chrono::steady_clock;
private:
    std::atomic_bool accepting_{}, pending_{};
    std::mutex mutex_;
    WeatherDiagnosticSummary counts_{};
    Clock::time_point deadline_{};
    bool startPending_{};
    void closeLocked(WeatherSessionEnd end) noexcept {
        if (counts_.end == WeatherSessionEnd::None) counts_.end = end;
        accepting_.store(false, std::memory_order_release);
    }
public:
    bool accepting() const noexcept { return accepting_.load(std::memory_order_acquire); }
    bool pending() const noexcept { return pending_.load(std::memory_order_acquire); }
    void start(Clock::time_point now = Clock::now()) {
        std::lock_guard lock(mutex_);
        auto next = counts_.session + 1;
        if (!next) ++next;
        counts_ = {}; counts_.session = next;
        deadline_ = now + weatherDiagnosticDuration;
        startPending_ = true;
        pending_.store(true, std::memory_order_release);
        accepting_.store(true, std::memory_order_release);
    }
    WeatherDiagnosticTicket begin(WeatherProducer producer, Clock::time_point now = Clock::now()) {
        if (!accepting()) return {};
        std::lock_guard lock(mutex_);
        if (!accepting()) return {};
        if (now >= deadline_) { closeLocked(WeatherSessionEnd::Deadline); return {}; }
        ++counts_.events; ++counts_.inFlight;
        ++counts_.producers[static_cast<unsigned>(producer)].calls;
        if (counts_.events == weatherDiagnosticEventLimit) closeLocked(WeatherSessionEnd::EventLimit);
        return {counts_.session, producer};
    }
    void entry(WeatherDiagnosticTicket ticket, WeatherDiagnosticEntry const& e) {
        if (!ticket) return;
        std::lock_guard lock(mutex_);
        if (ticket.session != counts_.session) return;
        auto& c = counts_.producers[static_cast<unsigned>(ticket.producer)];
        c.eligible += e.eligible; c.playing += e.playing; c.hasWeather += e.hasWeather;
        c.entryOn += e.state.setting; c.entryNotReady += !e.state.ready;
        c.entryAdmissionClosed += !e.state.admission; c.entryNoCapability += !e.state.capability;
        ++c.contexts[static_cast<unsigned>(e.context)]; ++c.scopes[static_cast<unsigned>(e.scope)];
    }
    void complete(WeatherDiagnosticTicket ticket, WeatherDiagnosticResult const& r) {
        if (!ticket) return;
        std::lock_guard lock(mutex_);
        if (ticket.session != counts_.session) return;
        auto& c = counts_.producers[static_cast<unsigned>(ticket.producer)];
        ++c.completed; --counts_.inFlight; c.exitOn += r.state.setting;
        ++c.decisions[static_cast<unsigned>(r.decision)]; c.applied += r.applied;
        c.offNativePass += r.offNativePass; c.nativeDraw += r.nativeDraw; c.returnedDraw += r.returnedDraw;
        c.nativeFog += r.nativeFog; c.returnedFog += r.returnedFog;
        c.nativeSplash += r.nativeSplash; c.returnedSplash += r.returnedSplash;
        c.mutationVerified += r.mutationVerified;
        auto const available = weatherDiagnosticAmountLimit - c.removed;
        if (r.removed > available) ++counts_.amountSaturations;
        c.removed += r.removed < available ? r.removed : available;
    }
    void stop() {
        if (!pending()) return;
        std::lock_guard lock(mutex_);
        closeLocked(WeatherSessionEnd::RuntimeEnded);
    }
    WeatherDiagnosticPoll poll(bool requested, WeatherDiagnosticState state, unsigned hookMask,
                               Clock::time_point now = Clock::now()) {
        if (!pending()) return {};
        std::lock_guard lock(mutex_);
        counts_.hookMask = hookMask;
        WeatherDiagnosticPoll result;
        if (accepting()) {
            if (now >= deadline_) closeLocked(WeatherSessionEnd::Deadline);
            else if (counts_.serviceSamples < weatherDiagnosticEventLimit) {
                ++counts_.serviceSamples; counts_.requestedOn += requested; counts_.publishedOn += state.setting;
                counts_.requestMismatch += requested != state.setting;
                counts_.noRenderWeather += state.noRenderWeather;
                counts_.noRenderParticles += state.noRenderParticles; counts_.noRenderFog += state.noRenderFog;
            }
        }
        if (startPending_) { result.started = counts_; startPending_ = false; }
        if (counts_.end != WeatherSessionEnd::None && counts_.inFlight == 0) {
            result.finished = counts_; pending_.store(false, std::memory_order_release);
        }
        return result;
    }
};
inline WeatherDiagnosticSession weatherDiagnosticSession;
inline std::atomic<unsigned> weatherDiagnosticHookMask{};

// Stack-owned ticket ensures an exceptional origin is counted as incomplete;
// no origin result or native reference lives beyond the existing hook call.
class WeatherDiagnosticCapture {
    WeatherDiagnosticTicket ticket_{};
public:
    explicit WeatherDiagnosticCapture(WeatherProducer producer) noexcept {
        try { if (weatherDiagnosticSession.accepting()) ticket_ = weatherDiagnosticSession.begin(producer); } catch (...) {}
    }
    ~WeatherDiagnosticCapture() { try { if (ticket_) weatherDiagnosticSession.complete(ticket_, {}); } catch (...) {} }
    explicit operator bool() const noexcept { return static_cast<bool>(ticket_); }
    void entry(WeatherDiagnosticEntry const& e) noexcept { try { weatherDiagnosticSession.entry(ticket_, e); } catch (...) {} }
    void complete(WeatherDiagnosticResult const& r) noexcept {
        try { weatherDiagnosticSession.complete(ticket_, r); } catch (...) {}
        ticket_ = {};
    }
    WeatherDiagnosticCapture(WeatherDiagnosticCapture const&) = delete;
    WeatherDiagnosticCapture& operator=(WeatherDiagnosticCapture const&) = delete;
};
} // namespace praxis::companion
