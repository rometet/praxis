#pragma once
// Crosshair block information (JADE/WAILA-style) runtime adapter.
//
// Reads the block under the crosshair through the same SDK expressions the placement
// code already uses (ClientInstance::getLatestHitResult + BlockSource::getBlock) and
// formats it with the pure BlockInfoPolicy. Drawing is left to the existing HUD host;
// this module only produces the line, so it stays testable and side-effect free.
#include <string>

namespace praxis::companion {
bool installBlockInfoRuntime();
// True once the runtime is installed AND a local player exists. A cheap gate the
// HUD host uses before paying for a raycast/block read.
bool blockInfoRuntimeReady() noexcept;
// Empty string means "draw nothing" (air, no target, or the module is off).
std::string blockInfoLineForHud();
std::string blockInfoStatus();
} // namespace praxis::companion
