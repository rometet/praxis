# Contributing to Praxis

Praxis Client is a publicly released, open-source Minecraft Bedrock client mod. This repository contains the redistributable source for published releases; the latest supported target and current restrictions are documented in [README.md](README.md) and [Releases](https://github.com/rometet/praxis/releases).

## Before contributing

- Check existing issues and pull requests. Discuss significant features or design changes before investing in a large implementation.
- Base your work on the latest `main`, and use a **short-lived branch for one focused change**.
- Open a pull request with a clear purpose, affected feature(s), validation results, and any remaining runtime limitations.
- Review the changed-file list. Remove unrelated diagnostics, local scripts, generated assets, binaries, and accidental files before merging.
- Prefer **squash merging** accepted PRs, then deleting their temporary branches. Release tags are immutable historical records: do not rewrite them or delete them as part of branch cleanup.
- Avoid direct commits to `main` for routine assisted edits. Never force-push or delete `main`.
- Keep incomplete or unmerged work in a clearly labeled draft PR. Do not delete branches containing unique work merely to reduce branch counts.

## Validation

Include reproducible checks appropriate to the change: compile/link checks for native hooks, relevant unit or UI tests, and Minecraft runtime observations where necessary. **Build success is not proof of in-game behavior.** Record what was not tested and preserve the release's compatibility and experimental-feature guards.

For source/build instructions see [SOURCE_BUILD.md](SOURCE_BUILD.md) and for distribution requirements see [RELEASE_POLICY.md](RELEASE_POLICY.md).

## Files, privacy, and licensing

Do not commit compiled DLL/PDB/EXE files, generated build trees, credentials, logs, crash dumps, private server/world data, proprietary reference code, or downloaded dependencies. Distribute binaries and source archives through tagged GitHub Releases, not the source tree.

Submit only code and assets you have the right to redistribute. Retain copyright and third-party notices. Unless separately agreed, Praxis-owned contributions are provided under **GPL-3.0-or-later**. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
