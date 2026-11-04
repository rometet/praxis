#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace praxis::companion {

// Bedrock FacingID order: down, up, north, south, west, east.
enum class FlexibleFace : std::uint8_t { Down = 0, Up, North, South, West, East };
enum class FlexibleRegion : std::uint8_t { Center = 0, Left, Right, Top, Bottom };
enum FlexibleMode : std::uint8_t {
    FlexibleNone     = 0,
    FlexibleRotation = 1u << 0,
    FlexibleOffset   = 1u << 1,
    FlexibleAdjacent = 1u << 2,
};

constexpr FlexibleMode operator|(FlexibleMode lhs, FlexibleMode rhs) noexcept {
    return static_cast<FlexibleMode>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
}
constexpr bool hasFlexibleMode(unsigned modes, FlexibleMode mode) noexcept {
    return (modes & static_cast<unsigned>(mode)) != 0;
}

struct FlexibleCell {
    int x{};
    int y{};
    int z{};
    constexpr bool operator==(FlexibleCell const&) const noexcept = default;
};

struct FlexibleUV {
    float u{};
    float v{};
    constexpr bool operator==(FlexibleUV const&) const noexcept = default;
};

// A hit point is deliberately kept separate from the SDK Vec3.  The native
// GameMode::useItemOn contract carries this point relative to the clicked
// support block, while HitResult::mPos carries an absolute world point.
struct FlexibleHitPoint {
    float x{};
    float y{};
    float z{};
};

struct FlexiblePlacementPlan {
    bool valid{};
    FlexibleCell originalSupport{};
    FlexibleCell destination{};
    FlexibleCell useSupport{};
    FlexibleFace originalFace{FlexibleFace::Down};
    FlexibleFace placementFace{FlexibleFace::Down};
    FlexibleFace regionDirection{FlexibleFace::North};
    FlexibleRegion region{FlexibleRegion::Center};
    FlexibleMode modes{FlexibleNone};
    float desiredYaw{};
    float desiredPitch{};
};

struct FlexiblePlacementOptions {
    int rotationKey = 'R';
    int offsetKey = 'G';
    int adjacentKey = 'H';
    unsigned rotationModifiers = 8;
    unsigned offsetModifiers = 8;
    unsigned adjacentModifiers = 8;

    constexpr bool operator==(FlexiblePlacementOptions const&) const noexcept = default;
};

inline constexpr int kFlexibleMinY = -2048;
inline constexpr int kFlexibleMaxY = 4096;
inline constexpr int kFlexibleHorizontalLimit = 30'000'000;

inline constexpr bool validFlexibleFace(FlexibleFace face) noexcept {
    return static_cast<unsigned>(face) < 6;
}

inline bool validFlexibleCell(FlexibleCell cell) noexcept {
    return cell.x >= -kFlexibleHorizontalLimit && cell.x <= kFlexibleHorizontalLimit
        && cell.z >= -kFlexibleHorizontalLimit && cell.z <= kFlexibleHorizontalLimit
        && cell.y >= kFlexibleMinY && cell.y <= kFlexibleMaxY;
}

inline bool validFlexibleUV(FlexibleUV uv) noexcept {
    return std::isfinite(uv.u) && std::isfinite(uv.v) && uv.u >= 0.0f && uv.u <= 1.0f
        && std::isfinite(uv.v) && uv.v >= 0.0f && uv.v <= 1.0f;
}

inline std::optional<FlexibleUV> flexibleUVFromRelativeHit(
    FlexibleHitPoint hit, FlexibleFace face) noexcept {
    if (!validFlexibleFace(face)) return std::nullopt;
    float u{}, v{};
    switch (face) {
    case FlexibleFace::Down: case FlexibleFace::Up:
        u = hit.x; v = hit.z; break;
    case FlexibleFace::North: case FlexibleFace::South:
        u = hit.x; v = hit.y; break;
    case FlexibleFace::West: case FlexibleFace::East:
        u = hit.z; v = hit.y; break;
    }
    FlexibleUV result{u, v};
    return validFlexibleUV(result) ? std::optional<FlexibleUV>{result} : std::nullopt;
}

inline std::optional<FlexibleUV> flexibleUVFromAbsoluteHit(
    FlexibleCell support, FlexibleHitPoint hit, FlexibleFace face) noexcept {
    return flexibleUVFromRelativeHit({
        hit.x - static_cast<float>(support.x),
        hit.y - static_cast<float>(support.y),
        hit.z - static_cast<float>(support.z),
    }, face);
}

// Preserve the same selected face-local UV when the planner chooses a new
// clicked support.  The boundary coordinate is intentional: Bedrock's native
// item-use path consumes a hit relative to that support, not a world point.
inline constexpr FlexibleHitPoint flexibleRelativeHit(
    FlexibleFace face, FlexibleUV uv) noexcept {
    switch (face) {
    case FlexibleFace::Down:  return {uv.u, 0.0f, uv.v};
    case FlexibleFace::Up:    return {uv.u, 1.0f, uv.v};
    case FlexibleFace::North: return {uv.u, uv.v, 0.0f};
    case FlexibleFace::South: return {uv.u, uv.v, 1.0f};
    case FlexibleFace::West:  return {0.0f, uv.v, uv.u};
    case FlexibleFace::East:  return {1.0f, uv.v, uv.u};
    }
    return {};
}

inline constexpr FlexibleHitPoint flexibleWorldHit(
    FlexibleCell support, FlexibleFace face, FlexibleUV uv) noexcept {
    auto const local = flexibleRelativeHit(face, uv);
    return {
        static_cast<float>(support.x) + local.x,
        static_cast<float>(support.y) + local.y,
        static_cast<float>(support.z) + local.z,
    };
}

// MaLiLib's HitPart divides the face by distance from the center.  The four
// corner triangles therefore follow the nearer edge; exact diagonal ties use
// the vertical result (bottom/top) deterministically.
inline FlexibleRegion selectFlexibleRegion(FlexibleUV uv) noexcept {
    auto const offH = std::abs(uv.u - 0.5f);
    auto const offV = std::abs(uv.v - 0.5f);
    if (offH > 0.25f || offV > 0.25f) {
        if (offH > offV) return uv.u < 0.5f ? FlexibleRegion::Left : FlexibleRegion::Right;
        return uv.v < 0.5f ? FlexibleRegion::Bottom : FlexibleRegion::Top;
    }
    return FlexibleRegion::Center;
}

// Four vertices describe the actual five-way selection outlines. The corner
// regions are triangles, not rectangular strips, so this stays in sync with
// selectFlexibleRegion() for both the overlay and hit planning.
inline constexpr std::array<FlexibleUV, 4> flexibleRegionPolygon(FlexibleRegion region) noexcept {
    switch (region) {
    case FlexibleRegion::Left:   return {{{0.0f, 0.0f}, {.25f, .25f}, {.25f, .75f}, {0.0f, 1.0f}}};
    case FlexibleRegion::Right:  return {{{1.0f, 0.0f}, {1.0f, 1.0f}, {.75f, .75f}, {.75f, .25f}}};
    case FlexibleRegion::Top:    return {{{0.0f, 1.0f}, {.25f, .75f}, {.75f, .75f}, {1.0f, 1.0f}}};
    case FlexibleRegion::Bottom: return {{{0.0f, 0.0f}, {1.0f, 0.0f}, {.75f, .25f}, {.25f, .25f}}};
    case FlexibleRegion::Center: return {{{.25f, .25f}, {.75f, .25f}, {.75f, .75f}, {.25f, .75f}}};
    }
    return {{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}};
}

inline constexpr FlexibleCell flexibleNormal(FlexibleFace face) noexcept {
    switch (face) {
    case FlexibleFace::Down: return {0, -1, 0};
    case FlexibleFace::Up: return {0, 1, 0};
    case FlexibleFace::North: return {0, 0, -1};
    case FlexibleFace::South: return {0, 0, 1};
    case FlexibleFace::West: return {-1, 0, 0};
    case FlexibleFace::East: return {1, 0, 0};
    }
    return {};
}

inline constexpr FlexibleCell flexibleAdd(FlexibleCell lhs, FlexibleCell rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline constexpr FlexibleCell flexibleOpposite(FlexibleCell value) noexcept {
    return {-value.x, -value.y, -value.z};
}

inline constexpr bool isFlexibleHorizontal(FlexibleFace face) noexcept {
    return face == FlexibleFace::North || face == FlexibleFace::South || face == FlexibleFace::West
        || face == FlexibleFace::East;
}

inline constexpr FlexibleFace oppositeFlexibleFace(FlexibleFace face) noexcept {
    switch (face) {
    case FlexibleFace::Down: return FlexibleFace::Up;
    case FlexibleFace::Up: return FlexibleFace::Down;
    case FlexibleFace::North: return FlexibleFace::South;
    case FlexibleFace::South: return FlexibleFace::North;
    case FlexibleFace::West: return FlexibleFace::East;
    case FlexibleFace::East: return FlexibleFace::West;
    }
    return FlexibleFace::North;
}

enum class FlexibleHorizontal : std::uint8_t { North = 0, East, South, West };

inline FlexibleUV flexibleOrientedUV(FlexibleUV raw, FlexibleFace face,
    FlexibleHorizontal playerFacing) noexcept {
    if (face == FlexibleFace::Down || face == FlexibleFace::Up) {
        FlexibleUV result{};
        switch (playerFacing) {
        case FlexibleHorizontal::North: result = {raw.u, 1.0f - raw.v}; break;
        case FlexibleHorizontal::South: result = {1.0f - raw.u, raw.v}; break;
        case FlexibleHorizontal::West:  result = {1.0f - raw.v, 1.0f - raw.u}; break;
        case FlexibleHorizontal::East:  result = {raw.v, raw.u}; break;
        }
        if (face == FlexibleFace::Down) result.v = 1.0f - result.v;
        return result;
    }
    if (face == FlexibleFace::North) return {1.0f - raw.u, raw.v};
    if (face == FlexibleFace::South) return raw;
    if (face == FlexibleFace::West) return raw;
    return {1.0f - raw.u, raw.v}; // East
}

inline FlexibleUV flexibleRawUVFromOriented(FlexibleUV oriented, FlexibleFace face,
    FlexibleHorizontal playerFacing) noexcept {
    if (face == FlexibleFace::Down || face == FlexibleFace::Up) {
        if (face == FlexibleFace::Down) oriented.v = 1.0f - oriented.v;
        switch (playerFacing) {
        case FlexibleHorizontal::North: return {oriented.u, 1.0f - oriented.v};
        case FlexibleHorizontal::South: return {1.0f - oriented.u, oriented.v};
        case FlexibleHorizontal::West:  return {1.0f - oriented.v, 1.0f - oriented.u};
        case FlexibleHorizontal::East:  return {oriented.v, oriented.u};
        }
    }
    if (face == FlexibleFace::North) return {1.0f - oriented.u, oriented.v};
    if (face == FlexibleFace::South) return oriented;
    if (face == FlexibleFace::West) return oriented;
    return {1.0f - oriented.u, oriented.v}; // East
}

inline std::optional<FlexibleUV> flexibleUVFromRelativeHitForFacing(
    FlexibleHitPoint hit, FlexibleFace face, FlexibleHorizontal playerFacing) noexcept {
    auto const raw = flexibleUVFromRelativeHit(hit, face);
    if (!raw) return std::nullopt;
    auto const oriented = flexibleOrientedUV(*raw, face, playerFacing);
    return validFlexibleUV(oriented) ? std::optional<FlexibleUV>{oriented} : std::nullopt;
}

inline std::optional<FlexibleUV> flexibleUVFromAbsoluteHitForFacing(
    FlexibleCell support, FlexibleHitPoint hit, FlexibleFace face,
    FlexibleHorizontal playerFacing) noexcept {
    auto const raw = flexibleUVFromAbsoluteHit(support, hit, face);
    if (!raw) return std::nullopt;
    auto const oriented = flexibleOrientedUV(*raw, face, playerFacing);
    return validFlexibleUV(oriented) ? std::optional<FlexibleUV>{oriented} : std::nullopt;
}

inline constexpr FlexibleFace faceOf(FlexibleHorizontal facing) noexcept {
    switch (facing) {
    case FlexibleHorizontal::North: return FlexibleFace::North;
    case FlexibleHorizontal::East: return FlexibleFace::East;
    case FlexibleHorizontal::South: return FlexibleFace::South;
    case FlexibleHorizontal::West: return FlexibleFace::West;
    }
    return FlexibleFace::North;
}

inline FlexibleHorizontal horizontalFacingFromYaw(float yaw) noexcept {
    if (!std::isfinite(yaw)) return FlexibleHorizontal::South;
    auto normalized = std::fmod(yaw, 360.0f);
    if (normalized < 0.0f) normalized += 360.0f;
    auto bucket = static_cast<unsigned>(std::floor((normalized + 45.0f) / 90.0f)) % 4u;
    return static_cast<FlexibleHorizontal>((bucket + 2u) % 4u);
}

inline constexpr FlexibleFace rotateHorizontal(FlexibleFace face, bool clockwise) noexcept {
    if (!isFlexibleHorizontal(face)) return face;
    auto const index = face == FlexibleFace::North ? 0u : face == FlexibleFace::East ? 1u
        : face == FlexibleFace::South ? 2u : 3u;
    auto const next = clockwise ? (index + 1u) % 4u : (index + 3u) % 4u;
    return next == 0 ? FlexibleFace::North : next == 1 ? FlexibleFace::East
        : next == 2 ? FlexibleFace::South : FlexibleFace::West;
}

// Tweakeroo's getRotatedFacing mapping, expressed as a POD policy.
inline constexpr FlexibleFace flexibleRegionDirection(
    FlexibleFace face, FlexibleRegion region, FlexibleHorizontal playerFacing) noexcept {
    auto const player = faceOf(playerFacing);
    switch (region) {
    // PlacementTweaks.getRotatedFacing returns originalSide.getOpposite()
    // for CENTER on both horizontal and vertical support faces.  The player
    // facing only orients the UV selection; it must not turn an UP/DOWN
    // center placement into a horizontal placement.
    case FlexibleRegion::Center: return oppositeFlexibleFace(face);
    case FlexibleRegion::Left:
        return isFlexibleHorizontal(face) ? rotateHorizontal(face, false) : rotateHorizontal(player, true);
    case FlexibleRegion::Right:
        return isFlexibleHorizontal(face) ? rotateHorizontal(face, true) : rotateHorizontal(player, false);
    case FlexibleRegion::Top:
        if (isFlexibleHorizontal(face)) return FlexibleFace::Down;
        return face == FlexibleFace::Down ? player : oppositeFlexibleFace(player);
    case FlexibleRegion::Bottom:
        if (isFlexibleHorizontal(face)) return FlexibleFace::Up;
        return face == FlexibleFace::Up ? player : oppositeFlexibleFace(player);
    }
    return FlexibleFace::North;
}

inline std::optional<FlexibleCell> checkedFlexibleOffset(FlexibleCell cell, FlexibleCell delta) noexcept {
    auto const x = static_cast<std::int64_t>(cell.x) + delta.x;
    auto const y = static_cast<std::int64_t>(cell.y) + delta.y;
    auto const z = static_cast<std::int64_t>(cell.z) + delta.z;
    if (x < -kFlexibleHorizontalLimit || x > kFlexibleHorizontalLimit || y < kFlexibleMinY
        || y > kFlexibleMaxY || z < -kFlexibleHorizontalLimit || z > kFlexibleHorizontalLimit)
        return std::nullopt;
    return FlexibleCell{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
}

inline float desiredFlexibleYaw(FlexibleHorizontal facing) noexcept {
    switch (facing) {
    case FlexibleHorizontal::South: return 0.0f;
    case FlexibleHorizontal::West: return 90.0f;
    case FlexibleHorizontal::North: return 180.0f;
    case FlexibleHorizontal::East: return -90.0f;
    default: return 0.0f;
    }
}

inline float desiredFlexiblePitch(FlexibleFace face) noexcept {
    return face == FlexibleFace::Up ? -90.0f : face == FlexibleFace::Down ? 90.0f : 0.0f;
}

inline constexpr FlexibleHorizontal flexibleRotationFacing(
    FlexibleRegion region, FlexibleHorizontal playerFacing) noexcept {
    switch (region) {
    case FlexibleRegion::Center: return static_cast<FlexibleHorizontal>(
        (static_cast<unsigned>(playerFacing) + 2u) % 4u);
    case FlexibleRegion::Left: return static_cast<FlexibleHorizontal>(
        (static_cast<unsigned>(playerFacing) + 3u) % 4u);
    case FlexibleRegion::Right: return static_cast<FlexibleHorizontal>(
        (static_cast<unsigned>(playerFacing) + 1u) % 4u);
    case FlexibleRegion::Top:
    case FlexibleRegion::Bottom: return playerFacing;
    }
    return playerFacing;
}

inline FlexiblePlacementPlan planFlexiblePlacement(
    FlexibleCell support, FlexibleFace face, FlexibleUV uv, unsigned modes,
    FlexibleHorizontal playerFacing) noexcept {
    FlexiblePlacementPlan result{};
    result.originalSupport = support;
    result.originalFace = face;
    result.modes = static_cast<FlexibleMode>(modes & 0x7u);
    if (!validFlexibleCell(support) || !validFlexibleFace(face) || !validFlexibleUV(uv)) return result;
    result.valid = true;
    result.region = selectFlexibleRegion(uv);
    result.regionDirection = flexibleRegionDirection(face, result.region, playerFacing);
    auto target = checkedFlexibleOffset(support, flexibleNormal(face));
    if (!target) return {};
    if (hasFlexibleMode(modes, FlexibleAdjacent) && result.region != FlexibleRegion::Center) {
        target = checkedFlexibleOffset(*target, flexibleOpposite(flexibleNormal(result.regionDirection)));
        if (!target) return {};
        target = checkedFlexibleOffset(*target, flexibleOpposite(flexibleNormal(face)));
        if (!target) return {};
    }
    if (hasFlexibleMode(modes, FlexibleOffset)) {
        target = checkedFlexibleOffset(*target, flexibleOpposite(flexibleNormal(result.regionDirection)));
        if (!target) return {};
    }
    result.destination = *target;
    result.placementFace = hasFlexibleMode(modes, FlexibleRotation) ? result.regionDirection : face;
    auto const useSupport = checkedFlexibleOffset(result.destination, flexibleOpposite(flexibleNormal(result.placementFace)));
    if (!useSupport) return {};
    result.useSupport = *useSupport;
    result.desiredYaw = desiredFlexibleYaw(flexibleRotationFacing(result.region, playerFacing));
    result.desiredPitch = desiredFlexiblePitch(result.placementFace);
    return result;
}

// Unlike rotation, offset and adjacent placement do not require a particular
// clicked face. Keep Tweakeroo's target cell, but use an actual supporting
// neighbour when possible. In particular, Adjacent often lands next to the
// originally clicked block; blindly retaining the old clicked face would
// address air and reject otherwise valid slabs/stairs/attached blocks.
// The callback only queries occupancy; placement still goes through native
// server-authoritative transactions and all normal validation.
template <typename IsSolid>
inline FlexiblePlacementPlan resolveFlexiblePositionSupport(
    FlexiblePlacementPlan plan, IsSolid&& isSolid) {
    if (!plan.valid || hasFlexibleMode(plan.modes, FlexibleRotation)
        || (!hasFlexibleMode(plan.modes, FlexibleOffset)
            && !hasFlexibleMode(plan.modes, FlexibleAdjacent)))
        return plan;
    if (isSolid(plan.useSupport)) return plan;

    constexpr std::array<FlexibleFace, 6> faces{
        FlexibleFace::Up, FlexibleFace::Down, FlexibleFace::North,
        FlexibleFace::South, FlexibleFace::West, FlexibleFace::East,
    };
    // Prefer the clicked block if it is physically adjacent to the target.
    // This preserves support in the common Adjacent-only case.
    for (auto const face : faces) {
        auto const candidate = checkedFlexibleOffset(
            plan.destination, flexibleOpposite(flexibleNormal(face)));
        if (candidate && *candidate == plan.originalSupport && isSolid(*candidate)) {
            plan.useSupport = *candidate;
            plan.placementFace = face;
            return plan;
        }
    }
    // Otherwise choose a real neighbour instead of a fictitious air support.
    for (auto const face : faces) {
        auto const candidate = checkedFlexibleOffset(
            plan.destination, flexibleOpposite(flexibleNormal(face)));
        if (candidate && isSolid(*candidate)) {
            plan.useSupport = *candidate;
            plan.placementFace = face;
            return plan;
        }
    }
    // No neighbour: retain the air-target fallback for full opaque blocks.
    return plan;
}

enum class FlexibleAdmissionReason : std::uint8_t {
    Accepted, InvalidPlan, DestinationOccupied, MissingSupport, OutOfReach,
    NotBlockItem, InteractiveTarget, CompetingPlacement, PlacementAbiUnavailable,
    NativeUnsupported, RepeatSuppressed,
};

struct FlexibleAdmission {
    bool destinationEmpty{};
    bool supportPresent{};
    bool withinReach{};
    bool blockItem{};
    bool interactiveTarget{};
    bool competingPlacement{};
};

inline FlexibleAdmissionReason admitFlexiblePlacement(
    FlexiblePlacementPlan const& plan, FlexibleAdmission const& admission) noexcept {
    if (!plan.valid) return FlexibleAdmissionReason::InvalidPlan;
    if (admission.competingPlacement) return FlexibleAdmissionReason::CompetingPlacement;
    if (admission.interactiveTarget) return FlexibleAdmissionReason::InteractiveTarget;
    if (!admission.blockItem) return FlexibleAdmissionReason::NotBlockItem;
    if (!admission.destinationEmpty) return FlexibleAdmissionReason::DestinationOccupied;
    if (!admission.supportPresent) return FlexibleAdmissionReason::MissingSupport;
    if (!admission.withinReach) return FlexibleAdmissionReason::OutOfReach;
    return FlexibleAdmissionReason::Accepted;
}

// A held block item represents an explicit transformed-placement request.  A
// blocked transform must consume that attempt; otherwise calling origin()
// would place the user's block on the cell they actually clicked.
inline bool flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason reason) noexcept {
    return reason != FlexibleAdmissionReason::Accepted
        && reason != FlexibleAdmissionReason::NotBlockItem
        && reason != FlexibleAdmissionReason::InteractiveTarget;
}

struct FlexibleInputLatch {
    bool enabled{};
    bool gameplay{};
    bool focused{};
    bool guiCaptured{};
    bool rotationHeld{};
    bool offsetHeld{};
    bool adjacentHeld{};
    bool active{};

    void update(bool nextEnabled, bool nextGameplay, bool nextFocused, bool nextGuiCaptured,
        bool nextRotation, bool nextOffset, bool nextAdjacent) noexcept {
        enabled = nextEnabled;
        gameplay = nextGameplay;
        focused = nextFocused;
        guiCaptured = nextGuiCaptured;
        auto const admitted = enabled && gameplay && focused && !guiCaptured;
        rotationHeld = admitted && nextRotation;
        offsetHeld = admitted && nextOffset;
        adjacentHeld = admitted && nextAdjacent;
        active = rotationHeld || offsetHeld || adjacentHeld;
    }
    void clear() noexcept {
        enabled = gameplay = focused = guiCaptured = false;
        rotationHeld = offsetHeld = adjacentHeld = active = false;
    }
};

inline bool flexibleModifiersAdmitted(bool modifiersHeld, bool lholoEasyPlace,
    bool inventoryBusy, bool restockBusy) noexcept {
    return modifiersHeld && !lholoEasyPlace && !inventoryBusy && !restockBusy;
}

enum class FlexibleUseStage : std::uint8_t { Idle, RotationPrepared, TransactionSent };
enum class FlexibleUseEvent : std::uint8_t { PrepareRotation, SendTransaction, RestoreRotation, Abort };

inline FlexibleUseStage advanceFlexibleUse(FlexibleUseStage stage, FlexibleUseEvent event) noexcept {
    if (event == FlexibleUseEvent::Abort) return FlexibleUseStage::Idle;
    if (event == FlexibleUseEvent::PrepareRotation && stage == FlexibleUseStage::Idle)
        return FlexibleUseStage::RotationPrepared;
    if (event == FlexibleUseEvent::SendTransaction
        && (stage == FlexibleUseStage::Idle || stage == FlexibleUseStage::RotationPrepared))
        return FlexibleUseStage::TransactionSent;
    if (event == FlexibleUseEvent::RestoreRotation && stage == FlexibleUseStage::TransactionSent)
        return FlexibleUseStage::Idle;
    return stage;
}

inline std::string_view flexibleAdmissionMessage(FlexibleAdmissionReason reason) noexcept {
    switch (reason) {
    case FlexibleAdmissionReason::Accepted: return "ok";
    case FlexibleAdmissionReason::InvalidPlan: return "対象面または座標が無効です";
    case FlexibleAdmissionReason::DestinationOccupied: return "移動先が空いていません";
    case FlexibleAdmissionReason::MissingSupport: return "移動先を支える面がありません";
    case FlexibleAdmissionReason::OutOfReach: return "移動先が届く距離にありません";
    case FlexibleAdmissionReason::NotBlockItem: return "ブロックアイテムを持っていません";
    case FlexibleAdmissionReason::InteractiveTarget: return "操作対象ブロックは変形しません";
    case FlexibleAdmissionReason::CompetingPlacement: return "他の配置機能が操作中です";
    case FlexibleAdmissionReason::PlacementAbiUnavailable: return "LHolo配置ABIがありません。対応版LHoloを使用してください";
    case FlexibleAdmissionReason::NativeUnsupported: return "この回転配置は対応するnative配送がありません";
    case FlexibleAdmissionReason::RepeatSuppressed: return "modifier継続入力の重複配置を抑止しました";
    }
    return "配置できません";
}

} // namespace praxis::companion
