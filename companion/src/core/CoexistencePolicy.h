#pragma once
#include "core/FeatureSlots.h"
#include <array>
#include <algorithm>

namespace praxis::companion {

// Potential overlaps are based on Lamium 0.2.3's published feature list.
// Only Praxis switches may be changed; never infer Lamium's enabled state.
inline constexpr std::array<Feature,15> lamiumOverlapCandidates{
    Feature::ChunkBorder, Feature::Fullbright, Feature::ShulkerPreview,
    Feature::LightOverlay, Feature::Zoom, Feature::Hitbox,
    Feature::ToolProtection, Feature::WeaponSwitch, Feature::HandRestock,
    Feature::InventorySort, Feature::InventoryTransfer, Feature::Freelook,
    Feature::FreeCamera, Feature::AntiEffect, Feature::OffhandSwap
};
inline constexpr bool potentialLamiumOverlap(Feature f) noexcept {
    return std::find(lamiumOverlapCandidates.begin(),lamiumOverlapCandidates.end(),f)
        != lamiumOverlapCandidates.end();
}
inline constexpr bool mayDisablePraxisOverlap(bool lamiumLoaded, Feature f, bool currentlyOn) noexcept {
    return lamiumLoaded && currentlyOn && potentialLamiumOverlap(f);
}
static_assert(!potentialLamiumOverlap(Feature::Resource));
static_assert(!potentialLamiumOverlap(Feature::RedstoneOverlay));
static_assert(!potentialLamiumOverlap(Feature::WeatherChanger));
} // namespace praxis::companion
