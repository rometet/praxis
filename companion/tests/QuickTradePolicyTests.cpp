#include "core/QuickTradePolicy.h"
#include "core/ItemScrollerPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
namespace {
QuickTradeContext context(std::uint64_t generation = 7, std::uint64_t offer = 3, int index = 2) {
    QuickTradeContext c;
    c.generation = generation;
    c.offerGeneration = offer;
    c.tradeIndex = index;
    c.screenOpen = c.offerValid = c.inventoryValid = c.outputSpace = true;
    return c;
}
QuickTradeOptions options(int maxTrades = 4, std::uint64_t timeoutMs = 2000) {
    QuickTradeOptions o;
    o.enabled = true;
    o.maxTrades = maxTrades;
    o.timeoutMs = timeoutMs;
    return o;
}
} // namespace
int main() {
    unsigned checks = 0;
    auto require = [&](bool value, char const* label) { ++checks; if (!value) throw std::runtime_error(label); };
    try {
        // Only the top rendered UI may reset an active trade confirmation.
        require(quickTradeTopViewEvent("trade_screen", "trade_screen"), "top trade is processed");
        require(!quickTradeTopViewEvent("hud_screen", "trade_screen"), "HUD background ignored");
        require(!quickTradeTopViewEvent("inventory_screen", "trade_screen"), "other background ignored");
        require(!quickTradeTopViewEvent("", "trade_screen"), "empty rendered screen ignored");
        require(!quickTradeTopViewEvent("", ""), "empty top screen ignored");
        require(quickTradeTopViewEvent("hud_screen", "hud_screen"), "actual top HUD permits context reset");
        {
            ConfirmationState confirmation;
            require(!confirmation.press(1000), "first press arms confirmation");
            require(!quickTradeTopViewEvent("hud_screen", "trade_screen"), "intervening HUD ignored");
            require(confirmation.press(1200), "second press still confirms after HUD render");
        }
        // Defaults are OFF and bounded.
        QuickTradeOptions defaults;
        require(!defaults.enabled && defaults.requireConfirmation, "default OFF with confirmation gate");
        require(defaults.maxTrades == quickTradeMinimumTrades && defaults.timeoutMs == quickTradeDefaultTimeoutMs, "default bounds");
        require(validQuickTradeOptions(defaults), "default options valid");
        for (int bound : {0, 65, -1, 1000}) {
            auto o = options(); o.maxTrades = bound;
            require(!validQuickTradeOptions(o), "max trade bound rejected");
        }
        for (int bound : {1, 2, 63, 64}) {
            auto o = options(); o.maxTrades = bound;
            require(validQuickTradeOptions(o), "max trade bound accepted");
        }
        { auto o = options(); o.timeoutMs = 0; require(!validQuickTradeOptions(o), "zero timeout rejected"); }
        { auto o = options(); o.timeoutMs = 60001; require(!validQuickTradeOptions(o), "oversized timeout rejected"); }

        // Disabled feature never starts.
        {
            QuickTradeSession session;
            require(session.state() == QuickTradeState::Idle, "idle by default");
            require(!session.begin(context(), 0, true), "disabled session refuses activation");
            require(!session.beginTrade(0), "disabled session grants nothing");
            require(session.confirmedTrades() == 0, "no confirmed trades");
        }
        // Confirmation gate.
        {
            QuickTradeSession session(options());
            require(!session.begin(context(), 0, false), "unconfirmed activation refused");
            require(session.state() == QuickTradeState::Idle, "still idle after refusal");
            require(session.begin(context(), 0, true), "confirmed activation accepted");
            require(session.state() == QuickTradeState::Begin, "Begin state entered");
            require(session.active() && !session.awaitingAck(), "Begin is active but not awaiting");
        }
        // Single outstanding request and ack pacing.
        {
            QuickTradeSession session(options(3));
            require(session.begin(context(), 0, true), "begin");
            require(session.beginTrade(0), "first trade granted");
            require(session.state() == QuickTradeState::WaitingAck, "WaitingAck after grant");
            require(!session.beginTrade(0), "second grant refused while outstanding");
            require(session.grantedTrades() == 1, "exactly one grant");
            auto firstToken = session.outstandingToken();
            require(firstToken != 0, "unique transaction token");
            require(session.acknowledge(7, firstToken, 0, true), "ack accepted");
            require(session.confirmedTrades() == 1, "one confirmed trade");
            require(session.state() == QuickTradeState::Begin, "back to Begin after ack");
            require(!session.acknowledge(7, firstToken, 0, true), "duplicate ack ignored");
            require(session.confirmedTrades() == 1, "duplicate ack not counted");
            require(session.beginTrade(0), "second trade granted");
            auto secondToken = session.outstandingToken();
            require(firstToken != secondToken && secondToken != 0, "new transaction token");
            require(!session.acknowledge(7, firstToken, 0, true), "late first ack cannot authorize second trade");
            require(session.confirmedTrades() == 1 && session.awaitingAck(), "still waiting for second receipt");
            require(session.acknowledge(7, secondToken, 0, true), "second ack");
            require(session.confirmedTrades() == 2, "two confirmed trades");
            require(session.beginTrade(0), "third trade granted");
            require(session.grantedTrades() == 3, "three grants issued");
            require(session.confirmedTrades() == 2, "two confirmed before final ack");
            // Final acknowledgment succeeds and transitions to Completed.
            require(session.acknowledge(7, session.outstandingToken(), 0, true), "third ack completes");
            require(session.state() == QuickTradeState::Completed, "Completed at bound");
            require(session.stopReason() == QuickTradeStop::TradeLimitReached, "limit reason");
            require(session.confirmedTrades() == 3, "confirmed count equals bound");
            require(session.grantedTrades() == 0, "grant counter cleared on stop");
            require(!session.beginTrade(0), "no grant after completion");
            require(session.begin(context(), 0, true), "completed session re-arms on fresh activation");
            require(session.state() == QuickTradeState::Begin, "re-armed to Begin");
            require(session.confirmedTrades() == 0, "new activation has a fresh trade budget");
            require(session.beginTrade(0), "fresh activation can trade again");
            require(session.acknowledge(7, session.outstandingToken(), 0, true), "fresh activation acknowledgement");
            require(session.confirmedTrades() == 1, "fresh activation counts new trades");
            session.reset();
            require(session.state() == QuickTradeState::Idle && session.confirmedTrades() == 0, "reset clears");
            require(session.stopReason() == QuickTradeStop::None, "reset clears reason");
        }
        // Maximum bound of 64 is honoured exactly.
        {
            QuickTradeSession session(options(quickTradeMaximumTrades));
            require(session.begin(context(), 0, true), "begin at 64");
            for (int i = 0; i < quickTradeMaximumTrades; ++i) {
                require(session.beginTrade(0), "grant within 64");
                bool const last = i + 1 == quickTradeMaximumTrades;
                require(session.acknowledge(7, session.outstandingToken(), 0, true), "ack within 64");
                if (last) require(session.state() == QuickTradeState::Completed, "completed on 64th ack");
            }
            require(session.confirmedTrades() == 64, "64 confirmed trades");
            require(session.state() == QuickTradeState::Completed, "completed at 64");
            require(!session.beginTrade(0), "no 65th trade");
            require(session.stopReason() == QuickTradeStop::TradeLimitReached, "64 limit reason");
        }
        // Stale generation ack is ignored, and a changed context stops.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(7), 0, true), "begin gen 7");
            require(session.beginTrade(0), "grant gen 7");
            require(!session.acknowledge(8, session.outstandingToken(), 0, true), "stale generation ack ignored");
            require(session.confirmedTrades() == 0 && session.awaitingAck(), "still awaiting after stale ack");
            require(!session.observe(context(8), 0), "world generation change stops");
            require(session.stopReason() == QuickTradeStop::ContextChanged, "context changed reason");
            require(session.state() == QuickTradeState::Stopped, "stopped state");
            require(!session.beginTrade(0), "no grant after stop");
            require(!session.acknowledge(7, session.outstandingToken(), 0, true), "late ack after stop ignored");
            require(session.confirmedTrades() == 0, "stopped session confirms nothing");
        }
        // Rejection stops immediately.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            require(session.beginTrade(0), "grant");
            require(!session.acknowledge(7, session.outstandingToken(), 0, false), "rejection reported");
            require(session.stopReason() == QuickTradeStop::Rejected, "rejected reason");
            require(session.state() == QuickTradeState::Stopped, "stopped on reject");
            require(session.confirmedTrades() == 0, "rejected trade not confirmed");
            require(!session.beginTrade(0), "no grant after reject");
            require(!session.acknowledge(7, session.outstandingToken(), 0, true), "ack after reject ignored");
        }
        // Timeout stops and is fail-closed.
        {
            QuickTradeSession session(options(8, 500));
            require(session.begin(context(), 1000, true), "begin at 1000");
            require(session.beginTrade(1000), "grant at 1000");
            require(session.tick(1499), "still within timeout");
            require(!session.tick(1500), "timeout at exact deadline");
            require(session.stopReason() == QuickTradeStop::TimedOut, "timeout reason");
            require(!session.acknowledge(7, session.outstandingToken(), 1500, true), "ack after timeout ignored");
            require(session.confirmedTrades() == 0, "timed out trade not confirmed");
        }
        {
            QuickTradeSession session(options(8, 500));
            require(session.begin(context(), 1000, true), "begin");
            require(session.beginTrade(1000), "grant");
            require(!session.acknowledge(7, session.outstandingToken(), 1500, true), "late ack times out");
            require(session.stopReason() == QuickTradeStop::TimedOut, "late ack timeout reason");
            require(session.confirmedTrades() == 0, "late ack not confirmed");
        }
        {
            QuickTradeSession session(options(8, 500));
            require(session.begin(context(), 1000, true), "begin");
            require(!session.beginTrade(1500), "expired grant refused");
            require(session.stopReason() == QuickTradeStop::TimedOut, "expired grant timeout");
            require(session.grantedTrades() == 0, "expired grant issues nothing");
        }
        // Clock reversal is treated as expiry, never as fresh time.
        {
            QuickTradeSession session(options(8, 500));
            require(session.begin(context(), 1000, true), "begin");
            require(!session.tick(999), "clock reversal stops");
            require(session.stopReason() == QuickTradeStop::TimedOut, "reversal timeout reason");
            require(!session.beginTrade(1000), "reversed clock grants nothing");
        }
        // Offer and inventory invalidation between trades.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            require(session.beginTrade(0) && session.acknowledge(7, session.outstandingToken(), 0, true), "one trade");
            auto changedOffer = context(); changedOffer.offerGeneration = 4;
            require(!session.observe(changedOffer, 0), "offer generation change stops");
            require(session.stopReason() == QuickTradeStop::OfferInvalidated, "offer reason");
            require(session.confirmedTrades() == 1, "confirmed trade kept after stop");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto changedIndex = context(); changedIndex.tradeIndex = 5;
            require(!session.observe(changedIndex, 0), "trade index change stops");
            require(session.stopReason() == QuickTradeStop::OfferInvalidated, "index reason");
            require(!session.beginTrade(0), "changed trade index blocks the next trade");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto closed = context(); closed.screenOpen = false;
            require(!session.observe(closed, 0), "GUI close stops");
            require(session.stopReason() == QuickTradeStop::OfferInvalidated, "close reason");
            require(session.state() == QuickTradeState::Stopped, "GUI close stops the session");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto denied = context(); denied.offerValid = false;
            require(!session.observe(denied, 0), "denied offer stops");
            require(session.stopReason() == QuickTradeStop::OfferInvalidated, "denied reason");
            require(!session.beginTrade(0), "denied offer blocks the next trade");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto full = context(); full.outputSpace = false;
            require(!session.observe(full, 0), "full output stops");
            require(session.stopReason() == QuickTradeStop::InventoryInvalidated, "full reason");
            require(!session.beginTrade(0), "full inventory blocks the next trade");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto stale = context(); stale.inventoryValid = false;
            require(!session.observe(stale, 0), "invalid inventory stops");
            require(session.stopReason() == QuickTradeStop::InventoryInvalidated, "inventory reason");
            require(session.state() == QuickTradeState::Stopped, "invalid inventory stops the session");
        }
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto pending = context(); pending.pendingInventoryRequest = true;
            require(!session.observe(pending, 0), "pending inventory request stops");
            require(session.stopReason() == QuickTradeStop::InventoryInvalidated, "pending reason");
            require(!session.beginTrade(0), "pending request blocks the next trade");
        }
        // Unusable contexts are refused at activation.
        {
            QuickTradeSession session(options(8));
            auto zeroGeneration = context(0);
            require(!session.begin(zeroGeneration, 0, true), "zero generation refused");
            auto zeroOffer = context(7, 0);
            require(!session.begin(zeroOffer, 0, true), "zero offer generation refused");
            auto negativeIndex = context(7, 3, -1);
            require(!session.begin(negativeIndex, 0, true), "negative trade index refused");
            auto pending = context(); pending.pendingInventoryRequest = true;
            require(!session.begin(pending, 0, true), "pending request refused");
            require(session.state() == QuickTradeState::Idle, "refusals leave idle");
            require(session.stopReason() == QuickTradeStop::None, "refusals set no stop reason");
        }
        // Explicit cancellation.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            require(session.beginTrade(0), "grant");
            session.cancel();
            require(session.state() == QuickTradeState::Stopped, "cancel stops");
            require(session.stopReason() == QuickTradeStop::Cancelled, "cancel reason");
            require(!session.acknowledge(7, session.outstandingToken(), 0, true), "ack after cancel ignored");
            require(!session.beginTrade(0), "grant after cancel refused");
            session.cancel();
            require(session.stopReason() == QuickTradeStop::Cancelled, "repeat cancel is stable");
            require(session.confirmedTrades() == 0, "cancelled trade not confirmed");
        }
        // Reconfiguration is a hard stop; invalid options are refused.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            auto invalid = options(0);
            require(!session.configure(invalid), "invalid options refused");
            require(session.active(), "invalid configure keeps session");
            require(session.configure(options(2)), "valid configure accepted");
            require(session.state() == QuickTradeState::Stopped, "configure stops session");
            require(session.options().maxTrades == 2, "options applied");
            require(!session.beginTrade(0), "no grant after reconfiguration");
        }
        // A second activation while active is refused.
        {
            QuickTradeSession session(options(8));
            require(session.begin(context(), 0, true), "begin");
            require(!session.begin(context(), 0, true), "double activation refused");
            require(session.remainingTrades() == 8, "remaining trades reported");
            require(session.generation() == 7 && session.offerGeneration() == 3 && session.tradeIndex() == 2, "context echoed");
        }
        // A session that never started ignores observations and ticks.
        {
            QuickTradeSession session(options(8));
            require(!session.observe(context(), 0), "idle session ignores observation");
            require(!session.tick(0), "idle session ignores tick");
            require(!session.acknowledge(7, session.outstandingToken(), 0, true), "idle session ignores ack");
            require(session.state() == QuickTradeState::Idle, "idle session stays idle");
            require(session.stopReason() == QuickTradeStop::None, "idle session has no stop reason");
            require(!session.active() && !session.awaitingAck(), "idle session is inactive");
            require(session.remainingTrades() == 8, "idle session reports full budget");
        }
        std::cout << checks << " QuickTradePolicy checks passed\n";
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
