# Praxis Tsukuyomi follow-up: ToggleSneakSprint and saturation UI (2026-11-13)

## User feedback
- ToggleSneakSprint still did nothing with the previous 26.60 build.
- AppleSkin should match Lamium's icon-based display as closely as practical; saturation outlines looked wrong.
- AngleSnap and Java F3/DebugScreen (including map hiding) were reported as working well. No changes to those features or ItemScroller.

## Code changes
### Input
- Root cause hypothesis strengthened by comparing Lamium's installed code: editing LocalPlayer::setSneaking or persistent MoveInputComponent during client ticks gets overwritten by vanilla's raw input extraction phase.
- The existing Praxis `FreeCameraMovementHook` already intercepts `ClientInputUpdateSystem::extractRawHIDInput` to support another feature. This commit **does not register any additional hook**. It extends that existing callback to pass a *temporary copy* of the SDK `MoveInputComponent` with `SneakDown` / `SprintDown` set when ToggleSneakSprint intent is armed. Original input state is not modified and vanilla remains responsible for applying movement.
- A focused `gameplayToggleInputIntent()` bridge reads atomics and checks admission. Existing LL `KeyInputEvent` toggles are retained; an independent `GetAsyncKeyState` edge-detection fallback detects keys when the event has already been consumed. GUI focus and world exit invalidate/pause injection.
- One-shot diagnostics distinguish `ToggleSneakSprint: native input extraction bridge MATCHED` from a missing/mismatched local input component, and edge messages distinguish `LL key` vs `polled`. Search the standard LeviLauncher log if the behavior fails again.
- **Constraint caveat:** this solution shares a **pre-existing native input interception** used by FreeCamera. The new feature registers **no new native hook**, but is not a pure no-hook implementation. The pinned 26.60 LeviLamina SDK does not expose an equivalent public input-extraction callback. The previous LL-event/tick-only attempts failed in real gameplay. A true hook-free alternative requires upstream API support.

### AppleSkin
- Replaced rectangular yellow strokes with 5 dynamically generated runtime textures derived from the **active resource pack's own** `textures/ui/hunger_background` alpha silhouette. Decode via existing WIC helper, create per-pixel contour RGBA images, upload through published TextureGroup API.
- Current saturation: thin golden whole/right-half contours; projected gain: pale-gold whole/right-half/left-half contours. Bound projected saturation by projected hunger. Hunger preview remains vanilla `hunger_full` / `hunger_half`, 50% opacity.
- Draw at the live `hunger_rend` UI control position. Text HUD is disabled. Render fails closed if pack texture/attribute is missing. Recreate runtime textures on published resource-pack epoch change. Do not redistribute Lamium source code or textures.

## Verification
- PraxisCompanion DLL build succeeded.
- `PraxisCompanionTsukuyomiFeatureTests`: 70 PASS (including silhouette tests).
- `PraxisCompanionSaturationTests`: 271 PASS.
- `PraxisCompanionSaturationUiTests`: 437 PASS, 58 ImGui frames.
- `PraxisCompanionPlayerHudTests`: 25 PASS.
- `PraxisCompanionHudLayoutHotkeyTests`: 33 PASS.
- `PraxisCompanionInformationPolicyTests`: 71 PASS.
- `PraxisCompanionDeathLogTests`: 23 PASS.
- Gameplay and native food texture upload still need a real Minecraft 1.26.60.05 test.

## Targeted next real test
1. Fully restart Minecraft (do not hot-replace DLL while running).
2. F10 > ToggleSneakSprint ON, press V once while in world: sneak should persist without chatter. Press V again to release. Press B while walking to test sprint. If not working, inspect `logs/latest.log` for `ToggleSneakSprint:` diagnostics.
3. F10 > AppleSkin ON, survive with saturation and hold food: gold contour should follow each meat sprite; half units should paint only half the silhouette and pale gain should be separate. Verify with resource pack changes if applicable.
4. Do not spend time retesting AngleSnap/DebugScreen unless regression is observed.

## Deployment safety
- Back up original DLL/PDB/manifest, preserve config; validate SHA256. Do not touch LHolo/BDS.
