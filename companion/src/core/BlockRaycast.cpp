// PORTED FROM: Insight v0.6.0 (src/Raycast.cpp), MIT, Copyright (c) 2026 neverforward.
// https://github.com/neverforward/Insight  See THIRD_PARTY_NOTICES.md.
//
// Changes from the original, all mechanical: namespace `insight` -> `praxis::companion`,
// `RaycastResult` -> `BlockRaycastHit`, and the `#include "Raycast.h"` path. The ray
// marching strategy, the outline-shape clip, the `shouldCheckBlock` predicate and the
// own-shape re-confirmation are unchanged -- those are the parts that make a torch,
// fence post, flower or button hit where it actually looks.
#include "core/BlockRaycast.h"

#include <cmath>

#include "mc/world/item/ItemInstance.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/phys/HitResult.h"

namespace praxis::companion {

// Whether a block is a liquid is engine data (its material says so), which covers
// water, lava and their flowing variants without an id list.
bool isLiquidBlock(Block const& block) {
    try {
        return static_cast<bool>(block.getMaterial().mLiquid);
    } catch (...) {
        return false;
    }
}

namespace {

double length(Vec3 const& v) {
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y
                     + static_cast<double>(v.z) * v.z);
}

} // namespace

std::optional<BlockRaycastHit> raycastToBlock(BlockSource& region, Vec3 const& from, Vec3 const& dir,
                                              double maxDistance, bool passLiquids) {
    double const len = length(dir);
    if (len < 1e-6) return std::nullopt;
    double const reach = maxDistance > 0 ? maxDistance : 1.0;
    Vec3 const   unit{
        static_cast<float>(static_cast<double>(dir.x) / len),
        static_cast<float>(static_cast<double>(dir.y) / len),
        static_cast<float>(static_cast<double>(dir.z) / len),
    };
    Vec3 const to{
        static_cast<float>(static_cast<double>(from.x) + static_cast<double>(unit.x) * reach),
        static_cast<float>(static_cast<double>(from.y) + static_cast<double>(unit.y) * reach),
        static_cast<float>(static_cast<double>(from.z) + static_cast<double>(unit.z) * reach),
    };

    // A block counts as something to hit when it is not air, and when its liquid-ness
    // does not make it see-through for this player's settings.
    auto const checkBlock = [passLiquids](BlockSource const&, Block const& block, bool) {
        if (block.isAir()) return false;
        return passLiquids ? !isLiquidBlock(block) : true;
    };

    // The engine's own pick, with the outline shapes of each block: this is what makes a
    // torch, a fence post, a flower or a button something the ray can pass beside.
    // Marching the ray by hand and taking the first non-air cell treated every block as
    // a full cube, so looking at the gap next to a torch still reported the torch.
    HitResult const hit = region.clip(from, to, false, ShapeType::Outline,
                                      static_cast<int>(reach) + 1, false, false, nullptr, checkBlock,
                                      false);
    if (hit.mType != HitResultType::Tile) return std::nullopt;

    BlockPos const pos{hit.mBlock.x, hit.mBlock.y, hit.mBlock.z};
    // The engine's clip can name the cell above the aimed one for some shapes; ask that
    // block itself whether the ray meets its outline, and keep the answer only when it
    // agrees.
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return std::nullopt;
    // Everything the caller needs is copied out of the engine BEFORE any further engine call.
    // `block` is a reference into the chunk's palette and `region` is the dimension's own
    // mutable source; the clip below can restack either (chunk swap, reload, dimension
    // change), so reading them after it is a use-after-free that faults with 0xC0000005 and
    // cannot be caught. The HUD calls this once per frame, which is why it surfaced there.
    BlockRaycastHit result;
    result.hitBlock      = true;
    result.pos           = pos;
    result.typeName      = block.getTypeName();
    result.descriptionId = block.getDescriptionId();
    HitResult const own = block.clip(region, pos, from, to, ShapeType::Outline, {});
    if (own.mType != HitResultType::Tile) return std::nullopt;
    // The item the block turns into is what the player sees named in the inventory, and
    // the engine's tables are keyed by that item form far more reliably than by the
    // legacy tile id.
    // DO NOT call block.asItemInstance() here. Verified in game 2026-11-24: on
    // the first successful hit it faults with 0xC0000005 (every miss is fine, so
    // it only shows up once the player looks at a block). It only filled
    // itemDescriptionId, which the HUD never reads, so that field stays empty.

    result.distance =
        length(Vec3{hit.mPos.x - from.x, hit.mPos.y - from.y, hit.mPos.z - from.z});
    return result;
}

} // namespace praxis::companion
