# Praxis Client / PraxisCompanion

**Current prerelease: v0.1.7-rc.2** — Minecraft Bedrock for Windows **1.26.60.05**, Windows x64, pinned **LeviLamina Client 26.60.0** (build f28a4b5d).

Praxis is an independent client-side utility mod. The GUI opens with **F10**. LHolo is installed separately and is required for FlexiblePlacement's native placement bridge (ABI v2).

## 26.60 updates verified in-game

The current client was tested after the fixes below on a 1.26.60.05 world:

- **TimeChanger:** sun position and daytime sky/atmosphere rendering.
- **WeatherChanger / NoRender Weather:** weather display control and precipitation suppression.
- **World resource packs:** in-world switching and visible texture refresh on the tested path.
- **FlexiblePlacement:** R rotation and transformed block placement through LHolo, including the first-click fix.
- **FreeCamera:** activation/restoration and camera movement; **HandRestock:** safe HUD activation and replenishment behavior.
- **NoRender and supporting utilities:** latest 26.60 updates to render suppression, input and placement compatibility.

This release contains the updated GUI, overlays, map, inventory utilities and other source modules. These are user-observed functional checks, **not exhaustive coverage** of every block, world, server, graphics mode or 26.51 feature.

## 0.1.7-rc.2: additional client utilities

This prerelease adds AutoTool, EffectTimer, ToggleSneakSprint, AngleSnap's
yaw/pitch *axis locks*, AppleSkin-style food preview, Java F3-style DebugScreen,
session/persistent DeathLogger, and limited ItemScroller recipe/trade favorites.
ToggleSneakSprint and the saturation overlay were specifically re-tested in game
on 1.26.60.05 after their final fixes; AngleSnap's axis lock and the DebugScreen
layout/map hiding were also reported working. Full acceptance of every added
feature is **not** claimed.

**Known limitations:** true angle-increment snapping and LHolo MaterialCollector
are not implemented. ItemScroller is a limited recipe/trade-favorite helper, not
a full Java mod port. The AppleSkin overlay needs readable client-side hunger
attributes and the active pack's vanilla hunger sprite. ToggleSneakSprint uses
Praxis's **existing** native input-extraction hook; no *new* hook is registered.
The upstream pinned LeviLamina dependency has an existing GitHub Actions build
failure; the released binary is the locally tested Windows x64 build. Source and
checksums are provided with each release.

## Known limitations

- **AccurateBlockPlacement** and experimental **FastBlockPlacement** remain hidden/disabled in the normal GUI; do not treat them as complete.
- World resource-pack hot reload was verified in a local gameplay path; other screens, servers and pack combinations may behave differently. Do not replace world or pack backups with this feature.
- Native hot-unload/re-enable is unsupported. Fully restart Minecraft after upgrading or replacing the DLL.
- Only the pinned Minecraft and compatible LeviLamina Client builds are supported. LHolo is **not bundled**.

## Installation

1. Fully exit Minecraft. Back up the existing PraxisCompanion folder, especially its settings.
2. Install Minecraft Bedrock for Windows 1.26.60.05 with compatible LeviLamina Client 26.60.0.
3. Download the client Windows x64 ZIP for v0.1.7-rc.2 from GitHub Releases.
4. Extract its PraxisCompanion folder into the corresponding version's mods directory. Preserve your existing config/config.json, worlds and packs.
5. Start Minecraft and press **F10**. Install a compatible LHolo separately to use FlexiblePlacement.

The release contains the binary SHA-256, immutable source commit and source tag in BUILD_INFO.json and the packaged README.

[Source build](SOURCE_BUILD.md) · [Release policy](RELEASE_POLICY.md) · [Changelog](CHANGELOG.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The project combines GPL-3.0- and MPL-2.0-covered components and third-party code. Preserve LICENSE, companion/LICENSE_COMBINED.txt, licenses/ and THIRD_PARTY_NOTICES.md.

Praxis is not affiliated with Mojang or Microsoft.