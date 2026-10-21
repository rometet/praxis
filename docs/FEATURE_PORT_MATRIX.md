> Historical v0.1.0 document. See the current README, SOURCE_BUILD.md and release metadata for v0.1.2-rc.1.

# Feature port matrix

This is the current evidence for this branch. `COMPILES` means the exact Fake Headers compile and link; it does not mean Minecraft behavior was observed.

| FEATURE | LEGACY_SOURCE | LEGACY_BEHAVIOR | 26.51_BACKEND | LL_API | FAKE_HEADER | HOOK | CONFIG | GUI | LOGIC_TEST | NATIVE_COMPILE | RUNTIME_REQUIRED | STATUS |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| NoRender | `src/module/modules/render/NoRender.h` | 11 independently restored passes | Existing native adapter | Typed hook API | Actor/renderer/option types | Actor, block entity, sky, weather, particles, fog, effects | v2, 11 flags | 11 controls | 651 policy checks | PASS | YES | COMPILES; game behavior unverified |
| ChunkBorders | `src/module/modules/render/ChunkBorders.h` | 3D lines, region color, distance, height, HUD, budget | Tessellator mesh plus selected GUI frame | Typed SDK; optional LHolo bridge | `BaseActorRenderContext`, `mce::Mesh` | Existing world overlay hook | v2 options | Cards and detail | 17 checks | PASS | YES | COMPILES; game visuals unverified |
| Fullbright | `src/module/modules/render/Fullbright.*` | Gamma override when gameplay ready | Option registry getGamma | Typed vtable hook | `IOptionRegistry`, `IClientInstance` | getGamma | v2; v1 migrated | Brightness control | original-once/readiness tests | PASS | YES | COMPILES; freeze fix unverified in game |
| ShulkerPreview | `src/module/modules/misc/ShulkerPreview*.h` | Hovered NBT, 27 slots and icons; selected hotbar tooltip | Typed hovered `SlotData` or selected `ItemStack`, owned 27-slot snapshot, native item renderer in UI event | LL `AfterUIRenderEvent` | `ContainerScreenController`, `Player::getSelectedItem`, `CompoundTag`, `ItemStack`, `ItemRenderer` | Public UI event listener | v2 preview options including hotbar | Slot/count/scale/background/hotbar controls | 32 policy checks | PASS | YES | COMPILES; native panel/icons and hotbar timing unverified in game |
| RedstoneOverlay | `src/module/modules/render/RedstoneOverlay.h` | Live dust/comparator power; repeater/torch ON/OFF | Center-first budgeted scan and cached native 3D numeric mesh | Typed `BlockSource::getBlock`, `BlockType::getBlockState`, `Block::getState<int>`, `BlockSource::getBlockEntity` | `BlockState`, `ComparatorBlockActor::mOutput`, `DiodeBlock::mOn`, `RedstoneTorchBlock::mOn` | Existing world overlay hook | v2 range/budgets/values | Shared value toggle, range and budgets | 15 checks plus NativeProbe | PASS | YES | COMPILES; lever/button/observer/piston not implemented; game visuals unverified |
| LightOverlay | `src/module/modules/render/LightOverlay.h` | Block/sky/effective light, top surface, budgets and numeric labels | Center-first budgeted scan and cached native 3D numeric mesh | Typed `BlockSource::getBrightnessPair` | `BrightnessPair`, `BlockSource`, `Material` | Existing world overlay hook | v2 full light options | Full light controls | 27 checks | PASS | YES | COMPILES; game light values/visuals unverified |
| Zoom | `src/module/modules/render/Zoom.h` | Hold, FOV, animation, scroll, sensitivity, smoothing | FOV typed hook plus public mouse event | `MouseInputEvent` | `LevelRendererPlayer`, `MouseAction` | getFov | v2; v1 multiplier mapped | Full detail | 14 checks | PASS | YES | COMPILES; game feel unverified |
| Legacy Praxis GUI | `src/gui/ClickGui.h`, `src/gui/ClickGuiStyleSettings.h`, `src/hooks/D3DHook.*`, `src/hooks/InputHook.*` | Modules, detail, Client Settings, Config Manager, Keybinds, About, favorites, card icons, search/category and style | Standalone DXGI/D3D11On12 or optional actual `LHolo.dll` bridge | LL input events and optional LHolo C ABI v2 | `IClientInstance::releaseMouse/grabMouse` | Standalone DXGI/WndProc only when LHolo absent | v2 edits, favorites, named profiles, appearance | Shared content | Backend selection 6 checks; runtime config validation | PASS | YES | COMPILES; standalone and coexistence runtime unverified; font picker not ported |

The overlay/preview capability bits are set only when their backend context exists. ChunkBorders, RedstoneOverlay and LightOverlay probe the native render context; the latter two also require material and texture resources before drawing. ShulkerPreview becomes available when the UI render event has a local player and an `ItemRenderer`. Game behavior still needs runtime verification.

## Redstone component audit

| Component | Status | Evidence | Blocker |
| --- | --- | --- | --- |
| Dust 0–15 | IMPLEMENTED | `redstone_signal` typed block state | Minecraft runtime check pending |
| Comparator output 0–15 | IMPLEMENTED | 26.51.5 `ComparatorBlockActor::mOutput` and `BlockSource::getBlockEntity`; legacy comparator classification | Minecraft runtime check pending |
| Repeater ON/OFF | IMPLEMENTED | 26.51.5 `RepeaterBlock` inherits `DiodeBlock::mOn` | Locking/direction overlay remains unverified |
| Redstone torch ON/OFF | IMPLEMENTED | 26.51.5 `RedstoneTorchBlock::mOn` | Direction overlay remains unverified |
| Lever / button | NOT_IMPLEMENTED | 26.51.5 `LeverBlock` / `ButtonBlock` exist | Typed powered state and placement orientation not verified |
| Observer | NOT_IMPLEMENTED | 26.51.5 `ObserverBlock` exists | Transient pulse state/timing not exposed by a verified typed accessor |
| Piston / sticky piston | NOT_IMPLEMENTED | 26.51.5 `PistonBlock`, `PistonBlockActor` exist | Extended/moving state and safe world rendering not verified |

Legacy `showDirection`, `showPistons`, and `showControls` are not exposed in the Companion GUI because their runtime drawing is not implemented. No inert settings are advertised. Repeater and torch report 0 for OFF and 15 for ON; their exact appearance requires a game check.
