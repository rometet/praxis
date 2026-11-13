# PraxisCompanion v0.1.7-rc.3

**Minecraft Bedrock for Windows 1.26.60.05 / Windows x64 / LeviLamina Client 26.60.0 (pinned build f28a4b5d)**

Open the Praxis interface with **F10**. The current 26.60 version includes TimeChanger, WeatherChanger, NoRender, overlays, FreeCamera, inventory tools, and FlexiblePlacement. The user verified the core repaired functions in-game, including visible texture updates when switching resource packs inside the world, and transformed placement through a separately installed compatible LHolo.

The last crash on enabling HandRestock was traced to native 26.60 HUD RTTI. The client now validates the HUD scene type before retrieving the controller. FlexiblePlacement no longer drops the physical first mouse press when Bedrock reports first=false; it keeps click deduplication and the LHolo ownership gate.

## New in v0.1.7-rc.2

- ToggleSneakSprint (V for crouch, B for sprint by default), using a transient
  raw-input intent through the **existing** Praxis input bridge.
- AppleSkin-style saturation outlines and food-recovery preview rendered onto
  the vanilla HUD. Saturation follows the active resource pack's food sprite.
- AngleSnap yaw/pitch axis locks and Java F3-like DebugScreen; the map is
  automatically hidden while the debug view is visible.
- AutoTool, EffectTimer, DeathLogger and limited ItemScroller favorites.

Actual 15-degree rotation snapping and MaterialCollector are not implemented.
Not all added modules have received complete in-game validation. This prerelease
is not a claim of full feature parity. CI continues to have a known upstream
LeviLamina dependency build failure; this package uses the locally tested client
build with recorded SHA-256.

## Limitations and installation

- The advanced AccurateBlockPlacement and FastBlockPlacement UI remains hidden/disabled.
- Tested gameplay does not establish universal compatibility with every world/server or 26.51 behavior.
- LHolo (Flexible Placement ABI v2) is separately required for transformed placement, not included.
- Never hot-unload/re-enable the native DLL. Exit Minecraft before updating.
- Preserve config/config.json and back up worlds, data and resource packs.

Extract the ZIP's top-level PraxisCompanion directory into the Minecraft version's mods folder.

Repository: https://github.com/rometet/praxis
Source release tag: **v0.1.7-rc.3**. Exact source commit and DLL SHA-256 are in the downloadable BUILD_INFO.json and the README included in the binary ZIP.

Preserve the combined GPL-3.0, MPL-2.0 and third-party notices, including LICENSE_COMBINED.txt, LICENSE, THIRD_PARTY_NOTICES.md, and the upstream licenses. Not affiliated with Mojang or Microsoft.
## Source availability and privacy (v0.1.7-rc.3)

The source at https://github.com/rometet/praxis/tree/v0.1.7-rc.3,
the matching release's source archive, and SOURCE_DEPENDENCIES.md are the
documented rebuild route. BUILD_INFO.json records the tagged commit and DLL
SHA-256. The binary's CodeView record uses a relative PDB name; full local
debug symbols are not redistributed because they contain build-machine paths.
The published DLL itself is independent of the optional PDB.
