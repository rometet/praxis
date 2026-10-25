# PraxisCompanion v0.1.4-rc.1 / Praxis R34

Minecraft Bedrock 1.26.51.01 | Windows x64 | LeviLamina Client 26.51.5.

This is the canonical public source for the R34 prerelease. The runtime
manifest schema still reports 0.1.0; the Git tag and package BUILD_INFO.json
identify the actual release and corresponding source commit.

## New in R34

- Flexible Block Placement: Rotation (R), Offset (G), Adjacent (H), and
  combined modes; toggle the feature with P.
- Flexible Placement wins against conflicting Freelook/FreeCamera hotkeys.
  In particular G no longer activates Freelook and snaps your view back.
  Assign a different hotkey to Freelook to retain that function.
- Floor-stair support handling, hit normalization, click fence and
  placement-owner diagnostics.
- Compact F10 interface, streamlined HUD, block-state comparison, and
  Lamium coexistence controls.

## Install and limitations

A separate compatible LHolo build exporting Flexible Placement ABI v2 is
needed for transformed native placement. LHolo itself is not distributed here.
No server-side addon is needed by the design, but not every block and BDS
placement case is confirmed. Minecraft Java Tweakeroo inspires the three
modes; network implementation on Bedrock is distinct.

The experimental native resource-pack reload from R33 is **still limited**
to its explicitly permitted disposable offline world. It is not enabled
generally for multiplayer or production worlds.

Extract the client ZIP's PraxisCompanion/ directory into the launcher's
mods/ folder with Minecraft closed. Preserve your current config.json,
worlds, server data, texture packs and other mods. Optional matching PDB
symbols are provided separately.

## Build and licensing

Build in the companion/ subdirectory with xmake and LeviLamina 26.51.5.
See ../SOURCE_BUILD.md and ../RELEASE_POLICY.md.
GitHub: https://github.com/rometet/praxis
Source tag: v0.1.4-rc.1 (exact source SHA and DLL hashes in GitHub Release
and downloadable BUILD_INFO.json).

Preserve GPL-3.0, MPL 2.0 and third-party license notices in LICENSE,
LICENSE_COMBINED.txt, THIRD_PARTY_NOTICES.md, ../licenses/ and
../THIRD_PARTY_NOTICES.md.
Praxis Client is not affiliated with Mojang or Microsoft.
