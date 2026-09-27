# Minecraft 26.51.5 runtime test plan

Use LeviLamina Client 26.51.5 and `PraxisCompanion.dll` from Praxis PR #2. First install only `mods/PraxisCompanion/`. Record game build, DLL hashes, config and any crash/freeze log. Start with every Companion feature OFF. Then repeat with optional `mods/LHolo/` from LHolo PR #12.

| Area | Actions | Expected observation |
| --- | --- | --- |
| Standalone title | Boot with only Companion; F10 open/close | GUI visible and interactive before world join; log reports `STANDALONE` |
| Standalone world | Join; F10; edit settings; close/reopen; leave/rejoin | All seven utility controls available; no stale input or resource |
| Standalone lifecycle | F11, resize, Alt+Tab while GUI open/closed; shutdown | Fullscreen and backbuffer rebuild, cursor and held inputs restore, no crash |
| Control | Boot, join, leave, rejoin with LHolo ON and Companion all OFF | No crash, projection and vanilla input unchanged |
| Fullbright | Enable before join at brightness 25; join; enter gameplay; disable; rejoin | Loading completes; vanilla gamma during loading; override only in gameplay; immediate restore |
| NoRender | Enable and disable each of 11 passes separately; leave/rejoin | Each intended vanilla pass disappears and returns without affecting inventory/mod UI |
| ChunkBorders | Test -17, -16, -1, 0, 15, 16 and a 32-chunk region boundary; adjust height/color/width/budget; move across chunks | 3D lines and HUD agree with signed coordinates; no stale mesh or unbounded work |
| Zoom | Hold/release C, animation, scroll both directions, inventory, pause, Alt+Tab, GUI and leave | FOV and sensitivity restore immediately on release/focus loss; no double wheel |
| ShulkerPreview | Empty/partial/full/custom named shulkers, all colors, container hover, selected hotbar shulker and tooltip expiration | 27 preserved slots and native icons; no stale hover pointer |
| LightOverlay | Dark room, daylight, torch, ignore sky, threshold and budgets | Accurate block/sky/effective numeric labels; bounded scan |
| RedstoneOverlay | Dust power 0/15, comparator output 0/15, repeater and torch ON/OFF, live changes, place/remove | Live labels and cache invalidation |
| GUI coexistence | LHolo open/close, Praxis open/close, switch both ways, projection active, F11, resize, Alt+Tab, leave, rejoin, shutdown | One input owner, no stuck WASD/clicks, frozen or invisible cursor, device loss or ResizeBuffers failure |
| Older LHolo | Install LHolo without bridge; boot; F10 | GUI reports `CONFLICT_BLOCKED`; native utilities still work; no second graphics hook |

ShulkerPreview, LightOverlay and RedstoneOverlay compile and link, but their Minecraft observations have not run. Check Shulker empty, partial, full, custom named, color variants, slot counts, malformed tag behavior and hover expiration carefully. Lever, button, observer and piston modes remain open as recorded in the feature matrix. None of the rows above claim a completed game test.
