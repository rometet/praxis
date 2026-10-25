# R34 source build and binary correspondence

Current source release: **v0.1.4-rc.1** (Praxis R34).
Minecraft Bedrock 1.26.51.01; LeviLamina Client 26.51.5; Windows x64.

Build the canonical source at the immutable release tag from `companion/`:

    xmake f -p windows -a x64 -m release --target_type=client -y
    xmake b -j 3 PraxisCompanion

The distributed R34 DLL is the one built from this source state and
locally tested in-game with the G/Freelook conflict resolved. The source
commit SHA, DLL/PDB SHA-256 hashes, target SDK, package metadata and
limitations appear in the GitHub Release and downloadable BUILD_INFO.json.
Exact cross-host binary reproducibility is not claimed.

R34 offline validation:
- Hotkey modifiers / camera collision: 282 checks PASS
- Flexible Placement: 529 PASS
- Floor stair placement: 2515 checks; failures 0
- LHolo Flexible Placement ABI: 6 PASS
- Windows release DLL build: PASS

R34 requires separately installed LHolo with flexible-placement ABI v2
for rotation/offset/adjacent native transactions. This feature is built
for Bedrock, not a byte-for-byte port of Java Tweakeroo. Native server
acceptance for every block and server is not claimed. The R33 resource-
pack world gate and LHolo resource ABI v4 requirements are unchanged.

## Earlier R33 build notes

# R33 source build and binary correspondence

Target: Minecraft Bedrock **1.26.51.01**, LeviLamina Client **26.51.5**, Windows x64.
The release tag is **v0.1.3-rc.1**; its exact source commit SHA, DLL SHA-256 and PDB SHA-256 appear in the GitHub Release description and downloadable BUILD_INFO.json.

Prerequisites: Visual Studio 2022 C++ build tools, Windows SDK, clang-cl/LLVM, Python and xmake. The local R33 candidate used these with LeviLamina 26.51.5. To build from the source checkout:

    Set-Location .\companion
    xmake f -p windows -a x64 -m release --target_type=client -y
    xmake b -j 3 PraxisCompanion

Packages resolve from the LeviMC xmake repo. If a local repository clone is corrupt and cannot update, repair that package repository or use a previously verified package cache, rather than changing the tagged sources.

The shipped program is built entirely from companion/src with the companion/xmake.lua project. The source and test trees in this tag are byte-for-byte identical to those of the R33 DLL tested by the user. Two *test-target-only* include directory paths in the public xmake.lua were changed to the already vendored companion/tests/lholo-peer state header (SHA-256 0559e63197779ea6be9ca800e7a73c801abc2a051f3ab71a0ae2616a94a06ce9), replacing local development paths. Production source, target list and compiler inputs remain unchanged.

Local offline tests for the release-candidate code passed:
- R31 avatar admission: **28** checks
- R30 native LHolo Glass R5 state prearm: **19**
- R29 native vanilla editor policy: **39**
- Native Settings: **266**
- World Pack engine/native adapter: **1,488**
- World native task: **50**
- R27 ON/OFF: **29**
- Real ImGui synthetic GUI: **12,605,994** checks

The public source was separately built and linked using an independent empty object tree. Its generated DLL hash differs from the single tested local DLL because this PE/PDB toolchain is **not asserted to be byte-for-byte reproducible**. The GitHub asset uses the **user-tested R33 DLL**, not the independent recompilation. Correspondence of the actual companion/src source files was verified with per-file SHA-256.

Dependency source references and licenses are recorded in licenses/DEPENDENCY_NOTICES.json and the preferred dependency source archive. Existing sources and font assets already present in this public repo retain their third-party copyright and licensing texts. See LICENSE, companion/LICENSE, companion/LICENSE_COMBINED.txt, THIRD_PARTY_NOTICES.md, licenses/ and src/assets/fonts/.

The native in-world RP feature is only admitted for the named disposable offline clone. Matching LHolo resource ABI v4 is separately required and is not shipped with this Praxis package. Mojang/Microsoft proprietary game files and private PC/world information are not distributed.
