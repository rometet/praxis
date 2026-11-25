#pragma once
// Outline-accurate block raycast for the crosshair block-information panel.
//
// PORTED FROM: Insight v0.6.0 (src/Raycast.{h,cpp}), MIT, Copyright (c) 2026
// neverforward. https://github.com/neverforward/Insight  See THIRD_PARTY_NOTICES.md.
//
// Why this exists rather than `ClientInstance::getLatestHitResult()`: the engine's
// latest hit result is the *pick* target the game itself computed, which is a full
// cube by default. A torch, fence post, flower, button or slab then reads as a solid
// cell, so aiming at the empty half of a torch still reports the torch. Insight's
// raycast asks the engine for each block's own outline shape, so the ray passes
// beside a thin block and reaches whatever stands behind it, and it re-confirms the
// hit against the block's own shape because the engine's clip can name the cell above
// the aimed one.
//
// Changes from the original: namespace, no dependency on Insight's Config, and the
// result is a plain value type that owns every string (nothing holds an SDK pointer
// past the call).
#include <optional>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

namespace praxis::companion {

struct BlockRaycastHit {
    bool        hitBlock = false;
    BlockPos    pos{};              // integer coordinates of the hit block
    std::string typeName;           // e.g. "minecraft:stone"
    std::string descriptionId;      // e.g. "tile.stone.stone"
    std::string itemDescriptionId;  // the block's item form, e.g. "item.stone" ("" when it has none)
    double      distance = 0.0;     // distance from `from` to the hit point
};

// Cast a ray from `from` along `dir` through the blocks of `region`, honouring each
// block's own outline. Air is transparent; when `passLiquids` is true water and lava
// are too. Returns nothing when the ray reaches `maxDistance` without touching
// anything.
[[nodiscard]] std::optional<BlockRaycastHit> raycastToBlock(BlockSource& region, Vec3 const& from,
                                                            Vec3 const& dir, double maxDistance,
                                                            bool passLiquids);

// True when a block is one of the liquid blocks (water / lava) by material, which
// covers the flowing variants without an id list.
[[nodiscard]] bool isLiquidBlock(Block const& block);

} // namespace praxis::companion
