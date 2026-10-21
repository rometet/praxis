# Praxis Client / PraxisCompanion

Minecraft Bedrock **1.26.51.01** / Windows x64 / LeviLamina Client **26.51.5**.

This is the public source for **v0.1.2-rc.1**, a prerelease. The FreeCamera key
toggle change has offline regression coverage and awaits an actual game test.
The user's Weather OK report belongs to the preceding installed build; it is
not a claim that this new binary has passed native testing.

## Changes

- UI descriptions are Japanese. NoRender and its options, TimeChanger and
  WeatherChanger keep their English names; ON/OFF stays unchanged.
- HUD controls are collected in the left HUD page, with the layout editor,
  equipment, durability, information, offhand and LHolo HUD controls retained.
- InventoryHUD's 27-slot display is retired from UI and execution. Slot 23
  remains reserved, preserving old settings and unknown feature slots.
- FreeCamera's activation key toggles on each fresh press. Releasing the key
  keeps it active; a second press turns it off. Repeated key-down events do not
  toggle it repeatedly. Legacy `hold` values remain readable. Freelook keeps
  its own hold/toggle choice, and FreeCamera keeps its unlimited offset travel
  and existing restoration behavior.
- WeatherChanger retains the current Clear/Rain/Snow sky implementation.

Unfinished Resource world/gate changes are excluded. Current LHolo projection
transparency/geometry issues remain a separate reported issue; this Praxis
release does not claim to resolve them.

## Install

Use the release's `PraxisCompanion` directory under your launcher's `mods`
directory while Minecraft is closed. Keep existing config, key assignments,
worlds, packs and other mods. `config.example.json` is an example, not a file
to overwrite your saved configuration. F10 opens the GUI on the Module list.
PDB symbols are an optional separate download and must match the DLL.

The binary package's README and build information identify the exact public
source commit, tag and DLL/PDB hashes.

## Build

See [SOURCE_BUILD.md](../SOURCE_BUILD.md). Build from the `companion` project; the
root `src` contains only shared font and kiero build inputs. The prior flat
project layout is retired in this version.

## Source and licenses

Canonical source: https://github.com/rometet/praxis

The immutable `v0.1.2-rc.1` tag identifies the release source. The Companion,
third-party preferred-source and symbol archives are supplied with the
release. See [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md),
[LICENSE_COMBINED.txt](LICENSE_COMBINED.txt) and
[RELEASE_POLICY.md](../RELEASE_POLICY.md).
