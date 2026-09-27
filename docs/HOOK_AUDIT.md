# Hook audit

Companion utility hooks use LeviLamina's named type hook API. The standalone GUI has separate DXGI COM method detours installed only when LHolo is absent. Failure to remove a hook pins the DLL. The world overlay hook calls vanilla first; suppression hooks omit vanilla only while the corresponding NoRender pass is enabled. Hooks execute on Minecraft's rendering/game threads according to the target; no additional background scanner thread exists.

| TARGET | PURPOSE | WHY_REQUIRED | PUBLIC_API_ALTERNATIVE | THREAD | INSTALL_CONDITION | ORIGIN_CALL_POLICY | REMOVE_PATH | FAILURE_MODE | LHolo_TARGET_COLLISION |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ActorRenderDispatcher::render` (2 overloads) | Entities, items, lightning | Pass suppression | None equivalent | Render | Mod enable | Unless suppressed | `removeNative` | Abort enable/pin on cleanup failure | NO |
| `LevelRendererPlayer::$renderBlockEntities` (hide) | Block entities | Pass suppression | None equivalent | Render | Mod enable | Unless suppressed | `removeNative` | Same | NO |
| `LevelRendererPlayer::$renderBlockEntities` (overlay) | World overlays | Typed render context | None equivalent for 3D mesh | Render | Mod enable | Exactly once, before Companion mesh | `removeNative` | Same | NO |
| `LevelRendererCamera::renderSky` | Sky | Pass suppression | None equivalent | Render | Mod enable | Unless suppressed | `removeNative` | Same | NO |
| `ParticleRenderer::renderParticles` | Particles | Pass suppression | None equivalent | Render | Mod enable | Unless suppressed | `removeNative` | Same | NO |
| `WeatherRenderObject::$ctor` | Weather | Clear densities after vanilla construction | None equivalent | Render | Mod enable | Exactly once | `removeNative` | Same | NO |
| `LevelRendererPlayer::$setupFog` | Fog | Set typed fog storage after vanilla | None equivalent | Render | Mod enable | Exactly once | `removeNative` | Same | NO |
| `LevelRendererPlayer::getFov` | Zoom | Return adjusted FOV | None equivalent | Render | Mod enable | Exactly once | `removeNative` | Same | NO |
| `LocalPlayer::$normalTick` | Runtime state | Gameplay/player generation observation | Client events do not provide identical timing | Game | Mod enable | Exactly once | `removeNative` | Same | NO |
| `IOptionRegistry::getGamma` | Fullbright | Read vanilla, then override when ready | None equivalent | Game/render | First options capture | Exactly once, always | `removeNative` dynamic unhook | Fail closed | NO |
| `IOptionRegistry::getHideHud/getHideSkyFlash` | HUD/lightning NoRender | Option semantics | None equivalent | Game/render | First options capture | Exactly once | `removeNative` dynamic unhook | Fail closed | NO |
| `EffectPatches` signature scan + 5-byte call patch | Portal/fire overlays | Suppress the exact effects | None equivalent | Render | Capability inspection | Patch restore on disable | `restoreEffectPatches` | Pin DLL if restore fails | NO |
| LL `MouseInputEvent` listener | Zoom wheel and relative motion | Public event supplies typed data | Already public | Input | Mod enable | LL owns its hook | `removeListener` | Pin DLL if unsubscribe fails | LHolo may consume earlier when its menu owns input |
| LL `ClientExitLevelEvent` listener | Reset caches/readiness | Public lifecycle event | Already public | Game | Mod enable | LL owns its hook | `removeListener` | Pin DLL if unsubscribe fails | NO |
| LL `AfterUIRenderEvent` listener | Decode current shulker hover and draw owned 27-slot preview with native icons | Public UI render context and hovered `SlotData` | Already public | UI render | Mod enable; draw only if enabled | LL owns its hook | `removeShulkerPreview` | Pin DLL if unsubscribe fails | NO |
| `IDXGISwapChain::Present/Present1` | Standalone GUI/Chunk HUD | Own ImGui frame without LHolo | LL UI events do not own ImGui | Render | Standalone only | Exactly once | `StandaloneGuiHost::stop` | Fail GUI selection/pin on unhook failure | NO: installed only without LHolo |
| `IDXGISwapChain::ResizeBuffers/ResizeBuffers1` | Release GUI graphics before resize | Backbuffer lifetime | None for own DXGI host | Render | Standalone only | Exactly once | `StandaloneGuiHost::stop` | Release resources, fail/pin on unhook failure | NO: installed only without LHolo |
| `ID3D12CommandQueue::ExecuteCommandLists` | Capture real direct queue for D3D11On12 | D3D12 UI rendering | None for standalone backend | Render | Standalone D3D12 only | Exactly once | `StandaloneGuiHost::stop` | D3D12 UI stays unavailable until queue captured | NO: installed only without LHolo |
| `WndProc` subclass | F10 and ImGui Win32 input | Title screen GUI access | LL events cannot own ImGui Win32 lifecycle | UI | First standalone Present | Call previous for noncaptured messages and F11 | `StandaloneGuiHost::stop` | Fail/pin on ownership mismatch | NO: installed only without LHolo |
| LL `KeyInputEvent/MouseInputEvent` listeners | Suppress gameplay input while standalone GUI is visible | Prevent camera/attack/use | Public events | Input | Standalone only | LL owns its hook | `StandaloneGuiHost::stop` | Fail/pin on unsubscribe failure | NO: bridge mode uses LHolo owner |

With a compatible bridge, LHolo alone owns the graphics/input hooks and Companion only registers callbacks and F10 with LHolo's existing owner. Without LHolo, Companion owns its own DXGI/Win32 backend. An installed LHolo without a compatible bridge blocks GUI hook installation.
