# Current v0.1.7-rc.3 source build

Minecraft Bedrock for Windows **1.26.60.05**; LeviLamina Client **26.60.0** (pinned f28a4b5d); Windows x64.

From the tagged public source release, inside companion/:

    xmake f -p windows -a x64 -m release --target_type=client -y
    xmake b -j 4 PraxisCompanion

The GitHub Release contains the source commit SHA, the exact installed user-tested DLL hashes and checksums. This native toolchain is not asserted to make byte-identical builds on different machines. Source-code correspondence is verified separately. The standalone LHolo bridge is not included.

Verified offline focused checks: FlexibleClickFence 28, FlexiblePlacement 690, LHolo Flexible Placement ABI 6, HandRestock 42, and FreeCamera offset/input 239. A camera configuration integration test could not run in the standalone host due to LeviLamina initialization; Minecraft gameplay was tested separately.

The release DLL is linked with /PDBALTPATH:PraxisCompanion.pdb so the CodeView
record does not expose the builder's Windows profile or source checkout path.
Full private PDB symbols remain on the build machine and are not a release
artifact. Download the exact tagged source and read SOURCE_DEPENDENCIES.md
for upstream dependency retrieval. Third-party dependencies can be unmodified
public projects or linked SDKs, and must be provided in compliance with
their respective licenses. The main repository is the copy of Praxis source.

The exact source release tag and all third-party copyright and license notices must be preserved. Mod DLL hot-unload is not supported.

---
# PraxisCompanion v0.1.5-rc.1 source build

Target: **Minecraft Bedrock for Windows 1.26.60.05**, **LeviLamina Client 26.60.0** build `f28a4b5d`, bedrockdata `v26.60.5-client.3`.

From the release tag, with Windows x64 / VS2022 C++ Build Tools / clang-cl / xmake / Python:

```powershell
cd companion
xmake f -p windows -a x64 -m release --target_type=client -y
xmake b -j 4 PraxisCompanion
```

Production sources are in `companion/src` and build rules are in `companion/xmake.lua`. The release's `BUILD_INFO.json`, `SHA256SUMS.txt` and GitHub description identify the source commit and DLL SHA256; a corresponding tagged tree is published before attaching binaries. Reproducible byte-for-byte builds across Windows hosts are not claimed.

**Known limitation:** World-local resource-pack hot-reload is **not working** for 26.60.05. Accurate and FastBlock placement are hidden and runtime-disabled; FlexiblePlacement remains. Build success is not all-feature runtime certification.

The historical [v0.1.4-rc.1](https://github.com/rometet/praxis/releases/tag/v0.1.4-rc.1) is for Minecraft 1.26.51.01 only. Do not mix these DLLs.
