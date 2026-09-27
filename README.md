# Praxis Client

Praxis Client is a client-side utility mod for Minecraft Bedrock Edition.

## v0.1.0 target

- Minecraft Bedrock: **1.26.51.01**
- LeviLamina Client SDK/runtime guard: **26.51.5**
- Platform: **Windows x64**
- Mod package name: **PraxisCompanion**
- GUI key: **F10**
- Source repository: https://github.com/rometet/praxis

Praxis Client is an unofficial third-party project and is not affiliated with, endorsed by, or approved by Mojang Studios or Microsoft.

## Features

- NoRender
- ChunkBorders
- Fullbright
- ShulkerPreview
- RedstoneOverlay
- LightOverlay
- Zoom
- Legacy Praxis-style module GUI

LHolo is optional and can provide schematic/projection/placement functionality.

## Install

1. Install LeviLamina Client **26.51.5** for Minecraft Bedrock **1.26.51.01**.
2. Download `PraxisCompanion-v0.1.0-client-windows-x64.zip`.
3. Extract it directly into `mods/PraxisCompanion/`.
4. Start Minecraft and press **F10**.

## Build

Requirements: Windows x64, LLVM/Clang-CL, Visual Studio C++ build tools, Python, and xmake.

```powershell
xmake repo -u
xmake f -a x64 -m release -p windows --target_type=client -v -y
xmake -v -y PraxisCompanionNativeProbe
xmake -v -y PraxisCompanion
```

Run the individual test targets in `tests/` before release builds.

See `docs/` for architecture, feature status, hook audit, coexistence notes, and the runtime test plan.

## Source / binary correspondence

Public binary releases identify the immutable source tag, exact source commit SHA, Minecraft target, and DLL SHA-256. The README inside each binary ZIP repeats those values.

See [`RELEASE_POLICY.md`](RELEASE_POLICY.md).

## License

Praxis-owned source is licensed under **GPL-3.0-or-later**. See [`LICENSE`](LICENSE).

Third-party code, fonts, assets, SDKs, and libraries retain their upstream licenses. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and `licenses/`.
