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
`mods/PraxisTabTpsServer/` directory (LeviLamina 26.60+ uses `mods/`)
**only after confirming the installed server runtime versions match**.
Keep it out of active/production BDS until smoke tests pass.

## Data contract

- `PacketId=praxis:tps_sample`, `PacketVersion=1`, receiver `Client`, optional.
- Wire string `PTPS1|epoch|sequence|ticks|windowMs|avgFrameUs`.
- Reports actual observed server tick callbacks per elapsed 1-second window.
- Only dispatches to players whose client advertises the packet capability.
- Random per-enable epoch, monotonic sequence and bounded 128-byte payload.
- Unified LL memory operators in `src/MemoryOperators.cpp` are required for
  the server loader to accept this plugin.
- Low-frequency tick sampler heartbeat: first sample and every 30 windows;
  logs TPS/simulation MSPT/ticks/elapsed/eligible client count, never player identities.
- **MSPT is now measured** by surrounding the actual `ServerLevel::_subTick`
  invocation with `std::chrono::steady_clock`. This is the elapsed
  simulation-tick processing time, not the 50 ms tick interval; it does not
  include network work and other tasks outside the level tick.
- `avgFrameUs` holds the mean measured simulation tick duration in microseconds;
  0 means genuinely unavailable, never a fabricated value.
- Both metrics travel together in the optional `praxis:tps_sample` v1 packet,
  without a client protocol version change.

## Still unverified

Server plugin load, enable, and **real MSPT measurement** are verified on the
sub-PC with BDS 1.26.60.6 and LeviLamina 26.60.0. First measured sample:
21 completed ticks in 1016 ms (TPS 20.00) and 0.672 ms average simulation
work per tick; no compatible clients joined yet (eligibleClients=0).
A live client join + custom-packet negotiation/render test has **not yet**
been performed. Do not release until that test passes.

Security: server never processes commands from clients or trusts client
supplied TPS values. It emits telemetry only; no world writes or block edits.
