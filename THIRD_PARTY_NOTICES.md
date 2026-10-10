# Third-party notices

This release combines the imported Praxis Companion source with ChiyanMap
terrain code. The combined DLL is GPL-3.0-only. The original MPL-2.0 source
rights are retained in `companion/LICENSE` and `licenses/MPL-2.0.txt`; the
covered source is also offered under GPL version 3 pursuant to MPL 2.0 section
3.3. See `companion/LICENSE_COMBINED.txt`. The earlier public repository's GPL
license remains in the root `LICENSE`. Third-party files retain their terms.

## Terrain code

- ChiyanMap: https://github.com/sunjing2/ChiyanMap
- Source commit: `1785278a865e6dc93d38cea76ccbffaaf64f6659`.
- GPL-3.0-only; full text: `companion/src/map/chiyan/LICENSE`.
- Adapted terrain palette, biome interpolation, surface/water and relief rules.
  Original attribution and comments remain in the files and `PROVENANCE.json`.

## Linked components and SDK headers

The exact versions, upstream references and included notice paths are recorded
in `licenses/DEPENDENCY_NOTICES.json`. Dear ImGui v1.91.9 and fmt 11.2.0 are MIT.
LeviLamina 26.51.5 is used as the client SDK/runtime; its GPL and LGPL version 3
texts are under `licenses/LeviLamina/`. SymbolProvider v1.3.0 preserves its
source-file Public Domain and mingw-w64 notice under `licenses/SymbolProvider/`.
Header dependencies include EnTT v4.0.0, nlohmann-json v3.12.0, GSL v4.2.0,
GLM 1.0.1, expected-lite v0.8.0 and PFR 2.1.1. LevelDB 1.23 and Snappy 1.2.2
are declared build dependencies with their BSD notices retained. The final
link map identifies the components actually included in the DLL.

The matching preferred-dependency-source archive accompanies the release.
System-provided Windows/MSVC runtimes are obtained from their providers.

## Graphics discovery

The locally adapted `src/vendor/kiero/kiero.cpp` and `kiero.h` derive from
https://github.com/Rebzzel/kiero and retain the MIT license in
`licenses/kiero/LICENSE`.

## Embedded fonts

Corporate Logo Bold ver3, Noto Sans JP, Inter and IBM Plex Sans remain embedded
in the DLL. Their OFL-1.1 texts, copyright and Japanese notice are retained in
`src/assets/fonts/` and `licenses/fonts/`. Corporate Logo attribution includes
LOGOTYPE.JP and Adobe's Source Han Sans-derived glyphs.

Praxis Client is an unofficial project, unaffiliated with Mojang or Microsoft.
