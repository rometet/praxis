# TabTPS isolated runtime smoke-test readiness — 2026-11-20

## Verified

- Client-side LL 26.60.x `PraxisCompanion.dll` built with optional TabTPS packet/HUD.
- Server-only `PraxisTabTpsServer.dll` built against LL **26.60.0 server**
  and `bedrockdata v26.60.6-server.5`. Its package manifest is `platform: server`.
- Shared protocol tests: 45 PASS; server tick-sampling policy: 1257 PASS.
- Client + server packet IDs and versions are now the same shared C++ constants.
- Active existing BDS is **1.26.63.1** and must not be interrupted or
  used to test the 1.26.60.6 server plugin.
- Separate folder prepared at `E:\Praxis-TabTPS-Smoke-20261120`.
- Official lip 0.41.1 portable ZIP downloaded and SHA256-verified:
  `ddfa736dbf543b5f542d9a35f501cd6a9d549722b89933b93c786c2c54d91165`.
- `lip setup --game 1.26.60 --loader levilamina@26.60.0 --dry-run`
  completed successfully. It resolves official **BDS 1.26.60.6** and
  `bedrock-runtime-data 26.60.6-server.5`, and does not change the server folder.

## Sub-PC BDS runtime smoke test (2026-11-20, verified)

- With the user's express EULA/privacy consent, installed an isolated BDS
  **1.26.60.6** and **LeviLamina 26.60.0** using official lip 0.41.1.
- Isolated host folder:
  `C:\Dev\Praxis-TabTPS-BDS-Smoke-20261120\bds` (sub-PC).
  Source clone: `C:\Dev\Praxis-TabTPS-BDS-Smoke-20261120\source`.
- Dedicated server ports: UDP 19160 (with v6 field 19161);
  `online-mode=true`, `allow-list=true`, LAN discovery disabled.
  No existing sub-PC BDS process, world or ports were modified.
- First load attempt was correctly refused by LeviLamina because the standalone
  server plugin lacked unified memory operators. Added
  `server/tabtps/src/MemoryOperators.cpp` with `LL_MEMORY_OPERATORS`,
  registered it in the xmake target, rebuilt, then restarted only the new BDS.
- Loader then confirmed `PraxisTabTpsServer v0.1.0` loaded and enabled;
  BDS confirmed `Server started` and `Accepting clients on [::]:19160`.
- Added a bounded measurement heartbeat (first window, then every 30)
  with no player identity logging. Real log from the isolated BDS:
  `TabTPS window: observed ticks=21, elapsedMs=1031, TPS=20.00, eligibleClients=0`.
  This verifies the server tick event -> sampler -> validated sample pipeline,
  but **not** client packet delivery.
- Two previously active sub-PC BDS processes remained running unchanged.

## Remaining before release

1. Allowlist the authorized user's Xbox gamertag (currently the list is empty)
   and verify network access to the isolated UDP 19160 server.
2. Start a compatible Minecraft Bedrock 1.26.60.5 client with the matching
   Praxis Companion mod and verify the optional custom packet handshake.
3. Hold Tab to confirm real server TPS, stale/connection reset, and behavior
   on unmodded clients, including server load and reconnect tests.
4. MSPT stays unavailable until verified full server-frame instrumentation.

Do not call the feature end-to-end tested or make a release until client
negotiation, gameplay rendering, and disconnect checks have passed.
