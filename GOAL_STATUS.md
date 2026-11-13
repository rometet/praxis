# GOAL_STATUS — Tsukuyomi-inspired Praxis features

Date: 2026-11-13
Branch: `feature/tsukuyomi-death-history` (isolated clone)
Target: Minecraft Bedrock Windows 1.26.60.05 / LeviLamina Client 26.60.0 (`f28a4b5d`)
Boundary: no push, no release, no live installation or Minecraft process changes.

## Hard-rule compliance

- All new capability code uses LeviLamina public SDK types, public event listeners
  (`ClientLevelTickEvent`, `MouseInputEvent`, `KeyInputEvent`, `PlayerDieEvent`,
  `ClientExitLevelEvent`, `AfterUIRenderEvent`), documented SDK getters/fields and
  existing Praxis abstractions.
- **No new native hook, detour, trampoline, memory patch/scan, signature/offset
  match or raw packet interception was added.** Verification:
  `rg "LL_TYPE_INSTANCE_HOOK|LL_STATIC_HOOK|ll::memory::hook|resolveIdentifier|HookPriority|VirtualProtect|WriteProcessMemory"`
  over the new files returns no matches. Existing native hooks were not extended.
- Failure-closed paths are explicit; unavailable behavior is never simulated.
- Retired feature slot **23 (InventoryHUD)** is never reused or enabled.

## Feature matrix (all 9 non-InventoryHUD items)

| # | Feature | Status | Implementation | Runtime verification |
|---|---|---|---|---|
| 1 | AutoTool | Implemented | `core/AutoToolPolicy.h`, `runtime/GameplayRuntime.cpp`; best hotbar tool by `Item::getDestroySpeed` (public virtual), durability threshold, per-target edge, cooldown, optional return slot | Policy tests PASS; in-game test pending |
| 2 | EffectTimer | Implemented | `core/EffectTimerPolicy.h`, effect collection in `runtime/InformationRuntime.cpp`, HUD in `render/InformationHud.cpp`; `Actor::getAllEffects()` + `EffectDuration` ticks | Policy tests PASS; in-game test pending |
| 3 | AngleSnap | Partially implemented (axis locks) | `core/AngleSnapPolicy.h`; public writable `MouseInputEvent::dx/dy` axis locks; ramped/snap-to-step rotation is **unavailable** because this SDK exports no safe writable rotation channel (`Actor::setRotationWrapped` is MCNAPI and did not link) | Policy tests PASS; in-game axis-lock test pending |
| 4 | ToggleSneakSprint | Implemented | `core/ToggleSneakSprintPolicy.h`, `runtime/GameplayRuntime.cpp`; `KeyInputEvent` toggles, `Actor::setSneaking` / `Mob::setSprinting`, manual Shift/Ctrl override, reset on GUI/focus/world | Policy tests PASS; in-game test pending |
| 5 | AppleSkin | Implemented | `core/PlayerHudPolicy.h`, `runtime/InformationRuntime.cpp`; real hunger/saturation/exhaustion attributes only when syncable and identity-valid; held-food preview from `Item::getFood()` nutrition/saturation modifier; unknown stays Unknown | Policy tests PASS; in-game test pending |
| 6 | ItemScroller extensions | Recipe favorites + crafting-grid apply implemented; trade favorites captured; automatic trade execution unavailable | `core/ItemScrollerPolicy.h`, `platform/ItemScrollerExtras.cpp`; `AfterUIRenderEvent`, public container-manager virtuals `handlePlaceAmount` + request barrier; strict confirmation, bounded 18 steps, 50 ms spacing, 60 s deadline, cursor/screen/session checks. Trade `setRecipeIndex` is MCNAPI/not exported, so only capture/display is provided | Policy tests PASS; in-game test pending |
| 7 | DebugScreen extensions | Implemented | `core/DebugScreenPolicy.h`, `runtime/InformationRuntime.cpp`, HUD + F10 page; client tick ID/delta, observed server tick-ID rate, looking-at, level ID/dimension/time/chunk/biome/light/memory; server heap/chunk count explicitly Unavailable | Policy tests PASS; in-game test pending |
| 8 | MaterialCollector | Unavailable (documented) | LHolo has no exported material-list/materials API; only its HUD layout/hotkey API is public. Duplicating schematic data or reading LHolo internals is prohibited | UI limitation notice in F10; no runtime claim |
| 9 | DeathLogger | Implemented | `map/DeathHistory.h` preload, `runtime/DeathLogCodec.h`, `runtime/DeathLog.cpp`, F10 “死亡履歴” page; bounded 64, newest-first, exact world identity, checksummed `.pdl`, async atomic writes, session fallback. Existing map last-death marker untouched | Codec/history tests PASS; file I/O and in-game test pending |

## Configuration / GUI

- New persisted feature slots: AutoTool 15, EffectTimer 16, AngleSnap 17,
  ToggleSneakSprint 18, DebugScreen 19. Slot 23 remains retired.
- New option groups: `autoToolOptions`, `angleSnapOptions`,
  `toggleSneakSprintOptions`, `itemScrollerOptions`, plus
  `informationOptions.effectTimer` / `.debugScreen`.
- F10 additions: module cards/details, HUD section for EffectTimer, HUD layout
  editor EffectTimer panel, dedicated Debug page, dedicated Death Log page,
  ItemScroller recipe/trade favorites under InventorySort, MaterialCollector
  limitation text.
- Existing profile `keybinds`/`keybindModifiers` arrays only grow; unknown slots
  stay opaque; no retired slot is reused.

## Verification run in this clone

Commands (from `companion/`):

```
xmake f -m release -y
xmake build -y PraxisCompanion                 # PASS
xmake build -y PraxisCompanionTsukuyomiFeatureTests
xmake build -y PraxisCompanionDeathLogTests
xmake run PraxisCompanionTsukuyomiFeatureTests # 50 checks PASS
xmake run PraxisCompanionDeathLogTests         # 21 checks PASS
xmake run PraxisCompanionPlayerHudTests        # 25 checks PASS
xmake run PraxisCompanionSaturationTests       # 271 checks PASS
xmake run PraxisCompanionInformationPolicyTests# 71 checks PASS
xmake run PraxisCompanionHudLayoutHotkeyTests  # 33 checks PASS
xmake run PraxisCompanionSaturationUiTests     # 437 checks / 58 ImGui frames PASS
xmake run PraxisCompanionDeathHistoryTests     # PASS
```

Build of all targets (`xmake build -y --all`) reaches unrelated pre-existing
target `PraxisCompanionResourcePackSnapshotTypedProbe`, which fails on SDK field
`ResourcePackManager::mFullStack`; this failure is not in changed files.

### Known test-environment limitations

- `PraxisCompanionGuiRenderTests` fails at the 480x800 pack-page width check.
  This was reproduced identically in a clean worktree at baseline commit
  `d4117bb`, so it is pre-existing/environmental and not caused by this work.
- Tests that link LeviLamina (`AntiEffect`, `InventoryHud`,
  `CameraIntegrationConfig`, `ConfigMigration`, `ConfigStorage`) build but cannot
  initialize in a standalone host without the game loader. `SOURCE_BUILD.md`
  already documents this limitation for camera configuration; they require the
  real client environment.

## Minimal human in-game test script (one session where possible)

1. Install this isolated build only into a disposable/test client. Never replace
   the live Praxis/LHolo DLLs.
2. AutoTool: put a correct pickaxe/axe/shovel and a weaker tool in the hotbar;
   mine a matching block. Confirm the tool switches once per target, returns when
   configured, and does not switch while the F10 GUI is open.
3. EffectTimer: drink a potion with a known duration. Confirm the HUD counts down
   in real time and disappears at expiry. Confirm no effect shows invented data.
4. AngleSnap: enable only the yaw lock, then move the mouse vertically; confirm
   horizontal turning is locked and vertical still works. Repeat for pitch.
5. ToggleSneakSprint: press the configured toggle keys; confirm crouch/sprint
   persist, then press the vanilla Shift/Ctrl keys and confirm manual control
   returns. Open F10 and change worlds; confirm toggles reset.
6. AppleSkin: with the food HUD on, hold bread and other food; confirm hunger and
   saturation restore previews match the item. If the server hides the attribute,
   confirm the HUD says Unknown, not a guessed value.
7. ItemScroller: open a crafting table, arrange a recipe, open F10 and save the
   observed grid as a favorite. Close F10, empty the grid, press the apply key
   twice; confirm exactly one item per slot is moved through normal inventory
   transactions. Open a villager trade, confirm the last-trade lines appear.
8. DebugScreen: open F10 → デバッグ. Confirm client/server tick observations,
   looking-at and world rows. Confirm server heap/chunk rows say Unavailable.
9. DeathLogger: die once in a local world with a stable world ID, open F10 →
   死亡履歴, confirm the entry and local time, copy it, restart Minecraft, re-enter
   the same world and confirm the persisted entry. Confirm the map's last-death
   marker still works. Die in a remote/session-only world and confirm the log is
   session-only.
10. MaterialCollector: open F10 → 設計図 and confirm the limitation text; no
    schematic data is imported.

## Remaining blockers / not claimed

- In-game runtime behavior for every new feature is **not** claimed by the
  offline builds. The script above is the required acceptance pass.
- AngleSnap step snapping and villager trade auto-execution are unavailable via
  exported public SDK APIs; both are surfaced as limitations rather than faked.
- MaterialCollector is not implemented because LHolo publishes no material-list
  API.
