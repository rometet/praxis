# Changelog

## v0.1.4-rc.1 — 2026-10-26 — Praxis R34 Flexible Placement

- Three Tweakeroo-inspired modes: Rotation (R), Offset (G), Adjacent (H), including combinations, toggled with P.
- Fixed G/Freelook key collision that restored the prior view orientation when G was released.
- Improved block support selection and native placement-hit normalization, plus floor-stair and interactive click handling.
- Added compact GUI/HUD refinements, block-state inspection comparison and standalone Lamium coexistence tools.
- Flexible placement is a Bedrock client-side implementation requiring compatible LHolo ABI v2; Tweakeroo Java parity and all BDS placement cases are not guaranteed.
- Preserved R33 resource-pack restrictions: only the named disposable offline clone supports experimental native hot reload.
- Automated checks passed: 529 flexible policy, 2515 stair policy (zero failures), 282 hotkey checks, 6 LHolo ABI checks. The user confirmed the fixed G-key camera behavior in-game.


## v0.1.3-rc.1 — 2026-10-21 — Praxis R33 experimental native resource reload

- Integrated the native Minecraft Settings → Global Resources single-pack toggle route for a disposable offline test world.
- Corrected the resource ABI v4 BeginNative ordering before the game's stack mutation and resumed both resource participant epochs.
- Added local resource pack catalog/ordinary texture and blocks.json support with strict native identity checks.
- R33 avatar recovery sequence: animation/geometry reload, client resource notification, entity renderer rebuild and bounded third-person diagnostics.
- User-confirmed 2026-10-21: resource-pack OFF from vanilla settings maintained local player visibility in the offline test clone.
- Compatible LHolo resource ABI v4 is separately required; normal worlds, servers, multiple edits and complex resource packs are not covered.
- Known LHolo settings-saving and native teardown warnings remain.

## v0.1.2-rc.1 — 2026-10-10

- Japanese UI descriptions with English NoRender options, TimeChanger and WeatherChanger.
- HUD page and layout editor, legacy InventoryHUD retirement with old settings preserved.
- FreeCamera fresh-press toggle, legacy configuration compatibility, unlimited offset and restoration retained.
- Resource world/gate excluded from this earlier RC.
- Offline validation, source/PDB correspondence, game runtime test pending at that time.

## v0.1.0 — 2026-09-28

Initial public Praxis Client release for Bedrock 1.26.51.01 / LeviLamina Client 26.51.5.
NoRender, ChunkBorders, Fullbright, ShulkerPreview, RedstoneOverlay, LightOverlay, Zoom and Praxis GUI.
