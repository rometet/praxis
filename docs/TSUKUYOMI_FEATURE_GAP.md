# Tsukuyomi -> Praxis independent feature roadmap (2026-11-13)

Source reference: https://github.com/koumei0722/TsukuyomiClient
Target: https://github.com/rometet/praxis, Bedrock 1.26.60.05 / LeviLamina Client 26.60.0.

## Legal and safety boundary

TsukuyomiClient's public GitHub repository does not publish a LICENSE file (checked 2026-11-13).
Public visibility is not permission to copy. Implement functional behavior independently.
Do not paste third-party source, copied signatures, addresses, decompiled layouts or resources.
Any native hook must be constrained to the pinned client version and verified in game.
Prefer existing Praxis/LHolo facilities. No deployment, GitHub push or release without validation.

## Gap and priority (not a completion claim)

| Priority | Feature | Current Praxis capability | Implementation route |
|---|---|---|---|
| 1 | DeathLogger | Only last-death map marker | Local bounded 64-entry session history in Map UI; copyable; implemented in isolated worktree, unit-tested |
| 2 | AutoTool | WeaponSwitch only chooses combat weapons | New mining-tool selection on start-destroy, validated speed/durability and input ownership |
| 3 | EffectTimer | Information HUD, no potion countdown | Native-safe owned status-effect snapshot and configurable HUD panel |
| 4 | AngleSnap | Freelook/FreeCamera, no angular snapping | Independent angle policy, controllable yaw/pitch steps, native input gate |
| 5 | ToggleSneakSprint | No toggle module | Key-state policy, game focus/UI and camera exclusions |
| 6 | AppleSkin | Food HUD exists, saturation often unknown | Source-verified hunger/saturation, non-intrusive food tooltip/preview |
| 7 | InventoryHUD | Old feature slot 23 intentionally retired | New HUD panel with no reuse of persisted slot 23 |
| 8 | ItemScroller craft/trade | Sort/transfer partial | Recipe-grid interactions and trade favorites after native safety probes |
| 9 | DebugScreen extensions | 23 information lines | Display unavailable honestly; bounded system/client diagnostics |
| 10 | MaterialCollector | LHolo manages schematic data | LHolo-controlled materials API, not duplicate schematic renderer |

## Deferred / not copied

Tsukuyomi Schematica and FlexiblePlacement overlap LHolo/Praxis existing native owners.
Replay, bot sessions and proprietary/native signature logic require separate design,
compatibility and safety review. ItemScroller's trade/craft hooks are not safe for
bulk direct transplantation across Minecraft versions.

## DeathHistory acceptance

- map enabled and 'last death' enabled: each death transition yields one entry
- newest first, bounded to 64; local calendar time in F10, epoch seconds when copied
- old-world events are inaccessible, and the session history clears on world retirement
- invalid coordinates/dimensions are rejected without changing history
- live 26.60 GUI + real-game and package build must still be verified
- this is not yet file-based persistent death logging

No changes to working Minecraft installation or to upstream GitHub were made.
