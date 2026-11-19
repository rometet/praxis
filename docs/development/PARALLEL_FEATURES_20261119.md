# Praxis concurrent feature development contract (2026-11-19)

Base: `rometet/praxis` main commit `15be75f19c12523576aada3fbfbfe584eb60e96b`.
Target: Minecraft Bedrock Windows 1.26.60.05 / LeviLamina Client 26.60.0.
This is an implementation contract, not a statement that the three features are complete.

## Branch/owner isolation
- `feat/tab-tps` — GPT-6: client TabTPS measurement/state + optional network packet, isolated server-side bridge.
- `feat/command-keys` — Command Code DeepSeek: keyboard/mouse macros.
- `feat/quick-trade` — Mistral DeepSeek: trading automation by explicit user click.
- Each agent uses a separate clone or worktree. No agent pushes directly to `main` or edits another agent's working tree.
- Each branch starts at the base above, or intentionally rebases on the common integration branch only after review. Agents publish individual PRs; integrator handles conflicts.
- No stopping of BDS, Minecraft, or existing developer processes for tests without coordination.
- No new native hook if an existing listener/adapter can be used. No hot unloading Praxis DLL.

## Existing-code audit and migration
- Production sources live in `companion/src` with `companion/xmake.lua` as build definition.
- `core/FeatureSlots.h` currently uses persisted 31-feature layout with 32-bit masks and retired slots. **Do not renumber, recycle, or extend beyond the mask**.
- New features initially use independent, named settings and render/UI sections rather than forcing new indexed `Feature` values. Any registry redesign requires separate design, migration tests and integrator approval.
- `core/HotkeyPolicy.h` is canonical for Ctrl=1/Shift=2/Alt=4/Any=8; respect existing persisted values.
- Shared ownership: `core/Settings.h`, `core/FeatureSlots.h`, `runtime/ConfigStorage*`, `gui/GuiBridge.cpp`, `platform/Native.cpp`, `companion/xmake.lua`. An agent may propose a minimal integration patch, but should prioritize self-contained implementation/test files.

## Contract boundaries
- **TabTPS:** server sends actual measured TPS and full-frame MSPT via optional versioned LL custom packet; client validates, displays Fresh/Stale/Unsupported and never fabricates remote TPS. See `TABTPS_PROTOCOL_V1.md`.
- **CommandKeys:** macro core schedules actions non-blockingly; dispatches only through validated existing client chat/command path and respects contexts/focus; never auto-grants permissions. Start with deterministic Send/Cycle/Repeat/Type, bounded rates, release-to-stop.
- **QuickTrade:** reuse `platform/ItemScrollerExtras.cpp`, `InventoryRequest.cpp`, `InventoryActions.cpp`, and trade screen state. Never issue unacknowledged duplicate trades. Stop on screen/player/container changes, inventory invalidation, rejection, full inventory, exhausted offer or user cancellation. No synthetic item insertion.

## PR acceptance gates
1. Target branch from known base; no unrelated source changes or accidental personal data/secrets.
2. Module unit tests pass, integration build with pinned LL toolchain passes, and no new crash regression.
3. Manual Windows Bedrock test checklist included (never claim passed before user actually verifies).
4. Config migration, world exit/re-enter and restart tested; feature off remains zero-impact.
5. Privacy/legal notice: use original implementation; acknowledge licenses of references, no copying Java binary code.
6. Release only after three-way compatibility audit, including macro key focus during trade and HUD draw during server disconnect.

## Current progress at kickoff
- TabTPS transport-independent v1 wire codec, values policy, receiver lifetime and unit tests initiated on `feat/tab-tps`.
- Network registration, server tick measurement, GUI rendering, game integration and real-world runtime tests are NOT complete yet.
- Command Code and Mistral are NOT automatically running merely because their handoff files exist; their processes must be launched separately.
