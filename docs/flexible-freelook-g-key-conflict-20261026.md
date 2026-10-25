# Praxis Flexible Placement – G key / Freelook camera collision

Date: 2026-10-26

## Evidence

The real installed PraxisCompanion config contained:
- `keybinds[20] = 71` (Freelook = G), `keybindModifiers[20] = 0`
- `freelook = true` and `freelookOptions.hold = true`
- `flexibleBlockPlacement = true`, `flexiblePlacementOptions.offsetKey = 71` (Offset = G), `offsetModifiers = 8`.

Real `logs/latest.log` (2026-10-26 02:32) contains repeated
`CameraCandidate Freelook begin; native look input` and
`CameraCandidate Freelook restored native camera and Link` while
testing flexible placements. Freelook's held mode intentionally keeps
the player facing fixed and restores the original camera on release.

## Fix

In the camera input ownership gate `bindAllowed`, reject a Freelook or
FreeCamera hotkey that overlaps **any of the three** enabled Flexible
Placement activation chords (Rotation, Offset, Adjacent). Use the existing
`hotkeyConflict` and canonical modifier conventions to handle explicit
and Any masks, including combined keys. When Flexible Placement is off,
normal camera hotkeys remain available.

Show a warning on the conflicting camera setting page. Do not edit the
user's persisted key bindings, block placement policy, LHolo ABI, camera
angles, or world. Rebinding Freelook to another key through the GUI is
supported.

## Validation

- Hotkey modifier policy: 282 PASS including G/Freelook collision regression.
- Flexible placement policy: 529 PASS (unchanged).
- Floor stair placement policy: 2515 checks, zero failures.
- LHolo ABI: 6 PASS.
- Full PraxisCompanion DLL release build: PASS.

## Manual

Hold G, turn camera without clicking, release: crosshair must remain
where moved (no snapback). Hold G and right click in the green offset
cell: placement should not be interrupted by Freelook. Test separate
H/R modes and check assigned Freelook key in GUI. Repeat on BDS;
automated C++ tests cannot establish server-accepted placement.

Tweakeroo's three conceptual modes/overlay are referenced but the
Bedrock client-only native transaction implementation is not identical
to Java Tweakeroo's interaction/network path.
