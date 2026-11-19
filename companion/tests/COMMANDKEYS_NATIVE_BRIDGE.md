# CommandKeys native runtime milestone (experimental)

## Implemented
- Optional native Minecraft client-level tick and exit listeners.
- Explicit F10 > CommandKeys sidebar page with up to 32 editable macros.
- Vanilla game command requests and chat packets from the actual local player.
- One action per scheduler tick; Send, Cycle, Repeat modes; bounds on payloads and delays.
- Physical key edge detector with exact modifier matching and release cancellation.
- Fail-closed checks for active world, valid local player, gameplay focus and hidden menu.
- Macro profiles only become active after Apply and ENABLE; process defaults to empty/OFF.
- World exit and native module stop cancel pending actions.

## Limitations
- Profile is session-only and is NOT persisted to ConfigStorage yet.
- Type-to-chat mode is rejected; no simulated keystrokes or fake chat edits.
- Network packets are constructed via pinned SDK wrappers, not guessed byte payloads.
- No game-client run, server command execution confirmation or third-party server
  permissions validation has yet occurred; leave PR draft, do not release.
- UI key input takes virtual-key integer codes. F10/F11 and non-alphanumeric
  keys are rejected by the runtime until safe conflict resolution is designed.
- A server may reject commands for lack of permission; the sender reports only
  'packet sent', not a confirmed successful command.

## Verification
- Native object compile target: PraxisCompanionCommandKeysNativeProbe.
- Policy test target: PraxisCompanionCommandKeysPolicyTests (94 PASS).
- Full xmake release PraxisCompanion.dll: PASS on pinned LL client SDK (2026-11-19).

## Sources
- companion/src/platform/CommandKeysRuntime.cpp
- companion/src/gui/CommandKeysPage.cpp
- companion/src/core/CommandKeysPolicy.h
