# TabTPS 26.60 source-based port — complete specification acceptance register

Date: 2026-11-21. Reference: jpenilla/TabTPS at
`e0e55e98524643f85aeb0e80bad4411f1a5532a6` (MIT, Jason Penilla).
Source-derived rules are explicitly separate from Bedrock-specific substitutions.
This is a **draft**; no production rollout or final release authorization.

Status legend: [IMPLEMENTED/UNIT VERIFIED] behavior covered by source and native build;
[SERVER RUNTIME VERIFIED] observed in isolated BDS logs; [CLIENT NOT VERIFIED]
requires live 26.60.5 game; [NOT IMPLEMENTED] no claim of support.

## Section 0 / source-vs-port

- [IMPLEMENTED/UNIT VERIFIED] Source pin, original MIT license and copyright retained.
- [IMPLEMENTED/UNIT VERIFIED] All unknown metrics represented as absent, never
  substituted with 20 TPS, zero ping or dummy CPU/RAM.
- [IMPLEMENTED/UNIT VERIFIED] Java-specific features never described as native
  Bedrock capabilities. Client overlays and optional custom packet are port-only.

## Sections 1–3 / UI, modules, text, thresholds

- [IMPLEMENTED/UNIT VERIFIED] Six exact IDs: tps, mspt, ping, cpu, memory, players.
  Module order follows CSV and invalid names are rejected.
- [IMPLEMENTED/UNIT VERIFIED] Exact original labels
  `TPS:`, `MSPT:`, `Ping:`, `CPU:`, `RAM:`, `Online:`;
  colon is white, label gray, exactly one space before number.
- [IMPLEMENTED/UNIT VERIFIED] Source default Tab header empty, footer
  `tps,mspt`; Action Bar/Boss Bar `tps,mspt,ping`, separator space.
  Display defaults OFF for all three.
- [IMPLEMENTED/UNIT VERIFIED] TPS/MSPT exactly two decimals with `.` locale;
  Ping integer+ms, CPU system/process order and Java-like decimal shortening,
  RAM used/committed/max elision and Online count/max.
- [IMPLEMENTED/UNIT VERIFIED] Eight fixed NamedTextColors, per-character RGB
  linear interpolation using index/length; non-warning green for CPU/RAM/Online.
  Original text thresholds for TPS 18.5/15, MSPT 25/40, Ping 100/250.
- [CLIENT NOT VERIFIED] Runtime use of the ImGui font and position on different
  Bedrock GUI scales. Visual draw order / overlap with other HUDs unverified.
- [NOT IMPLEMENTED] Java native Tab list header/footer modification via
  Bedrock native player list text API. This port renders a separate overlay
  with equivalent header/footer composition, not the original Java packet.
- [NOT IMPLEMENTED] Editable color theme and MiniMessage-formatted separators;
  fixed source default colors and plain separator supported.

## Sections 4, 7.1–7.2 / authoritative collection and protocol

- [IMPLEMENTED/UNIT VERIFIED] TPS via real completed simulation ticks,
  60 time-weighted 20-tick samples matching Fabric RollingAverage math.
  Source's 20-TPS initial seed intentionally omitted; 60 measured periods
  are required before showing any numeric 1m TPS.
- [IMPLEMENTED/UNIT VERIFIED] MSPT from actual completed
  `ServerLevel::_subTick` busy elapsed time averaged across last 100 calls.
  Not inferred from 50ms tick spacing; it excludes surrounding network work.
- [SERVER RUNTIME VERIFIED] Separate BDS 1.26.60.6/LL 26.60.0 loaded
  source-based v2 and emitted real MSPT/CPU/memory/Online/max metrics;
  old 60s rolling method also logged real 20TPS and a safe missing warm-up.
  Newest 20-tick weighted version was runtime-verified after startup: 1mTPS=19.99, simMSPT=0.644ms, system CPU=5.94%, process CPU=0.25%, private working set=276MiB, private commit=359MiB, online=0/2 (zero compatible clients).
- [IMPLEMENTED/NATIVE BUILT] Server per-player ping pulled from
  `Player::getNetworkStatus().mCurrentPing`. Real player connection not
  yet observed, so this remains [CLIENT NOT VERIFIED].
- [IMPLEMENTED/NATIVE BUILT] CPU obtained from Windows system and process
  time counters at most once/500ms, keeping 20 valid nonzero samples.
  Uses server tick callback for scheduled polling, not a worker thread.
- [IMPLEMENTED/NATIVE BUILT] Memory from Windows BDS process private working
  set and private commit (EX2); this is not JVM heap. JVM max is null/absent.
  The prior RSS/private commit pairing was corrected to comparable counters.
- [IMPLEMENTED/UNIT VERIFIED] Online from real active player count;
  maximum read from the test BDS server.properties at enable.
- [IMPLEMENTED/UNIT VERIFIED] Optional `praxis:tabtps_telemetry` custom packet
  v2 with epoch/sequence/monotonic timestamp/strict bounded codec and -1
  sentinel for unmeasured fields. One shared 250ms server snapshot plus
  per-recipient Ping. Client cache expires after 3s and clears on generation
  change. Compatible-client packet negotiation is [CLIENT NOT VERIFIED].
- [CLIENT NOT VERIFIED] 25 concurrent players load/stress and per-player
  bandwidth under actual connections; no extrapolated performance claims.

## Sections 5–6, 7.3–7.5 / display modes, controls, lifecycle

- [IMPLEMENTED/UNIT VERIFIED] Independent Tab, Action Bar and Boss Bar opt-in
  settings. The latter two use **non-destructive Praxis ImGui overlays**, not
  Minecraft's native actionbar or server bossbar packet (port-specific).
- [IMPLEMENTED/UNIT VERIFIED] Boss fill mode formulas:
  `clamp(mspt / 50,0,1)`, and TPS `clamp(tps / 20,0,1)`, plus reverse
  modes. PROGRESS/notched 6/10/12/20 segments. Boundary differences between
  text and boss colors are asserted in acceptance tests.
- [IMPLEMENTED/UNIT VERIFIED] Tab overlay shown while VK_TAB held, removed
  on key release; game foreground and UI focus guards, no key-repeat toggle.
- [IMPLEMENTED/NATIVE BUILT] `KeyInputEvent` cancellation for
  performance-only Tab mode, suppresses normal Tab key propagation when
  enabled. Actual vanilla player list hiding is [CLIENT NOT VERIFIED].
- [IMPLEMENTED/NATIVE BUILT] F10 TabTPS page, module CSV editing, independent
  opt-in toggles, boss fill/notches, separator and HUD offset/scale.
  Local `tabtps-display-v2.ini` persists changes using temporary write +
  atomic replace. Per-player UUID-level server persistence is absent.
- [IMPLEMENTED/UNIT VERIFIED] Unknown/stale/server absent shows `--`, not
  last cached values; generation mismatch disallows prior server metrics.
- [CLIENT NOT VERIFIED] Cross-server transitions, reconnect with network
  packet loss, GUI focus/other HUD compatibility, native key cancellation.
- [NOT IMPLEMENTED] Java permission-priority display configuration, server
  UUID JSON userdata, `enableOnLogin` server policy, Java `/tabtps toggle`
  command set and related /tickinfo, /memory, /pingall, /reload aliases.
  Local F10 toggles/persistence are Bedrock substitute.
- [NOT IMPLEMENTED] Java JVM heap pools or user-configurable JVM Xmx;
  no misleading native substitute.

## Section 8 / acceptance test results

- [UNIT VERIFIED] TPS thresholds 20,18.50,18.49,15.01,15; MSPT 25,
  25.01,40,40.01; Ping 99,100,249,250.
- [UNIT VERIFIED] CPU 18.32/7.85, RAM max shown/hidden, Online 3/20,
  exact `TPS: 20.00 MSPT: 4.25`, header empty, separator, per-char gradient.
- [UNIT VERIFIED] Boss MSPT=25 yellow, MSPT=40 red, TPS=18.5 yellow,
  MSPT=10 yields 20% bar fill.
- [UNIT VERIFIED] Ordered custom modules, unknown metrics, repeated / reordered
  packet, stale expiry 3s, reset on connection generation, no TPS warm-start.
- [UNIT VERIFIED] 59 dedicated source-port assertions passed; existing
  deterministic tests: 47 original protocol, 1280 sampler, 94 CommandKeys,
  289 QuickTrade = 1769 checks in all, all passing.
- [NATIVE BUILD VERIFIED] Unified client DLL built with TabTPS,
  CommandKeys and QuickTrade. Standalone LeviLamina 26.60 BDS plugin built.
- [SERVER RUNTIME VERIFIED] Only isolated BDS loading, measurement logs and
  zero connected clients; all real GUI, packet, ping and multiple-user tests
  remain [CLIENT NOT VERIFIED].
- [CLIENT NOT VERIFIED] Actual gameplay packet, rendering, Tab suppression,
  independent HUDs, disconnect, 25-player load, other-server compatibility.

## Operational isolation

- Main Minecraft client may be running. Never replace a loaded client DLL.
- Update the 26.60.5 installed client only after process-exit and safe hash
  backup; never touch any other gameplay mods.
- Sub-PC standalone BDS test only on port 19160; do not touch BDS 19150
  or other projects. No private worlds, Xbox player identifiers or binaries
  belong in public source control.
- Remain draft/unreleased until authorized-player live acceptance tests pass.

## HUD layout regression: transparent Java-style rendering (2026-11-21)

User live screenshots confirmed a previous source-style client build could
show all telemetry but rendered black rectangular background panels, a
bottom Action Bar over hotbar slots, and no native player rows on Tab.
Those are real client visual bugs, not telemetry measurement failures.

- Bedrock-specific UI fix: render gradient glyphs directly to the ImGui
  foreground draw list, using identical per-glyph advance calculation for
  both text width and positions. Clamp text within the viewport, auto-fit
  very long module lists, and apply a one-pixel shadow for readability.
  Text has no dark rectangular background and captures no keyboard input.
- Tab: restore the existing Deesse pack's *pre-deletion* player list binding
  from the verified local backup. Opt out of the earlier vanilla Tab
  suppression with `hidePlayers=0` (default for new users is now false).
  Telemetry stays on an independent Bedrock overlay below the original
  360-pixel-tall player-list layout, rather than pretending to be Java's
  actual network-sent player-list footer.
- Action Bar: transparent text is positioned above the native hotbar
  and health indicators instead of over the inventory slots.
- Boss Bar: original color, fill and 20-notch math remains intact.
  Its overlay is hidden temporarily while Tab is held to avoid covering
  the native player list, then resumes when Tab is released.
- All earlier user toggles and module CSV values remain saved. For this
  authorized local installation only, hidePlayers was changed to zero
  and fontScale to 1.25. Installed binaries and pack file were swapped
  atomically with rollback backups. No unrelated resource-pack content
  or BDS files were changed.

Verification: combined Windows client release DLL built successfully;
all 1,769 deterministic policy/source-port checks and the three native
compile probes passed. Local installed DLL SHA256 matches the build.
The resource pack backup/current comparison proved only a single
101-character missing player-list binding, which was precisely restored.

**Still requires live client recheck after THIS revision**: visible native
player list while holding Tab, unclipped translucent-free TPS/MSPT text,
Action Bar clear of the hotbar, boss bar resume on Tab release, and no
side effects to other HUD mods. Do not mark visual acceptance passed
until that screenshot/run has been inspected.