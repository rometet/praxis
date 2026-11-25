# Insight v0.6.0 — source files ported into PraxisCompanion

Upstream: https://github.com/neverforward/Insight
Commit:   faff2bb5bddaa013c21d386cb90873f423dd0270  (tag v0.6.0)
Licence:  MIT, Copyright (c) 2026 neverforward (see LICENSE beside this file)

Ported:
  src/Raycast.h   -> companion/src/core/BlockRaycast.h
  src/Raycast.cpp -> companion/src/core/BlockRaycast.cpp

Changes: namespace insight -> praxis::companion; RaycastResult -> BlockRaycastHit;
include path; no dependency on Insight's Config/PlatformLogic. Algorithm, outline-shape
clip, shouldCheckBlock predicate and own-shape re-confirmation are unchanged.

The unmodified upstream files are kept beside this notice for diffing
(ORIGINAL-Raycast.h / ORIGINAL-Raycast.cpp).
