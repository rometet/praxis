# Flexible Block Placement: Offset / Adjacent support repair (2026-10-26)

## Source of truth

Tweakeroo: `PlacementTweaks.java` flexible-placement logic (rotation, offset, adjacent).
The three position transformations are independent and can be combined.
Praxis keeps the existing 5-zone overlay and destination math.

## Changes

* Offset and Adjacent already existed in the source and are active through their independent HOLD bindings.
* Position-only modes now select a real adjacent support when the previous face points into air.
* Prefer the original clicked block for Adjacent when it is a valid direct neighbour.
* Preserve any chosen rotation-face ownership; do not silently change rotation mode.
* Keep fallback for opaque full blocks if the target has no non-air neighbour.
* Overlay and actual placement share the same support selection.

## Controls (current installed config)

* P: enable/disable the Flexible Block Placement feature.
* Hold R: Rotation; select one of five overlay sectors.
* Hold G: Offset; target diagonal or one block past the clicked face.
* Hold H: Adjacent; target an edge neighbour of the clicked block.
* HOLD combinations: R+G, R+H, G+H, R+G+H.
* Rebind in Praxis key bindings. Center of Adjacent retains ordinary target.

## Practical test matrix

1. Place a solid support; select its TOP face and an edge with H only. The outline must move to its horizontal neighbour and placement must use the support.
2. Hold G only on an edge. Outline must show the diagonal target; verify it on local Bedrock and on BDS.
3. Hold G+H on the same edge. The outline must advance two cells on the selected axis.
4. Hold R alone on a directional block to confirm rotation remains unchanged.
5. Test stair/slab placement with a valid support, and test no-support behavior with full blocks.
6. Confirm occupied targets remain blocked (red outline), and inventory UI, EasyPlace and manual LHolo ownership do not conflict.

## Validation

* `PraxisCompanionFlexiblePlacementTests`: 529 PASS.
* `PraxisCompanionFlexibleFloorStairTests`: 2515 checks, 0 failures.
* `PraxisCompanionLholoFlexiblePlacementAbiTests`: 6 PASS.
* `xmake build PraxisCompanion`: successful.
* Native multiplayer server acceptance requires a manual test; the server may reject unsupported floating blocks.
