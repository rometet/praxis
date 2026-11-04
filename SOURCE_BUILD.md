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
