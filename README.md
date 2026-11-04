# Praxis Client / PraxisCompanion

**v0.1.6-rc.1 prerelease — Minecraft Bedrock for Windows 1.26.60.05, Windows x64, LeviLamina Client 26.60.0 (verified pinned build).**

Praxis is a client-side utility mod with a configurable F10 interface. This 26.60 prerelease has **not** achieved complete 26.51 feature parity.

## Known limitations

- **In-world resource-pack switching and hot reloading do not work on 26.60.** Global pack selection may change, but native restacking stays pending and actual textures do not update. The in-mod Apply/Reload buttons are disabled in this prerelease; catalog browsing remains available. Use Minecraft's standard pack management and restart or re-enter the world as needed.
- **FlexibleBlockPlacement remains visible** (rotation / offset / adjacent); the unfinished **AccurateBlockPlacement** card and **FastBlockPlacement** controls are hidden. FastRightClick and FastLeftClick remain listed as distinct click utilities; advanced block-placement controls are not offered.
- **WeatherChanger sunny startup:** runtime verification reported unavailable; full function is not confirmed.
- **LHolo:** no compatible LHolo was loaded in the latest 26.60 run, so coexistence was not verified in that run; LHolo is not bundled.
- Praxis and the standalone F10 GUI loaded successfully in a local game session. Hook installation and a successful build **do not establish that every feature works**.
- Native mod hot-unload/re-enable is unsupported. Restart Minecraft after replacing the DLL.

This is an experimental prerelease, **not** a production-ready or fully validated release.

## Install

1. Fully exit Minecraft and back up the existing `PraxisCompanion` mod folder and `config/config.json`. Keep worlds, server data and packs untouched.
2. Install a compatible LeviLamina Client 26.60.0 for Minecraft Bedrock 1.26.60.05. Praxis verifies the supported native loader image.
3. Extract the client ZIP so its `PraxisCompanion/` directory sits under your launcher's `mods/` directory.
4. Keep your existing configuration; no user settings or world data are bundled.
5. Launch Minecraft; open the interface with F10.

## Source and license

Source tag, exact source commit, DLL SHA-256, and asset checksums are listed in the GitHub Release and BUILD_INFO.json. See [SOURCE_BUILD.md](SOURCE_BUILD.md) and [RELEASE_POLICY.md](RELEASE_POLICY.md).

The combined program contains code under GPL-3.0 and MPL-2.0, plus ChiyanMap-derived work. Preserve [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), the [licenses](licenses/) directory and [companion/LICENSE_COMBINED.txt](companion/LICENSE_COMBINED.txt).

Praxis is an independent third-party project; not affiliated with Mojang or Microsoft.
