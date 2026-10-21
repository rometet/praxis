# Praxis Client / PraxisCompanion

Minecraft Bedrock **1.26.51.01** · Windows x64 · LeviLamina Client **26.51.5**.

The current experimental source line is **v0.1.3-rc.1 (Praxis R33)**. This prerelease retains the features of [v0.1.2-rc.1](https://github.com/rometet/praxis/releases/tag/v0.1.2-rc.1) and adds native in-world resource-pack reload within an exact **disposable offline test world**.

**Not a general-purpose resource-pack hot-reload release.** The path is hard-gated to the local test world with directory ID PraxisLevelPackOfflineR17 and display name Praxis Level RP Offline Test R17. It is disabled for other worlds, multiplayer, mandatory server packs, multiple simultaneous edits and complex shader/behavior packs.

## Changes in v0.1.3-rc.1

- Minecraft's own Settings → Global Resources screen can add/remove **one eligible registered local texture pack** without leaving the specified offline test world.
- Preserves original stack entries and world-scoped/server-owned packs. Only one validated selection is allowed.
- Uses the real native Minecraft Full Stack/texture/UI reload with owner/thread/epoch checks and both resource participants: Praxis and **compatible LHolo resource ABI v4**.
- R33 recovers the local player model by reloading animation/geometry, notifying client resource definitions and rebuilding entity renderers after the native resource reload.
- Keeps the F10 diagnostic resource pack page and existing HUD, FreeCamera/Freelook, NoRender, TimeChanger/WeatherChanger, inventory and utility modules.

On **October 21, 2026**, the user confirmed that their character remained visible after turning **OFF** a resource pack from the Minecraft settings screen in the dedicated offline clone. Native application/epoch restoration and third-person model rendering were also recorded in logs. ON/OFF on other packs, devices, worlds and servers is **not verified**.

**Compatible LHolo required for this workflow.** The exported resource reload API v4 is absent from some public LHolo versions. This release does **not** include LHolo; do not bypass the participant check.

## Installation

1. Back up your existing PraxisCompanion DLL, PDB, manifest and config. Do not delete any world, saved packs or settings.
2. Install LeviLamina Client **26.51.5** for Minecraft **1.26.51.01**.
3. Extract the ZIP's top-level PraxisCompanion/ folder directly into the launcher's mods directory, with Minecraft fully closed.
4. Install a **compatible separate LHolo build with resource ABI v4** for experimental in-world RP toggling.
5. Start Minecraft and use F10 for Praxis GUI or Minecraft's normal resource pack screen inside the supported disposable clone.

Do not overwrite saved configuration with config.example.json. Matching PDB symbols are an optional separate download. Native mod hot-unload/re-enable is unsupported; restart Minecraft.

## Known limitations

- Only the named disposable offline clone and ordinary registered local texture pack toggling are allowed.
- Not intended for survival worlds, multiplayer servers, mandated server packs or bulk pack edits.
- LHolo config saving may log Access is denied; shutdown may log native hook teardown warnings.
- Build and offline test success does not replace in-game visual testing. The tested local OFF operation has succeeded, but broad compatibility is not claimed.

Details: [R33 validation](docs/R33_NATIVE_RESOURCE_RELOAD.md), [build](SOURCE_BUILD.md), [release policy](RELEASE_POLICY.md), and [third-party notices](THIRD_PARTY_NOTICES.md).

## Source and licensing

Canonical source: https://github.com/rometet/praxis

The combined binary includes ChiyanMap-derived GPL-3.0-only work along with covered MPL 2.0 and other third-party components. Preserve all upstream terms and notices in companion/LICENSE_COMBINED.txt, LICENSE, licenses/ and THIRD_PARTY_NOTICES.md.

Praxis Client is an independent third-party project, not affiliated with or endorsed by Mojang Studios or Microsoft.
