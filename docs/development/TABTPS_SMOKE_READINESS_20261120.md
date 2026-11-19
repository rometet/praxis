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

## Next actual runtime verification (not performed)

1. Obtain/confirm explicit acceptance of Minecraft's EULA and Microsoft's
   Privacy Policy before running lip's non-dry-run `setup`. Do not
   silently use `--accept-eula`.
2. Complete setup only under the isolated test folder, not under the live
   `E:\LHolo-BDS-Throughput-Lab-20261113\server` installation.
3. Change the disposable `server.properties` network ports to otherwise unused
   ports, and preserve online authentication; never collide with the active BDS.
4. Install `server/tabtps/bin/PraxisTabTpsServer` as
   `server/mods/PraxisTabTpsServer/` and start only the disposable BDS.
5. Verify plugin load, `PacketBase` optional capability negotiation, no
   disconnection for an unmodded client, and a compatible 26.60.5 client
   showing real TPS while holding Tab.
6. Recheck server average TPS under induced load and stale handling after
   a world disconnect. MSPT must remain unavailable until a verified
   *complete* tick-duration measurement path exists.

DO NOT release or mark gameplay verified on the strength of the DLL builds alone.
