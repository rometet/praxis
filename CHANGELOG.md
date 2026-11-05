# Changelog

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
