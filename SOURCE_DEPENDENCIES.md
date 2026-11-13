# Dependency source, provenance and corresponding-source access

PraxisCompanion `v0.1.7-rc.3` — Windows x64 / Minecraft Bedrock for Windows `1.26.60.05`.

## Exact source for the distributed binary

- Source repository: https://github.com/rometet/praxis
- Matching immutable source archive: https://github.com/rometet/praxis/archive/refs/tags/v0.1.7-rc.3.zip
- GitHub release page: https://github.com/rometet/praxis/releases/tag/v0.1.7-rc.3
- The release's `BUILD_INFO.json` supplies the exact source commit, source tree hash, DLL SHA-256, dependency pins and compiler/linker provenance.
- The release also offers a *separate* `PraxisCompanion-v0.1.7-rc.3-corresponding-source.zip` beside the client binary. It contains the complete public project tree at the matching tag, its build rules, copyright records, embedded original font files and relevant licenses.
- A separately attached `PraxisCompanion-v0.1.7-rc.3-third-party-sources.zip`
  contains **13 upstream source snapshots** including the exact pinned
  LeviLamina version, Dear ImGui, fmt, LevelDB, Snappy and SymbolProvider.
  The archive's `SOURCE_MANIFEST.json` supplies the upstream location, pin,
  byte size, member count and SHA-256 for each nested source ZIP.
  Some transitive/unmodified dependencies are not separately snapshotted;
  `xmake.lua` and the pinned upstream source include their build recipes.
- This is a client-only native mod for a proprietary Minecraft Bedrock game. We do **not** distribute the game, its binaries or proprietary symbols.
- Download/build instructions: `SOURCE_BUILD.md`; `companion/xmake.lua`; the exact Bedrock import-only metadata generator is declared in the xmake target and `companion/src/resourcepacks/BedrockRuntimeImports2660.def`.

## Pinned dependencies and their sources

The following links are intended as clear, immutable upstream source locators. They are **not** a claim that the entire repository contents of each dependency are included in the client ZIP. The applicable license and copyright files are in `licenses/`, `companion/src/`, and `THIRD_PARTY_NOTICES.md`. All actual upstream availability and redistribution requirements must be honored.

| Component | Source / exact pin | License / inclusion |
|---|---|---|
| LeviLamina Client 26.60.0 | [pinned source f28a4b5d](https://github.com/LiteLDev/LeviLamina/tree/f28a4b5dd6454948f163b4649238dfecabb7485f) ([archive](https://github.com/LiteLDev/LeviLamina/archive/f28a4b5dd6454948f163b4649238dfecabb7485f.zip)) | GPL/LGPL version 3; external native SDK/runtime. License texts in `licenses/LeviLamina/`. |
| bedrockdata v26.60.5-client.3 | [LiteLDev xmake package recipe](https://github.com/LiteLDev/xmake-repo) | Versioned game SDK/mapping package **recipe** pinned in `companion/xmake.lua`; observe upstream licensing; no proprietary game EXE bundled. |
| levibuildscript 0.6.1 | [LiteLDev levibuildscript](https://github.com/LiteLDev/levibuildscript) | Build-time scripts pinned in `companion/xmake.lua`; source available upstream. |
| Dear ImGui v1.91.9 | [exact source](https://github.com/ocornut/imgui/tree/v1.91.9) | MIT; statically built with Win32/DX11 support, license in `licenses/Dear-ImGui/`. |
| fmt 11.2.0 | [exact source](https://github.com/fmtlib/fmt/tree/11.2.0) | MIT; `licenses/fmt/`. |
| EnTT v4.0.0 | [exact source](https://github.com/skypjack/entt/tree/v4.0.0) | MIT; `licenses/EnTT/`. |
| nlohmann-json v3.12.0 | [exact source](https://github.com/nlohmann/json/tree/v3.12.0) | MIT; `licenses/nlohmann-json/`. |
| GSL v4.2.0 | [exact source](https://github.com/microsoft/GSL/tree/v4.2.0) | MIT; `licenses/GSL/`. |
| GLM 1.0.1 | [exact source](https://github.com/g-truc/glm/tree/1.0.1) | MIT; `licenses/GLM/`. |
| expected-lite v0.8.0 | [exact source](https://github.com/martinmoene/expected-lite/tree/v0.8.0) | Boost Software License; `licenses/expected-lite/`. |
| Boost.PFR 2.1.1 | [pinned source](https://github.com/boostorg/pfr/tree/294a4976bd04829dd204aaf9e9fd30338a5d3199) | Boost Software License; `licenses/PFR/`. |
| LevelDB 1.23 | [source release](https://github.com/google/leveldb/tree/1.23) | BSD; `licenses/LevelDB/`. |
| Snappy 1.2.2 | [source release](https://github.com/google/snappy/tree/1.2.2) | BSD; `licenses/Snappy/`. |
| SymbolProvider v1.3.0 | [pinned source](https://github.com/LiteLDev/SymbolProvider/tree/6c93ec45c8455992ee726d92df60316c8e731c44) | Original file notices preserved in `licenses/SymbolProvider/`. |
| Kiero | [upstream source](https://github.com/Rebzzel/kiero) | MIT; adapted source in `src/vendor/kiero`, attribution and license in `licenses/kiero/`. |
| ChiyanMap | historical reference `https://github.com/sunjing2/ChiyanMap`, source revision `1785278a865e6dc93d38cea76ccbffaaf64f6659` | GPL-3.0-only; upstream URL currently gives 404, so independent upstream archival verification remains open. **Adapted, corresponding code is included** in `companion/src/map/chiyan`; original copyright/provenance and full license remain. Do not delete those notices. |

## How the legal boundary is handled

- The combined DLL is distributed under GPL-3.0-only, retaining the relevant MPL-2.0 provenance and eligible secondary-license option documented in `companion/LICENSE_COMBINED.txt`.
- Users should be able to retrieve the exact corresponding Praxis source from the same GitHub release page as the DLL, at no charge. If an upstream dependency's source disappears, the distributor must address any still-applicable source-access obligation; a link is not a permanent guarantee.
- This file documents source provenance, but it is **not legal certification** that every upstream license condition is satisfied.
- When redistributing the DLL, include its corresponding source reference and applicable licenses/notices.
- In particular, the historical ChiyanMap upstream 404 does not remove GPL rights or obligations: the adapted source and attribution remain available from this repository.

## Privacy and technical constraints

- The published client DLL contains a **relative CodeView PDB filename** rather than an absolute Windows build path.
- Complete private PDB symbols are not uploaded because they encode source paths and build environment metadata. The optional PDB is not necessary to download, run or modify the source.
- This maintenance release does not change Minecraft gameplay behavior. ToggleSneakSprint uses the already-installed input extraction bridge; no additional hook is registered by this release.
