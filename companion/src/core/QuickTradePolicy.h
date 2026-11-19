#pragma once
#include <cstdint>
namespace praxis::companion {
// One-click repeat trading is a confirmation-gated state machine. It never
// builds or sends a transaction: the platform layer performs exactly one
// vanilla trade per `beginTrade()` grant and reports the outcome back here.
// Every grant is bounded by a fresh user activation, a context generation and
// a single outstanding request, so a stale or duplicated acknowledgment can
// never authorize a second trade.
inline constexpr int quickTradeMinimumTrades = 1;
inline constexpr int quickTradeMaximumTrades = 64;
inline constexpr std::uint64_t quickTradeDefaultTimeoutMs = 2000;
enum class QuickTradeState { Idle, Begin, WaitingAck, Completed, Cancelled, Stopped };
enum class QuickTradeStop {
    None,
    Cancelled,
    TimedOut,
    Rejected,
    OfferInvalidated,
    InventoryInvalidated,
    ContextChanged,
    TradeLimitReached,
    InvalidRequest,
};
// Everything the caller observed about the open trade screen. `generation`
// changes on world/player/villager change; `offerGeneration` changes whenever
// the selected recipe or its costs are re-read from the container.
struct QuickTradeContext {
    std::uint64_t generation{};
    std::uint64_t offerGeneration{};
    int tradeIndex{-1};
    bool screenOpen{};
    bool offerValid{};
    bool inventoryValid{};
    bool outputSpace{};
    bool pendingInventoryRequest{};
    bool operator==(QuickTradeContext const&) const = default;
};
struct QuickTradeOptions {
    bool enabled{};
    bool requireConfirmation = true;
    int maxTrades = quickTradeMinimumTrades;
    std::uint64_t timeoutMs = quickTradeDefaultTimeoutMs;
    bool operator==(QuickTradeOptions const&) const = default;
};
inline bool validQuickTradeOptions(QuickTradeOptions const& o) noexcept {
    return o.maxTrades >= quickTradeMinimumTrades && o.maxTrades <= quickTradeMaximumTrades
        && o.timeoutMs > 0 && o.timeoutMs <= 60000;
}
// A context is usable only when the screen is open, the offer and inventory are
// still valid, the output has room and no other inventory request is pending.
inline bool quickTradeContextUsable(QuickTradeContext const& c) noexcept {
    return c.generation != 0 && c.offerGeneration != 0 && c.tradeIndex >= 0
        && c.screenOpen && c.offerValid && c.inventoryValid && c.outputSpace
        && !c.pendingInventoryRequest;
}
class QuickTradeSession {
public:
    // Default OFF: nothing happens until the user enables the feature and
    // explicitly activates it with a fresh context.
    explicit QuickTradeSession(QuickTradeOptions options = {}) noexcept : options_(options) {}
    QuickTradeOptions const& options() const noexcept { return options_; }
    // Changing options is a hard stop; a new activation is required afterwards.
    bool configure(QuickTradeOptions options) noexcept {
        if (!validQuickTradeOptions(options)) return false;
        if (!(options == options_)) stop(QuickTradeStop::Cancelled);
        options_ = options;
        return true;
    }
    QuickTradeState state() const noexcept { return state_; }
    QuickTradeStop stopReason() const noexcept { return reason_; }
    bool active() const noexcept { return state_ == QuickTradeState::Begin || state_ == QuickTradeState::WaitingAck; }
    bool awaitingAck() const noexcept { return state_ == QuickTradeState::WaitingAck; }
    std::uint64_t outstandingToken() const noexcept { return outstandingToken_; }
    int confirmedTrades() const noexcept { return confirmed_; }
    int grantedTrades() const noexcept { return granted_; }
    int remainingTrades() const noexcept { return options_.maxTrades - confirmed_; }
    std::uint64_t generation() const noexcept { return context_.generation; }
    std::uint64_t offerGeneration() const noexcept { return context_.offerGeneration; }
    int tradeIndex() const noexcept { return context_.tradeIndex; }
    // Explicit user activation. The caller must have already confirmed the
    // action; `requireConfirmation` keeps the gate on by default.
    bool begin(QuickTradeContext const& context, std::uint64_t now, bool confirmed) noexcept {
        if (!options_.enabled || !validQuickTradeOptions(options_)) return false;
        if (options_.requireConfirmation && !confirmed) return false;
        if (!quickTradeContextUsable(context)) return false;
        if (active()) return false;
        // Each explicit activation has its own independent trade budget.
        confirmed_ = 0;
        context_ = context;
        state_ = QuickTradeState::Begin;
        reason_ = QuickTradeStop::None;
        started_ = now;
        deadline_ = now + options_.timeoutMs;
        granted_ = 0;
        outstanding_ = false;
        return true;
    }
    // Called by the platform layer immediately before it performs the single
    // permitted vanilla trade. Returns false when no trade may be issued.
    bool beginTrade(std::uint64_t now) noexcept {
        if (state_ != QuickTradeState::Begin) return false;
        if (outstanding_) return false;
        if (granted_ >= options_.maxTrades) { stop(QuickTradeStop::TradeLimitReached); return false; }
        if (expired(now)) { stop(QuickTradeStop::TimedOut); return false; }
        if (++nextToken_ == 0) ++nextToken_; // 0 is never a valid token
        outstandingToken_ = nextToken_;
        outstanding_ = true;
        ++granted_;
        state_ = QuickTradeState::WaitingAck;
        return true;
    }
    // Acknowledgment of the outstanding trade. Duplicate, stale-generation and
    // unsolicited acknowledgments are ignored rather than counted.
    bool acknowledge(std::uint64_t generation, std::uint64_t token, std::uint64_t now, bool success) noexcept {
        if (state_ != QuickTradeState::WaitingAck || !outstanding_) return false;
        if (generation != context_.generation || !token || token != outstandingToken_) return false;
        if (expired(now)) { stop(QuickTradeStop::TimedOut); return false; }
        outstanding_ = false;
        outstandingToken_ = 0;
        if (!success) { stop(QuickTradeStop::Rejected); return false; }
        ++confirmed_;
        if (confirmed_ >= options_.maxTrades) { stop(QuickTradeStop::TradeLimitReached); return true; }
        state_ = QuickTradeState::Begin;
        return true;
    }
    // Re-observation of the trade screen between trades. A changed world,
    // villager, recipe or inventory state stops the session before the next
    // trade is granted.
    bool observe(QuickTradeContext const& context, std::uint64_t now) noexcept {
        if (!active()) return false;
        if (expired(now)) { stop(QuickTradeStop::TimedOut); return false; }
        if (context.generation != context_.generation) { stop(QuickTradeStop::ContextChanged); return false; }
        if (context.offerGeneration != context_.offerGeneration || context.tradeIndex != context_.tradeIndex) {
            stop(QuickTradeStop::OfferInvalidated);
            return false;
        }
        if (!context.screenOpen || !context.offerValid) { stop(QuickTradeStop::OfferInvalidated); return false; }
        if (!context.inventoryValid || !context.outputSpace) { stop(QuickTradeStop::InventoryInvalidated); return false; }
        if (context.pendingInventoryRequest) { stop(QuickTradeStop::InventoryInvalidated); return false; }
        return true;
    }
    // Timeout is fail-closed: an unanswered trade stops the session and is
    // never retried automatically.
    bool tick(std::uint64_t now) noexcept {
        if (!active()) return false;
        if (!expired(now)) return true;
        stop(QuickTradeStop::TimedOut);
        return false;
    }
    void cancel() noexcept { stop(QuickTradeStop::Cancelled); }
    // Explicit user stop for any other reason (GUI close, focus loss, ...).
    void stop(QuickTradeStop reason) noexcept {
        if (reason == QuickTradeStop::None) return;
        state_ = reason == QuickTradeStop::TradeLimitReached ? QuickTradeState::Completed : QuickTradeState::Stopped;
        reason_ = reason;
        outstanding_ = false;
        outstandingToken_ = 0;
        granted_ = 0;
    }
    // A new activation starts from a clean slate; the confirmed count is kept
    // so the caller can report it, and is cleared by `reset()`.
    void reset() noexcept {
        state_ = QuickTradeState::Idle;
        reason_ = QuickTradeStop::None;
        context_ = {};
        started_ = deadline_ = 0;
        confirmed_ = granted_ = 0;
        outstanding_ = false;
        outstandingToken_ = 0;
    }
private:
    bool expired(std::uint64_t now) const noexcept { return now < started_ || now >= deadline_; }
    QuickTradeOptions options_{};
    QuickTradeContext context_{};
    QuickTradeState state_{QuickTradeState::Idle};
    QuickTradeStop reason_{QuickTradeStop::None};
    std::uint64_t started_{}, deadline_{};
    int confirmed_{}, granted_{};
    bool outstanding_{};
    std::uint64_t nextToken_{}, outstandingToken_{};
};
} // namespace praxis::companion
