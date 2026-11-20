# TabTPS isolated runtime and MSPT verification — 2026-11-20

## Build and version constraints

- Client: Minecraft Bedrock 1.26.60.5, LeviLamina 26.60.0.
- Isolated sub-PC server: official BDS **1.26.60.6**, LeviLamina **26.60.0**,
  bedrockdata **v26.60.6-server.5**.
- Server plugin: `server/tabtps/bin/PraxisTabTpsServer/PraxisTabTpsServer.dll`.
- The same optional `praxis:tps_sample` packet ID, version 1, and
  `PTPS1|epoch|sequence|ticks|windowMs|avgFrameUs` codec are used on both sides.
  Wire compatibility was maintained while adding measured MSPT.
- Existing main-PC production/development BDS is independent and must not be touched.

## Isolated server verification

- The user confirmed the EULA/privacy acceptance and approved a **sub-PC**
  test BDS; setup was performed with official lip 0.41.1.
- Server test folder:
  `C:\Dev\Praxis-TabTPS-BDS-Smoke-20261120\bds`.
- Dedicated server uses port **19160**, `online-mode=true`,
  `allow-list=true` (authorized test player registered privately),
  `enable-lan-visibility=false`.
- Tailscale address on sub-PC **100.112.68.118**; main-PC TCP connection to
  the server port worked. This does not establish a successful game handshake.
- A standalone server plugin initially missed LeviLamina's unified memory
  operators; fixed with `server/tabtps/src/MemoryOperators.cpp`.
- Actual loader logs confirmed successful plugin load, enable, and BDS startup.
  Two previously running sub-PC BDS processes were not modified.
- Initial iteration sampled only TPS and correctly left MSPT unavailable.
  Attempting to hook `ServerInstance::$onTick` failed to link on the pinned
  BDS SDK, so that unsupported instrumentation path was **not shipped**.
- **Measured simulation MSPT** now wraps the actual virtual
  `ServerLevel::$_subTick` function with `std::chrono::steady_clock` before
  and after calling `origin()`. This excludes idle time between ticks,
  counts only completed simulation ticks, and records their real durations.
  It is scoped to the level simulation work, **not full outer BDS frame time**
  (e.g. network operations occurring outside that function).
- Live sample from the isolated BDS log:
  `TabTPS window: ticks=21, windowMs=1016, TPS=20.00, simulationMSPT=0.672ms, measured=true, eligibleClients=0, totalTicks=21`.
- Both metrics are produced by the same bounded 1-second sampler.
  An unavailable/invalid duration is encoded as zero, not inferred from TPS.

## Test requirements before release

1. Rebuild and install the unified PraxisCompanion client DLL containing
   TabTPS, CommandKeys and QuickTrade; keep previous DLL as rollback backup.
2. Re-run the transport protocol, sampler, macro policy and trade policy
   tests, plus native compile probes for the combined client.
3. Connect one authorized client to the isolated BDS, verify optional packet
   negotiation and correct TPS/MSPT rendering while holding Tab.
4. Verify the HUD clears on disconnect, does not display stale values,
   and does not crash when connecting to a server without the plugin.
5. Verify CommandKeys and QuickTrade in their real UIs separately.
   These features default OFF; compile/unit checks are not gameplay proof.

Do not merge or release the integration PR until real-client tests succeed.
