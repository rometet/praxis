# Public Release and Source Correspondence Policy

This document defines the minimum publication requirements for Praxis Client source and binary releases.

## 1. Canonical public source

The canonical public source repository is:

`https://github.com/rometet/praxis`

The public repository is maintained independently from the private development history. Public source is published from an audited current tree rather than by exposing the historical development repository.

## 2. Source must be public before the binary release

A public binary release may be published only after the corresponding source commit is present in this repository.

That source state must have:

- an immutable release tag;
- an exact commit SHA;
- the applicable `LICENSE`;
- the applicable `THIRD_PARTY_NOTICES.md`; and
- all third-party license/notice files required by the material actually distributed.

## 3. Required binary metadata

The README included inside every public binary package must contain:

- source repository URL: `https://github.com/rometet/praxis`;
- source release tag;
- exact source commit SHA;
- Minecraft target version; and
- SHA-256 of the released binary.

The GitHub Release description should contain the same source tag, source commit SHA, target version, and binary SHA-256.

## 4. Correspondence requirement

The tagged public source must correspond to the source used to produce the released binary.

If a binary is rebuilt after any source change that affects the executable, the binary hash and source commit/tag metadata must be updated before publication.

## 5. Publication boundary

Do not publish private development history, internal provenance/evidence archives, build output, credentials, private server data, or non-redistributable reference material as part of a public source or binary release.

Only material cleared for redistribution and required for the public source/build/distribution may be included.

## 6. Current state

The current v0.1.7-rc.2 client prerelease targets Minecraft Bedrock 1.26.60.05 and pinned LeviLamina Client 26.60.0. Earlier 26.51 releases remain accessible. Every released asset has a tagged public source commit and SHA-256 metadata.
