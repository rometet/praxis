# Changelog

## v0.1.7-rc.3 (2026-11-13) — License, source and build privacy maintenance

- Correct the pinned LeviLamina dependency notice to Client 26.60.0 and its
  exact source commit, replacing the stale dependency source hashes with
  verified snapshots; attach the matching source archive and 13 third-party
  pinned source snapshots.
- Clarify public corresponding-source and third-party dependency source
  locations, removing the false claim that a separate full dependency-source
  archive was attached to the previous prerelease.
- Keep copyright/license/provenance notices for reused GPL/MPL/MIT/LGPL/OFL
  components, including ChiyanMap and other attributed work.
- Use a relative CodeView PDB reference in the release DLL to avoid exposing
  the build account's Windows username and workspace path.
- Do not publish full private build PDB symbols.
- No gameplay or client UI behavior changes from v0.1.7-rc.2.

## v0.1.7-rc.2 (2026-11-13) — Tsukuyomi-inspired utilities

- Added AutoTool, EffectTimer, AngleSnap yaw/pitch axis locks,
  ToggleSneakSprint (V/B key toggles), graphical AppleSkin, Java-style
  DebugScreen, and persistent/session DeathLogger.
- Added a limited ItemScroller recipe-favorites and trade-favorites
  experience. Automatic villager-trade execution is **not** implemented.
- Reworked ToggleSneakSprint input from per-tick actor state writes to a
  transient raw-input intent through Praxis's **existing** input-extraction
  hook. No additional native hook was registered.
- Reworked AppleSkin to render resource-pack-derived meat-sprite saturation
  contours: gold for current saturation, pale gold for projected gain and
  correct partial-icon fills.
- Implemented an F3-inspired two-column DebugScreen which temporarily hides
  the minimap, returning it automatically on exit.
- Tested final ToggleSneakSprint and AppleSkin behavior in-game on the pinned
  1.26.60.05 client; prior feedback also confirmed working AngleSnap axis
  lock and F3-style DebugScreen. Offline tests cover additional policy/UI logic.
- **Limitations:** 15-degree true AngleSnap rotation, MaterialCollector via LHolo,
  and full ItemScroller auto-trading are not available. Other additions are
  not claimed fully in-game tested. The upstream LeviLamina CI build remains
  broken on GitHub-hosted Windows; the local client DLL was built and tested.
- Windows x64 / Minecraft 1.26.60.05 / LeviLamina Client 26.60.0 prerelease.

## v0.1.7-rc.1 (2026-11-05) — Minecraft 1.26.60.05 verified client update

- Repaired TimeChanger daytime sky and WeatherChanger/NoRender Weather visuals; validated these in-game.
- Completed the tested in-world resource-pack selection and visible texture-refresh route.
- Fixed 26.60 FreeCamera frame updates, HandRestock first HUD controller handling and the enable-time native RTTI crash.
- Fixed FlexiblePlacement 26.60 first-press suppression, vertical orientation pitch delivery and LHolo bridge behavior.
- Updated NoRender world-actor admission for the current rendering pipeline.
- Preserved advanced placement UI restrictions and the requirement to restart Minecraft when upgrading.
- Kept public release history separate from local development and diagnostic branches.
## v0.1.5-rc.1 (2026-11-05) 窶・Minecraft Bedrock 1.26.60.05 preview

- Experimental Windows x64 port for Minecraft 1.26.60.05 / LeviLamina Client 26.60.0.
- Preserved GUI and existing utility modules, with runtime parity still subject to per-feature acceptance tests.
- Kept FlexibleBlockPlacement visible. Hid AccurateBlockPlacement and FastBlockPlacement and disabled their runtime execution even when older profiles requested them. FastRightClick retains normal item-use but its advanced block-placement switch is not available.
- Marked in-world Global resource-pack hot-reload as **NOT WORKING**; disabled Praxis's in-world Apply/Reload buttons. See [mcapi-requests #288](https://github.com/LiteLDev/mcapi-requests/issues/288).
- This is a **prerelease**, not a claim of 26.51 complete feature parity.

## Previous line

[v0.1.4-rc.1](https://github.com/rometet/praxis/releases/tag/v0.1.4-rc.1) was a separate 26.51 prerelease.
