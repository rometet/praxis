# Praxis R33: native Minecraft Global Resources hot-reload

**v0.1.3-rc.1 experimental prerelease** — Windows x64 / Minecraft Bedrock 1.26.51.01 / LeviLamina Client 26.51.5.

## User-facing scope

In the exact authorized disposable offline world (directory ID PraxisLevelPackOfflineR17, display name Praxis Level RP Offline Test R17), Minecraft's native Settings → Global Resources can enable or disable **one eligible registered local texture pack** without leaving the world. Other world, server, multiplayer, bulk edit and complex shader/behavior pack changes remain outside the admission policy.

This capability depends on two real native resource participants: Praxis and a separate installed **LHolo with resource ABI v4**. Not all public LHolo builds export lholo_resource_reload_v4. Do not emulate or bypass the missing participant.

## R24–R33 development summary

R24–R28 built a strict offline-clone resource pack catalog, one-pack ON/OFF and native Full Stack, texture and UI reload. R29 integrated Minecraft's native Global Resources editor. R30 moved the LHolo v4 BeginNative transition before the game mutates the Global stack. R31 and R32 added an entity renderer rebuild and a client resource-change notification, but the local player was still invisible after an in-world toggle. R33 added MinecraftGame::reloadAnimationAndGeometryData(true) before the client notification and renderer rebuild, under identical owner/epoch constraints. Bounded camera/perspective diagnostics were added without changing the actual camera state.

## Verified on the offline clone, October 21, 2026

- User confirmed a native Settings resource pack OFF operation completed with their local player still visible in third person.
- Native application finished with UI/loading/FullStack match and progress phase=6, mask=0.
- Praxis and LHolo both resumed into their next resource epoch successfully.
- R33 geometry stage recorded missing_mask=0, geometry_reloaded=1, notified=1, rebuilt=1.
- After returning to gameplay, the diagnostic captured perspective=2, model=1, skin_initialized=1, platform_skin_valid=1.
- This single result does not assert generic ON/OFF validity for all resource packs or real servers.

## Exclusions and known problems

- This is a **prerelease/test feature**, hard-gated to the named offline clone.
- No server-required RP, world-level mutations, behavior packs, arbitrary UI definition packs, scripts, shaders, multiple simultaneous changes or general-purpose world use.
- Compatible LHolo with resource ABI v4 is NOT bundled; separately managed.
- LHolo config-saving warning: replace settings: Access is denied.
- Existing native hook teardown warnings on Minecraft shutdown; avoid live hot-unload of the mod.
- Minecraft, the launcher, user worlds, config, private runtime logs and legacy development history are NOT part of the release.

The combined executable retains its GPL 3 and MPL 2 terms and third-party attributions. See the root licensing documents and SOURCE_BUILD.md.
