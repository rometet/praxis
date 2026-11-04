#pragma once

#include "core/FlexiblePlacementPolicy.h"

namespace praxis::companion {

// A stair's horizontal facing is controlled by the placement yaw, independently
// of its clicked face. For a rotation-only click on a floor, keep that real
// floor face for every sector: changing it would unnecessarily require a side
// support (or a ceiling for CENTER) and could produce upside-down stairs.
// Position modifiers, wall/ceiling clicks and every other block retain their
// existing Tweakeroo-style face transformation.
inline FlexiblePlacementPlan preserveFloorSupportForStairRotation(
    FlexiblePlacementPlan plan, bool isStair) noexcept {
    if (!plan.valid || !isStair || plan.originalFace != FlexibleFace::Up
        || static_cast<unsigned>(plan.modes) != static_cast<unsigned>(FlexibleRotation))
        return plan;
    plan.useSupport = plan.originalSupport;
    plan.placementFace = FlexibleFace::Up;
    // Preserve the independently selected desiredYaw and the exact destination.
    return plan;
}

inline FlexiblePlacementPlan planFlexiblePlacementForItem(
    FlexibleCell support, FlexibleFace face, FlexibleUV viewUv, unsigned modes,
    FlexibleHorizontal playerFacing, bool isStair) noexcept {
    return preserveFloorSupportForStairRotation(
        planFlexiblePlacement(support, face, viewUv, modes, playerFacing), isStair);
}

} // namespace praxis::companion
