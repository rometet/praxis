# TabTPS optional packet and measurement protocol (v1)

## Platform and invariants
- Client: PraxisCompanion on LL Client 26.60.0. Server: separate native LeviLamina/BDS module, built for the matching server runtime.
- Packet: `ll::network::PacketBase<...>`, `CustomPacketId="praxis:tps_sample"`, `PacketVersion=1`, `Side=PacketSide::Client`, `Optional=true`.
- Server sends ONLY if `packet.canSendTo(player)`. Absence of plugin on either end never disconnects or degrades vanilla gameplay.
- Wire: `PTPS1|epoch|sequence|ticks|windowMs|avgFrameUs`, ASCII, max 128 bytes; `BinaryStream::writeString` and `ReadOnlyBinaryStream::getString(128)` then `tabtps::decode`.
- This payload is versioned independently from LL's `PacketVersion`; bump both when changing the wire format.

## Field definitions
- `epoch`: nonzero uint32 unique per BDS process/session, reset on server restart. Not personally identifying.
- `sequence`: monotonically increasing nonzero uint32 per telemetry emission, never wrapping within a connection.
- `ticks`: completed server simulation ticks during window, 1..1200.
- `windowMs`: elapsed monotonic-clock milliseconds, 250..60000.
- `avgFrameUs`: independently measured mean full-frame server update duration per tick in microseconds, 1..5000000. Full-frame measurement source must be documented.
- UI TPS = min(20.0, ticks*1000/windowMs) to one decimal; catch-up can exceed 20 before display cap. MSPT=avgFrameUs/1000.0 independently. NEVER compute MSPT as 1000/TPS.
- Target emission once per 1000ms with server-side measurement. Reject malformed, duplicate, reordered, oversized, wrong-connection-generation, or changed-epoch packets.

## Client lifecycle
- Each world/session has a local monotonic connection generation. Receiver resets on join, disconnect, world change, shutdown.
- `Unavailable`: no valid packet or wrong generation, NOT inferred 20 TPS.
- `Fresh`: valid packet <=2500ms old; show actual TPS and MSPT.
- `Stale`: >2500ms since last accepted packet, or local monotonic-clock rollback. Display `TPS -- / MSPT --`, NOT old values.
- Never keep pointers from LL network callback on the render thread. Use immutable values and safe handoff.

## Server implementation
- Prefer timing full BDS update (see LeviOptimize `full_frame_timing` diagnostic) rather than only `Level::tick` if displaying **full-frame MSPT**.
- Count completed Level/simulation ticks with monotonic clock. Keep hook/listener low overhead. Emit at most 1Hz on server thread.
- Do not require LeviOptimize unless it becomes an explicit dependency; do not parse chat/actionbar as transport.
- When no trustworthy full-frame timing exists, omit MSPT rather than fabricate it.
- The server sends telemetry only; it never accepts client-provided TPS values.

## Acceptance tests
- Codec: roundtrip, malformed/oversize/overflow input, independent TPS and MSPT.
- Receiver: duplicate/order/epoch/generation/stale, reconnection.
- Integration: modded client + modded BDS, modded client + vanilla server, vanilla client + modded BDS; no disconnections.
- Runtime: 20 TPS baseline, intentional lag, server restart, reconnect, world change, UI hide/show.
- Do not report runtime success without exact binary provenance and logs.

References: https://levimc.org/LeviLamina/api_reference/network/
