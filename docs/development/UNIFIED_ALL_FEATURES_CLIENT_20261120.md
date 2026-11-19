# Praxis unified client feature test build (2026-11-20)

This **draft integration branch** combines the independently reviewed work from
PR #8 (TabTPS), PR #9 (QuickTrade) and PR #10 (CommandKeys) into one
`PraxisCompanion.dll`. It is **not** an approved release or a merge to main.

## Build verification

- Client target: Minecraft Bedrock 1.26.60.5 / LeviLamina 26.60.0.
- `xmake b PraxisCompanion`: PASS (integrated client DLL).
- Protocol tests: 45 PASS.
- Server sampler tests: 1,257 PASS.
- CommandKeys policy tests: 94 PASS.
- QuickTrade policy tests: 289 PASS.
- Total: **1,685 checks passed**, verifying compile-time and deterministic
  policies, **not** runtime behavior across all Minecraft clients.

## Feature behavior

- **TabTPS:** while holding Tab, shows fresh server TPS in the HUD when
  the optional server counterpart sends `praxis:tps_sample` v1 packets.
  When unavailable or stale, displays dashes instead of fabricated results.
  MSPT remains unavailable until full server frame timing can be measured.
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
confirmed observed simulation ticks, elapsed windows and TPS ~20. This does
**not** verify client packet negotiation, on-screen HUD rendering or
Minecraft server connections. These remain required before making a release.

This branch does not contain personal allowlist entries, game worlds,
machine-specific backups or built executables.
