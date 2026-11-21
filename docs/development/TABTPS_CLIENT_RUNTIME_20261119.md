# TabTPS client integration milestone — 2026-11-19

## Implemented
- Native LeviLamina PacketBase/PacketHandlerBase for praxis:tps_sample v1, Client side, Optional=true.
- Bounded getString(128) and strict PTPS1 validation; no deprecated stream method.
- Network callback updates a generation-scoped, mutex-protected receiver for playing world only.
- Tab-held-only top-right ImGui overlay, with TPS, independently measured MSPT, stale/unavailable feedback.
- Native compile probe and full PraxisCompanion client DLL built without touching the installed game.

## Verified
- Native probe: PASS.
- Protocol test: 45 PASS.
- Server sampler test: 1257 PASS.
- xmake b -j 3 PraxisCompanion: PASS (local DLL and LL mod packer).

## Not implemented or not verified
- No matching deployed BDS-side telemetry plugin exists yet.
- No modded-server/client, vanilla peer or in-game UI test has yet been performed.
- Server lacking the plugin correctly yields unavailable rather than synthetic 20 TPS.
- Missing full-frame timing must render MSPT unavailable, not inverse TPS.
- xmake resolves a pinned LL package despite emitting an unknown-version warning; binary provenance and runtime compatibility require final validation.

## Files
- companion/src/platform/TabTpsClient.cpp
- companion/src/render/TabTpsHud.cpp
- companion/src/core/TabTpsProtocol.h
- companion/src/core/TabTpsServerSampler.h
- Draft PR #8 on feat/tab-tps
