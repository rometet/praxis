# Source build and relink

Target: Minecraft Bedrock 1.26.51.01, LeviLamina Client 26.51.5, Windows x64.
The release is built with clang-cl 22.1.8, MSVC 14.44.35207 and Windows SDK
10.0.26100.0, C++20, /MD, /O2 and /utf-8. PDB symbols accompany the release.

Install xmake, LLVM clang-cl, the MSVC build tools and Windows SDK. From the
source root, use:

```powershell
xmake -P companion f -p windows -a x64 -m release --target_type=client
xmake -P companion build PraxisCompanion
```

Dependencies are resolved by the LeviMC xmake package repository. Pinned
dependency source references and notices are in `licenses/DEPENDENCY_NOTICES.json`.
The accompanying preferred-dependency-source ZIP supplies those source files.
Build or modify the dependencies using their upstream build instructions; the
application source remains available for modification and relinking.

The native SDK recipe runs prelink for client-windows-x64 and links the objects
and Windows resources, then bedrock_runtime_api.lib, then the SDK/package
libraries (including SymbolProvider.lib), then system libraries. The generated
runtime API import library must precede SymbolProvider. The SymbolProvider
delay-load helper and preload initializer must be the selected owners; the
stock MSVC delay helper must not replace them. Release validation records this
link order and checks PE exports/imports plus matching PDB GUID/Age.

The 51 registered executable tests and three compile-only probes are declared
in `companion/xmake.lua`. Build their named targets explicitly; they are not
default runtime targets. The GUI test uses real ImGui with offline graphics
fixtures. Offline success does not prove native gameplay or visual correctness.
Public release validation counts and hashes are in the attached build metadata.

The release supplies this public source at its immutable tag, matching source
and preferred-dependency-source archives, full component/font notices, and
installation information. It does not bundle Minecraft, the launcher, closed
source loader tools, user worlds/configs, private development history or caches.
