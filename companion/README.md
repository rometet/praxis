# PraxisCompanion — 1.26.60.05 experimental client prerelease

Release: **v0.1.6-rc.1**; Windows x64; compatible pinned LeviLamina Client **26.60.0**.

## What is included

Praxis GUI (F10), utility and overlay modules, and **FlexibleBlockPlacement**. The module UI hides unfinished AccurateBlockPlacement and FastBlockPlacement controls; separate click utilities remain available.

**Resource-pack switching/hot reload does not work in-world in this 26.60 prerelease.** The catalog browser remains, while in-mod Apply/Reload buttons are disabled. Changing the selected Global pack is **not** proof that Minecraft updated the displayed textures. Use normal Minecraft pack management and restart/re-enter when required.

Other functionality is not fully verified: the sunny WeatherChanger startup test failed in the latest runtime log, and the latest test loaded without LHolo. Full 26.51 parity is **not** claimed.

## Install

Close Minecraft first. Extract the release ZIP's top-level `PraxisCompanion/` directory into the launcher's mods directory. Back up and preserve your existing `PraxisCompanion/config/config.json`, worlds, and pack folders. Native hot-unload/re-enable is unsupported.

## Source and notices

Source repository: https://github.com/rometet/praxis

Source tag: **v0.1.6-rc.1**. Exact source SHA and DLL SHA-256 are supplied in GitHub Releases / BUILD_INFO.json.

See LICENSE, LICENSE_COMBINED.txt, ../licenses/, and ../THIRD_PARTY_NOTICES.md. Not affiliated with Mojang or Microsoft.
