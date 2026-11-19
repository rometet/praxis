# HANDOFF — Command Code / DeepSeek: CommandKeys module

Repository: https://github.com/rometet/praxis
Base commit: 15be75f19c12523576aada3fbfbfe584eb60e96b
Feature branch: feat/command-keys (MUST be separate from feat/tab-tps and feat/quick-trade).
Target: Minecraft Bedrock for Windows 1.26.60.05, LL Client 26.60.0.
Reference functionality, not source to copy: https://modrinth.com/mod/commandkeys

## Mission
Implement a native Bedrock equivalent of CommandKeys for Praxis, scoped to a single client module and tests. Work independently until PR ready. Do not merge to main or stop active development/BDS.

## Deliverables
1. Data model for named profiles and macros with Send, Cycle, Repeat and Type; optional Random later.
2. Key/mouse chord binding using existing `core/HotkeyPolicy.h` and input adapter; independent conflict policy (ignore/both/suppress only if safe).
3. Non-blocking scheduler with bounded 100..60000ms delays and explicit cancellation on world change, screen focus changes, player logout, or feature disabled.
4. Send through existing legitimate chat/command client path; respect server permission. No direct server command execution or privilege escalation.
5. Settings persistence/migration (do not modify existing numeric `Feature` slots), GUI controls, key conflict view.
6. Unit tests for schedule/cycle/repeat/release/duplicate bindings/migration; compile checks and manual test checklist.

## Non-negotiable integration rules
- `companion/src/core/FeatureSlots.h` has 31 persisted slots in a 32-bit mask; don't renumber or extend arbitrarily.
- Avoid direct overlapping edits to `Settings.h`, `Native.cpp`, `GuiBridge.cpp`, `xmake.lua`; document minimal patch proposals separately.
- The user's TabTPS branch is working in parallel; do not base work on or edit its files.
- All keys default OFF; avoid spamming chat; require explicit user trigger and support immediate stop.
- No installing/disabling services or terminating Minecraft/BDS without user's involvement.
- PR only after builds/tests with failure conditions described.

## Completion report
Report commit SHA, PR link, changed files, tests run with output, remaining runtime test steps, known limitations. Never claim actual gameplay verification without it.
