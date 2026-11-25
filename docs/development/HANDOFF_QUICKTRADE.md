# HANDOFF — Mistral DeepSeek: QuickTrade

Repository: https://github.com/rometet/praxis
Base commit: 15be75f19c12523576aada3fbfbfe584eb60e96b
Branch: feat/quick-trade, separated from other agents.
Target: Bedrock for Windows 1.26.60.05, LL Client 26.60.0.

## Mission
Add an explicitly user-triggered **one-click repeat trade** action in the currently open villager trading screen. The user chooses an offer; a click repeats it as permitted. DO NOT automate arbitrary nearby villagers or make unauthorized client-side inventory mutations.

## Existing code to study before edits
- companion/src/platform/ItemScrollerExtras.cpp + .h — current trade observation and favorite/preview; note: existing comment states preview creates NO server transaction.
- companion/src/platform/InventoryRequest.cpp, InventoryActions.cpp, InventorySnapshot.cpp — request, acknowledgment, inventory state.
- companion/src/core/ItemScrollerPolicy.h, InventoryRequestPolicy.h, InventoryActionPolicy.h.
- TradeScreenController, TradeContainerManagerController and MerchantRecipe in pinned LL client headers.

## Required behavior
1. User opens trade GUI, picks desired valid trade, presses one QuickTrade button or binding.
2. Check buying inventory, the selected recipe, available output space and pending inventory requests; explicitly limit max trades per activation.
3. Perform ONE permitted server-authoritative trade, then wait for acknowledgment and refreshed container state; only then issue the next.
4. Stop immediately on GUI close/focus loss, player/world change, villager change, denied/expired offer, inventory full, error, timeout, or explicit cancellation. Show status and confirmed trade count.
5. No guessed transaction packet formats; use the verified existing menu/controller API. If not verified, stop at observational prototype and document missing symbol/API instead of issuing unsafe requests.
6. Default OFF; never auto-trade without a fresh explicit user action. No duplication, negative inventory counts or fast-unacknowledged spam.

## Non-negotiable integration
- Don't reuse or renumber persisted Feature slots (31 entries, 32-bit masks).
- Avoid concurrent editing of GuiBridge.cpp, Native.cpp, Settings.h, xmake.lua; provide minimal proposed integration changes.
- Separate worktree/clone; no direct push to main; PR with tests. Never stop active BDS/Praxis jobs without authorization.
- Reuse existing ItemScroller rather than importing code from a Java Edition mod.

## Tests
Unit tests for max trade count, ack pacing, stale request, stop conditions, wrong world generation, rejection/timeout, changed trade index and inventory full.
Verify build, then provide repeatable gameplay test steps with screenshots/logs only when truly tested.
Report commit SHA, PR, changed files and exact tests. Do not claim item consistency without live game verification.
