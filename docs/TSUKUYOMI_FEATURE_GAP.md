# Tsukuyomi -> Praxis independent feature roadmap (updated 2026-11-13)

Source reference: https://github.com/koumei0722/TsukuyomiClient
Target: https://github.com/rometet/praxis, Bedrock 1.26.60.05 / LeviLamina Client 26.60.0.

## Legal and safety boundary

TsukuyomiClient's public GitHub repository does not publish a LICENSE file (checked 2026-11-13).
Public visibility is not permission to copy. Implement functional behavior independently.
Do not paste third-party source, copied signatures, addresses, decompiled layouts or resources.
No new native hooks, offsets, patches, trampolines or memory scans are used for these features.
Only LeviLamina public SDK/events and existing Praxis abstractions are used.

## Status

| Priority | Feature | Status | Implementation route |
|---|---|---|---|
| 1 | DeathLogger | Implemented (offline verified; in-game pending) | Dedicated bounded 64-entry log, exact local-world identity, checksummed async persistence, F10 page; map last-death marker retained |
| 2 | AutoTool | Implemented (offline verified; in-game pending) | `Item::getDestroySpeed` candidate selection, durability-aware, per-target edge, optional return slot, public tick/mouse events |
| 3 | EffectTimer | Implemented (offline verified; in-game pending) | `Actor::getAllEffects()` / `EffectDuration`, own HUD panel and placement |
| 4 | AngleSnap | Axis locks implemented; step snapping unavailable | Public writable mouse dx/dy axis locks. No exported safe writable rotation channel in this SDK |
| 5 | ToggleSneakSprint | Implemented (offline verified; in-game pending) | Public key event toggles + `setSneaking`/`setSprinting`, manual override and reset rules |
| 6 | AppleSkin | Implemented (offline verified; in-game pending) | Real hunger/saturation/exhaustion attributes when syncable; held-food nutrition/saturation preview; unknown stays unknown |
| 7 | InventoryHUD | Excluded by explicit user requirement | Feature slot 23 remains retired and never enabled |
| 8 | ItemScroller craft/trade | Recipe favorites/apply implemented; trade capture only | Public container-manager virtuals, request barrier, strict confirmation. No exported trade-selection write API |
| 9 | DebugScreen extensions | Implemented (offline verified; in-game pending) | Client tick/delta, observed server tick rate, looking-at, world diagnostics; unavailable metrics explicit |
| 10 | MaterialCollector | Unavailable (documented) | LHolo has no public material-list/export API; only HUD layout/hotkey API is public |

## Deferred / not copied

Tsukuyomi Schematica and FlexiblePlacement overlap LHolo/Praxis existing native owners.
Replay, bot sessions and proprietary/native signature logic require separate design,
compatibility and safety review. No trade auto-execution is added because the public
SDK exposes no exported selection/transaction path.

## DeathHistory / DeathLogger acceptance

- map enabled and 'last death' enabled: each death transition yields one map entry
- newest first, bounded to 64; local calendar time in F10, epoch seconds when copied
- old-world events are inaccessible, and the session history clears on world retirement
- invalid coordinates/dimensions are rejected without changing history
- dedicated DeathLogger additionally listens to `PlayerDieEvent`, uses exact local
  world identity when available, keeps session-only worlds in memory, and writes a
  bounded checksummed `.pdl` file through an async atomic writer
- live 26.60 GUI and real-game checks remain a grouped human session; see
  `GOAL_STATUS.md`

No changes to the working Minecraft installation or to upstream GitHub were made.
