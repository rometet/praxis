# Third-Party Notices

Praxis-owned source code is licensed under GPL-3.0-or-later unless a file or directory states otherwise.

Third-party components are not relicensed by Praxis.

## Vendored source

### kiero-derived DXGI method discovery

- Upstream: https://github.com/Rebzzel/kiero
- License: MIT
- Local files: `src/vendor/kiero/kiero.cpp`, `src/vendor/kiero/kiero.h`
- License text: `licenses/kiero/LICENSE`

The local kiero files are adapted for Praxis D3D11/D3D12 method discovery and hook bootstrap.

## Embedded fonts

The following fonts are embedded into the Windows DLL through `src/resources/praxis_companion.rc`.

- Corporate Logo Bold ver3 — SIL Open Font License 1.1; attribution/notice files are kept beside the font.
- Noto Sans JP — SIL Open Font License 1.1.
- Inter — SIL Open Font License 1.1.
- IBM Plex Sans — SIL Open Font License 1.1.

Font binaries and their license files are in `src/assets/fonts/`; release-package copies are under `licenses/fonts/`.

## Build/runtime dependencies

These are resolved through xmake / the LeviMC package repository instead of being vendored here:

- LeviLamina 26.51.5 — https://github.com/LiteLDev/LeviLamina
- Dear ImGui v1.91.9 — https://github.com/ocornut/imgui — MIT
- fmt — https://github.com/fmtlib/fmt — MIT
- LevelDB — https://github.com/google/leveldb — BSD 3-Clause-style license
- Snappy — https://github.com/google/snappy — BSD 3-Clause-style license
- SymbolProvider v1.3.0 — https://github.com/LiteLDev/SymbolProvider

Where dependency license terms require notices for binary redistribution, those terms remain applicable.

## Project relationship

Praxis Client is an unofficial third-party project and is not affiliated with, endorsed by, or approved by Mojang Studios or Microsoft.
