# CommandKeys policy-only milestone

- C++20 scheduling logic is implemented in `src/core/CommandKeysPolicy.h`; validation/configuration model in `CommandKeysOptions.h`.
- Supported logical modes: Send sequential payloads, Cycle one payload per activation, Repeat bounded sequence while held, and Type (emit text-only action). Caller performs actual chat/command dispatch and text-entry.
- Requires user key activation and focused world context; no automatic startup execution.
- Action text disallows control/newline characters, max 256 bytes. Delay range is 100..60000 ms; a single poll emits at most one action.
- Normalized key/modifier conflicts are checked using existing `HotkeyPolicy.h`. Existing persisted numeric feature slots are untouched.
- This is NOT yet wired to physical keyboard/mouse events, chat submit, named-server profiles, persistence, or GUI. Do not claim it is a functioning in-game mod before implementing and validating adapters.
- Next: implement event adapter reusing Praxis input flow; save under a named config object with migration tests; add settings UI without claiming separate Feature slot; test against LL pinned 26.60 and actual gameplay.
- Tests: `tests/CommandKeysPolicyTests.cpp`.
