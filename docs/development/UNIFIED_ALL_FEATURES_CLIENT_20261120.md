# Praxis unified client feature test build (2026-11-20)

This **draft integration branch** combines the independently reviewed work from
PR #8 (TabTPS), PR #9 (QuickTrade) and PR #10 (CommandKeys) into one
`PraxisCompanion.dll`. It is **not** an approved release or a merge to main.

## Build verification

- Client target: Minecraft Bedrock 1.26.60.5 / LeviLamina 26.60.0.
- `xmake b PraxisCompanion`: PASS (integrated client DLL).
- Protocol tests: 47 PASS.
- Server sampler tests: 1,280 PASS.
- CommandKeys policy tests: 94 PASS.
- QuickTrade policy tests: 289 PASS.
- Total: **1,710 checks passed**, verifying compile-time and deterministic
  policies, **not** runtime behavior across all Minecraft clients.

## Feature behavior

- **TabTPS:** while holding Tab, shows fresh server TPS in the HUD when
  the optional server counterpart sends `praxis:tps_sample` v1 packets.
  When unavailable or stale, displays dashes instead of fabricated results.
  MSPT now shows measured ServerLevel::_subTick simulation processing time
  (idle waits excluded), independently of TPS. Network/outer-loop overhead
  is not included in this metric.
- **CommandKeys:** process-session default OFF; use the F10 CommandKeys
  page to define macros and explicitly apply/enable them. Supported actions:
  Send, Cycle and Repeat. Command execution is subject to server permissions.
- **QuickTrade:** process-session default OFF; explicitly enable in the
  F10 ItemScroller page, then use Ctrl+Shift+K with confirmation in a valid
  villager trade screen. Each action is bounded and awaits an authoritative
  inventory transaction response. No live trade success is yet proven.

## Verification boundaries

The corresponding **server** TabTPS native mod loaded and enabled successfully
in a separate test BDS 1.26.60.6 / LeviLamina 26.60.0 environment. Logs
confirmed real simulation ticks, TPS ~20 and simulation MSPT 0.672ms
(21 ticks / 1016ms window). This does **not** verify client packet
negotiation, in-game HUD rendering or actual Minecraft player connections.
These still require real-device verification before release.

This branch does not contain personal allowlist entries, game worlds,
machine-specific backups or built executables.
