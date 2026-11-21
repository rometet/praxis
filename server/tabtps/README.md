# Praxis TabTPS Source-Based Server Mod (Bedrock 26.60)

Reference jpenilla/TabTPS @ e0e55e98524643f85aeb0e80bad4411f1a5532a6 (MIT; Copyright 2020-2024 Jason Penilla).
This Windows telemetry and wire protocol is a Bedrock-specific port, not original Java code.

## Requirements

BDS 1.26.60.6 / LeviLamina 26.60.0; xmake on Windows x64 with pinned bedrockdata.
Run xmake b PraxisTabTpsServer in the server/tabtps directory.
Packaged output in bin/PraxisTabTpsServer/ must be installed under server/mods/PraxisTabTpsServer/.
Do not place the server plugin in Minecraft client mods.

## Real measurements (no fabricated data)

- TPS 1m: 60 time-weighted samples of 20 completed tick periods, matching the original Fabric RollingAverage calculation. Java's initial 20TPS seed is disabled: no TPS value until 60 real samples exist.
- MSPT: measured average busy simulation work across the last 100 ServerLevel::_subTick calls (20 minimum). Excludes idle waits and processing outside the world tick.
- Ping: each player's real NetworkPeer::NetworkStatus.mCurrentPing, never local FPS or a guessed ping.
- CPU: system and BDS process CPU percentages using Windows GetSystemTimes / GetProcessTimes, polled at 500ms with 20 nonzero samples.
- RAM: process Private Working Set versus Private Commit from PROCESS_MEMORY_COUNTERS_EX2. This is not the original JVM heap. Process max is unavailable.
- Online: active player count from ServerLevel and max-players parsed from the real server.properties file at startup.

Every unknown value is a -1 sentinel. Zero is reserved for real measured zero.
One 250ms shared snapshot plus each eligible recipient's Ping field is sent using
optional LeviLamina custom packet praxis:tabtps_telemetry v2.
The bounded wire fields are: PTPS2 | epoch | sequence | serverMonotonicMs |
tpsCenti | msptUs | pingMs | cpuSystemCenti | cpuProcessCenti |
memoryUsedMiB | memoryCommittedMiB | memoryMaxMiB | onlinePlayers | maxPlayers.

## Verification status

- The new server native DLL has built successfully.
- The previous isolated BDS build and first source-based v2 plugin loaded;
  its real logs verified 1m TPS after warm-up, MSPT, OS CPU, process memory,
  active players and server player limit.
- A compatible player must still join to prove optional custom packet
  negotiation, per-player Ping, the on-screen source-style rendering,
  reconnect handling and player-list suppression.
- The new 20-tick-weighted 60-slot revision must be redeployed and runtime checked.
- This plugin does not reproduce original Java UUID permissions, commands
  or JVM heap diagnostics. Do not mark those items as implemented.

Do not deploy to production or publish before the live-player acceptance tests.
