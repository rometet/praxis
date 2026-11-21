# QuickTrade UI-event fix and diagnostics — 2026-11-21

## Report and reproduction context
A player tested QuickTrade in the isolated Bedrock 1.26.60.6 BDS
environment and reported that it did not appear to work. The server's own
connection records confirm that the test client connected, but there were
no QuickTrade-specific transaction diagnostics to establish a trade outcome.
Do **not** report server-authoritative trade success without a new live test.

## Code defect found
`ItemScrollerExtras::onFrame` receives `AfterUIRenderEvent` for screens
other than the focused trade view (HUD/background layers). Previously it
passed those events to `quickTradeFrame({}, {}, 0, false, false)`.
That function calls `confirmation.clear()` whenever the context is
ineligible. The background event could therefore discard the first
Ctrl+Shift+K press before the user presses the chord again.

The adapter now processes **only the actual top screen**. Other render
callbacks do not mutate QuickTrade's confirmation, session, or key-edge
state. On an actual top-screen change to non-trade UI, the existing
fail-closed stop/reset semantics are preserved.

## Diagnostics and safety
QuickTrade now reports the specific reason a fresh activation cannot
proceed (unfocused trade GUI, another pending inventory request, no
active trading controller, missing selected recipe, busy cursor, missing
output slot, empty result, mismatched output, or no valid destination).
Only an activation edge or an already-active session examines the native
trade container to avoid unnecessary per-frame calls.

The trade action remains explicitly disabled by default, requires user
confirmation, has a one-trade limit by default and relies on the vanilla
`handleTakeAll` action and a server ItemStackResponse before counting
success. These guards were not weakened. There is no silent fake success,
manual inventory mutation, or repeated action without an acknowledgment.

## Verification
- Unified Windows client DLL build succeeded on the pinned SDK.
- QuickTrade native compile probe succeeded.
- QuickTrade policy unit checks: **298 passed** (9 added view-gating and
  two-press confirmation regression checks).
- Other integration policy checks also passed (total **1,778**).
- Installed DLL and built DLL hashes match, with a rollback backup.
- Actual villager GUI/button, inventory response and result-count
  verification are **not yet proven**; request another test and inspect the
  F10 > ItemScroller > QuickTrade Status message if the attempt fails.
- Existing BDS processes and saved game/configuration files were not changed.

## Suggested live test
1. Open an eligible villager offer with the needed items and free player
   inventory space, so its result is actually obtainable.
2. In F10 > ItemScroller, enable QuickTrade for this session. Keep double
   press enabled and max confirmed trades at 1.
3. Close F10, hold Ctrl+Shift, tap K, release K, tap K again within five
   seconds. Keep the trade UI open and do not click another recipe.
4. Verify the actual output item count and input consumption.
5. Open F10 > ItemScroller and record the exact Status string, even after
   a failure. Do not infer success just from a toast or a successful build.

Note: a separately observed Minecraft process-exit access-violation trace
does not by itself identify QuickTrade as the cause.
