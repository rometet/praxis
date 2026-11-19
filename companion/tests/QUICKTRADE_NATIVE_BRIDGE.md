# QuickTrade native bridge (experimental, not released)

Implements a session-only (default OFF) QuickTrade path on the ItemScroller sidebar.
Once explicitly enabled in F10, open a villager and press Ctrl+Shift+K twice.
The code checks a live TradeScreenController, game focus, cursor, output source
(TradeContainerManagerController::mCreatedItemOutputSlot), and a compatible
player inventory destination. It calls vanilla ContainerManagerController::handleTakeAll
once at a time, behind the existing beginRequest/finishRequest request barrier.
Only accepted ItemStackResponse records followed by a destination stack delta
can acknowledge the single-flight QuickTradeSession. A timed-out, rejected,
untracked or changed context cancels without automatic retry.

Configuration is intentionally not persisted; each game process starts OFF.
The physical key gesture is hardcoded Ctrl+Shift+K for the prototype.
Unknown container layouts fail closed, including a missing cursor collection.
Max trade attempts are bounded to 1..64 and a 60-second session window.

Unverified: LL client 26.60 on an actual villager with actual inventory
response IDs. SDK API visibility does not guarantee that the controller
routes its output through request responses on every server. No live trade
execution has been observed; leave PR draft and do not release until verified.
Potential compatibility and safety issue: native controller may expose
preview-only result items that do not appear in getItemStack(output),
in which case this prototype refuses to act, by design.

Main-PC xmake native probe: PASS; policy checks: 289 PASS; full client PraxisCompanion.dll build: PASS (2026-11-19).
Sub-PC xmake update failed due blocked access to gitlab.com; do not disable
package verification or edit the user's active Minecraft/BDS runtime.
