# QuickTrade policy-only status

- This branch currently contains a C++20 trading state machine and tests. It cannot send real vanilla trades and is not ready for Minecraft runtime testing.
- Every action requires a fresh user activation, a usable observed villager offer/inventory and an explicit max trades limit (1..64).
- The policy grants at most one outstanding trade at a time. Actual platform integration must map the native `InventoryRequest` transaction receipt to the session's `outstandingToken` and must NEVER derive confirmation only from screen animation, time passed, or local inventory assumptions.
- The adapter must reject context generation/offer changes and avoid a second grant until the previous authoritative receipt was accepted and a refreshed inventory/offer state was observed.
- These tests do not prove native BDS inventory or trade consistency. Next step: verified TradeScreenController API adapter plus separate manual testing.
