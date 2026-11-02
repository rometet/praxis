# Praxis Client — English Guide

[日本語 / Project home](../README.md) · [Releases](https://github.com/rometet/praxis/releases)

**Praxis Client (PraxisCompanion)** is a utility mod for **Minecraft Bedrock Edition on Windows**, offering building controls, overlays, inventory tools, camera functions, and a configurable GUI. Schematic projection is handled separately by [LHolo](https://github.com/rometet/LHolo).

> [!IMPORTANT]
> The latest public prerelease is **v0.1.4-rc.1 (Praxis R34)** for **Minecraft Bedrock 1.26.51.01 / LeviLamina Client 26.51.5 / Windows x64**. It is **not compatible with Minecraft 26.60**.

## Download and install

1. Close Minecraft, and use the matching Windows Minecraft and LeviLamina Client versions.
2. Download `PraxisCompanion-v0.1.4-rc.1-client-windows-x64.zip` from [Releases](https://github.com/rometet/praxis/releases/tag/v0.1.4-rc.1).
3. Back up any existing `mods/PraxisCompanion/` folder and its configuration.
4. Extract the ZIP's `PraxisCompanion` folder into the selected instance's `mods/` directory. Do not overwrite saved configuration with the example JSON.
5. Start Minecraft and press **F10** to open Praxis.

The basic utilities work without LHolo. If LHolo is installed, a **compatible GUI bridge** is needed for safe UI coexistence. An incompatible LHolo can prevent the Praxis GUI from opening rather than install a conflicting graphics hook.

## Main features

| Category | Features |
| --- | --- |
| Rendering | NoRender, Fullbright, ChunkBorders, LightOverlay, RedstoneOverlay, Zoom |
| Camera and world view | Freelook, FreeCamera, TimeChanger, WeatherChanger |
| Inventory and tools | ShulkerPreview, Hand Restock, Inventory Sort/Transfer, Tool Protection, Weapon Switch |
| Information | HUD, durability information, block-state comparison, map, HUD layout controls |
| Building | Flexible Block Placement (requires a separate compatible LHolo build) |
| Compatibility | Lamium coexistence information and overlapping Praxis-feature controls |

Features have different levels of verification. Not every combination of Minecraft version, world, server, and feature has been tested.

### Flexible Block Placement (R34)

- **P**: toggle the feature
- Hold **R**: Rotation mode
- Hold **G**: Offset mode
- Hold **H**: Adjacent mode
- Combine **R / G / H** as needed

This functionality requires **LHolo Flexible Placement ABI v2**. LHolo is not bundled. It is inspired by Java Tweakeroo, but the Bedrock implementation is different and is not guaranteed to be accepted by every server or for every block.

The **G** key can conflict with Freelook. R34 prioritizes Flexible Placement to avoid restoring the old camera direction when G is released; rebind Freelook to a different key if you use both.

## Experimental resource-pack reload: restricted

The R33/R34 in-world resource-pack reload is **not available for ordinary worlds or multiplayer servers**. It is gated to a specifically authorized **disposable offline test-world clone** and only supports toggling **one eligible local texture pack**. Server-mandated packs, multiple concurrent changes, behavior packs, and shaders are out of scope.

It also requires **a separate LHolo build with Resource Reload ABI v4**. Do not bypass these compatibility or world restrictions. The October 21, 2026 single-pack OFF test succeeded in the designated clone without losing the player's third-person model, but this is not a general compatibility claim.

Back up settings and worlds before testing prereleases. Avoid swapping DLLs or hot-unloading either mod while Minecraft is running.

## Documentation and licensing

- [Changelog](../CHANGELOG.md) · [Source build](../SOURCE_BUILD.md) · [Release policy](../RELEASE_POLICY.md)
- [Flexible Placement details](flexible-offset-adjacent-20261026.md) · [LHolo coexistence](LHOLO_COEXISTENCE.md)
- [Resource-pack test policy](R33_NATIVE_RESOURCE_RELOAD.md) · [Runtime test plan](RUNTIME_TEST_PLAN.md)
- [License](../LICENSE) · [Third-party notices](../THIRD_PARTY_NOTICES.md) · [Bundled license notices](../companion/LICENSE_COMBINED.txt)

Praxis Client is an independent third-party project, not affiliated with Mojang Studios or Microsoft. The distributed work includes components subject to GPL-3.0, MPL-2.0 and other respective third-party licensing terms.
