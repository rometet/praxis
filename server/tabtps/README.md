# Praxis TabTPS server companion (experimental)

This is the **server-only** LeviLamina C++ companion for the Praxis TabTPS
client feature. A client without this plugin will continue to show
`Server companion unavailable` rather than inventing a TPS value.

## Requirements

- Bedrock Dedicated Server **26.60.6** with matching **LeviLamina 26.60.0 server**.
- A separately installed Praxis client build containing `TabTpsClient.cpp`
  with the optional `praxis:tps_sample` packet registered on the client.
- **Do not** install this server plugin into a regular Minecraft client.

## Build

From the `server/tabtps` directory:

```powershell
xmake f -p windows -a x64 -m release -y
xmake b PraxisTabTpsServer
```

Expected packaged output: `bin/PraxisTabTpsServer/` with
`PraxisTabTpsServer.dll` and `manifest.json`.

Install the entire directory under the dedicated server's LeviLamina
`plugins` directory **only after confirming the installed server runtime
versions match**. Keep it out of active/production BDS until smoke tests pass.

## Data contract

- `PacketId=praxis:tps_sample`, `PacketVersion=1`, receiver `Client`, optional.
- Wire string `PTPS1|epoch|sequence|ticks|windowMs|avgFrameUs`.
- Reports actual observed server tick callbacks per elapsed 1-second window.
- Only dispatches to players whose client advertises the packet capability.
- Random per-enable epoch, monotonic sequence and bounded 128-byte payload.
- `avgFrameUs=0`: **MSPT unavailable**, not a guessed number; authentic MSPT
  needs a verified complete server-frame instrumentation hook.

## Still unverified

Binary builds locally, but a modded BDS startup + live client join +
custom-packet negotiation/render test has not yet been performed. Do not
mark the companion as released until an isolated runtime smoke test passes.

Security: server never processes commands from clients or trusts client
supplied TPS values. It emits telemetry only; no world writes or block edits.
