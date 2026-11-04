#pragma once
#include "core/FlexiblePlacementPolicy.h"
#include "core/HotkeyPolicy.h"
#include <cmath>
#include <string_view>

namespace praxis::companion {
struct AccuratePlacementOptions {
    // Independent of R/G/H flexible modes; no saved block-state mutation.
    int intoKey = 'I';
    int reverseKey = 'O';
    unsigned intoModifiers = anyModifiers;
    unsigned reverseModifiers = anyModifiers;
    bool operator==(AccuratePlacementOptions const&) const noexcept = default;
};
enum class AccurateBlockKind : unsigned char {
    Unsupported, Piston, Observer, Facing6, HorizontalFacing
};
inline constexpr bool accurateEndsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() && s.substr(s.size()-suffix.size()) == suffix;
}
inline constexpr AccurateBlockKind accurateBlockKind(std::string_view name) noexcept {
    if (accurateEndsWith(name,"sticky_piston") || accurateEndsWith(name,":piston"))
        return AccurateBlockKind::Piston;
    if (accurateEndsWith(name,":observer")) return AccurateBlockKind::Observer;
    if (accurateEndsWith(name,":dropper") || accurateEndsWith(name,":dispenser"))
        return AccurateBlockKind::Facing6;
    if (accurateEndsWith(name,":furnace") || accurateEndsWith(name,":blast_furnace")
        || accurateEndsWith(name,":smoker") || accurateEndsWith(name,":chest")
        || accurateEndsWith(name,":trapped_chest") || accurateEndsWith(name,":ender_chest")
        || accurateEndsWith(name,":barrel")) return AccurateBlockKind::HorizontalFacing;
    return AccurateBlockKind::Unsupported;
}
struct AccurateRotation {
    bool valid{};
    float yaw{}, pitch{};
    FlexibleFace targetFace{FlexibleFace::North};
};
// Into: The piston/dispenser output should point into the clicked side.
// Tweakeroo treats the observer specially: its sensing face points away
// from the clicked block so its redstone output points into it.
// The Bedrock native placement request requires player view angles, not
// an invented Carpet Java hit-coordinate protocol. For piston-like blocks
// the player LOOK is the opposite of the block's facing. This policy maps
// the requested facing into the view vector which Bedrock receives.
// Reverse alone flips the vanilla view vector; Into+Reverse flips Into.
inline AccurateRotation accurateRotation(
    AccurateBlockKind kind, FlexibleFace clickedFace,
    float playerYaw, float playerPitch, bool into, bool reverse) noexcept {
    AccurateRotation out{};
    if (kind == AccurateBlockKind::Unsupported || !validFlexibleFace(clickedFace)
        || (!into && !reverse) || !std::isfinite(playerYaw) || !std::isfinite(playerPitch)
        || std::fabs(playerPitch)>90.0f) return out;
    if (into) {
        // For horizontal-only devices vertical Into does not make sense.
        if (kind==AccurateBlockKind::HorizontalFacing && !isFlexibleHorizontal(clickedFace))
            return out;
        auto look = kind == AccurateBlockKind::Observer
            ? oppositeFlexibleFace(clickedFace) : clickedFace;
        if (reverse) look=oppositeFlexibleFace(look);
        // targetFace is the actual desired block-facing state, not the
        // simulated view vector. Piston Into on a floor (clicked Up) must
        // point Down; observer sensing facing is the inverse polarity.
        out.targetFace=oppositeFlexibleFace(look);
        out.pitch=look==FlexibleFace::Up ? -89.5f
            : look==FlexibleFace::Down ? 89.5f : 0.0f;
        if (isFlexibleHorizontal(look)) {
            auto const horizontal = look==FlexibleFace::North ? FlexibleHorizontal::North
                : look==FlexibleFace::South ? FlexibleHorizontal::South
                : look==FlexibleFace::East ? FlexibleHorizontal::East
                : FlexibleHorizontal::West;
            out.yaw = desiredFlexibleYaw(horizontal);
        } else {
            out.yaw = playerYaw; // preserve yaw for vertical actions
        }
    } else {
        out.yaw = std::remainder(playerYaw + 180.0f,360.0f);
        out.pitch = -playerPitch;
        out.targetFace=clickedFace;
    }
    out.valid=std::isfinite(out.yaw)&&std::isfinite(out.pitch);
    return out;
}
inline constexpr bool accurateActionKeyConflict(
    HotkeyChord one, HotkeyChord two) noexcept {
    return hotkeyConflict(one,two);
}
} // namespace praxis::companion
