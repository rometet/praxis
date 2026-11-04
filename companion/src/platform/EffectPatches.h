#pragma once
#include "core/RenderPolicy.h"
namespace praxis::companion {
RenderMask inspectEffectPatches();
bool updateEffectPatches(RenderMask requested) noexcept;
bool restoreEffectPatches() noexcept;
}
