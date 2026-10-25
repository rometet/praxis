#include "core/FlexiblePlacementPolicy.h"
#include "core/RuntimePolicy.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace praxis::companion;
static int checks{};
static void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        FlexibleCell const support{-12345, -64, 9876};
        std::array<FlexibleUV, 5> const regions{{
            {0.50f, 0.50f}, {0.10f, 0.50f}, {0.90f, 0.50f}, {0.50f, 0.90f}, {0.50f, 0.10f}
        }};
        for (unsigned face = 0; face < 6; ++face) {
            auto const typedFace = static_cast<FlexibleFace>(face);
            for (auto uv : regions) {
                auto const relative = flexibleRelativeHit(typedFace, uv);
                auto const relativeRoundTrip = flexibleUVFromRelativeHit(relative, typedFace);
                require(relativeRoundTrip && std::abs(relativeRoundTrip->u - uv.u) < .0001f
                    && std::abs(relativeRoundTrip->v - uv.v) < .0001f,
                    "native relative hit round-trips on every face");
                auto const absolute = flexibleWorldHit(support, typedFace, uv);
                auto const absoluteRoundTrip = flexibleUVFromAbsoluteHit(support, absolute, typedFace);
                require(absoluteRoundTrip && std::abs(absoluteRoundTrip->u - uv.u) < .01f
                    && std::abs(absoluteRoundTrip->v - uv.v) < .01f,
                    "absolute HitResult point round-trips on every face");
                auto plan = planFlexiblePlacement(support, typedFace, uv,
                    FlexibleRotation | FlexibleOffset | FlexibleAdjacent, FlexibleHorizontal::East);
                require(plan.valid, "all six faces and five regions plan");
                require(validFlexibleCell(plan.destination), "transformed destination stays bounded");
                require(validFlexibleCell(plan.useSupport), "transformed support stays bounded");
                require(plan.region == selectFlexibleRegion(uv), "region selection is stable");
            }
        }
        require(selectFlexibleRegion({0.25f, 0.25f}) == FlexibleRegion::Center, "lower ties center");
        require(selectFlexibleRegion({0.75f, 0.75f}) == FlexibleRegion::Center,
            "center boundary remains center");
        require(selectFlexibleRegion({0.01f, 0.99f}) == FlexibleRegion::Top,
            "diagonal corner tie follows vertical priority");
        require(selectFlexibleRegion({0.99f, 0.01f}) == FlexibleRegion::Bottom,
            "opposite diagonal tie follows vertical priority");
        require(selectFlexibleRegion({0.01f, 0.70f}) == FlexibleRegion::Left,
            "corner triangle follows nearest horizontal edge");
        auto const leftPolygon = flexibleRegionPolygon(FlexibleRegion::Left);
        auto const topPolygon = flexibleRegionPolygon(FlexibleRegion::Top);
        require(leftPolygon[1] == FlexibleUV{.25f, .25f}
            && leftPolygon[2] == FlexibleUV{.25f, .75f},
            "left overlay uses diagonal corner boundaries");
        require(topPolygon[1] == FlexibleUV{.25f, .75f}
            && topPolygon[2] == FlexibleUV{.75f, .75f},
            "top overlay uses the center square and diagonals");

        std::array<FlexibleHorizontal, 4> const facings{
            FlexibleHorizontal::North, FlexibleHorizontal::East,
            FlexibleHorizontal::South, FlexibleHorizontal::West};
        for (unsigned face = 0; face < 6; ++face) {
            auto const typedFace = static_cast<FlexibleFace>(face);
            auto const raw = flexibleRelativeHit(typedFace, {.10f, .20f});
            for (auto facing : facings) {
                auto const oriented = flexibleUVFromRelativeHitForFacing(raw, typedFace, facing);
                require(oriented && validFlexibleUV(*oriented),
                    "face/facing oriented UV remains valid");
                auto const roundTrip = flexibleRawUVFromOriented(*oriented, typedFace, facing);
                require(std::abs(roundTrip.u - .10f) < .0001f
                    && std::abs(roundTrip.v - .20f) < .0001f,
                    "face/facing UV inverse round-trips");
            }
        }
        auto const upNorth = flexibleUVFromRelativeHitForFacing(
            flexibleRelativeHit(FlexibleFace::Up, {.10f, .20f}), FlexibleFace::Up,
            FlexibleHorizontal::North);
        require(upNorth && std::abs(upNorth->u - .10f) < .0001f
            && std::abs(upNorth->v - .80f) < .0001f,
            "up face north-facing UV transform");
        auto const downWest = flexibleUVFromRelativeHitForFacing(
            flexibleRelativeHit(FlexibleFace::Down, {.10f, .20f}), FlexibleFace::Down,
            FlexibleHorizontal::West);
        require(downWest && std::abs(downWest->u - .80f) < .0001f
            && std::abs(downWest->v - .10f) < .0001f,
            "down face west-facing UV transform");
        auto const northSouth = flexibleUVFromRelativeHitForFacing(
            flexibleRelativeHit(FlexibleFace::North, {.10f, .20f}), FlexibleFace::North,
            FlexibleHorizontal::South);
        require(northSouth && std::abs(northSouth->u - .90f) < .0001f
            && std::abs(northSouth->v - .20f) < .0001f,
            "north face flips horizontal UV");
        auto const eastNorth = flexibleUVFromRelativeHitForFacing(
            flexibleRelativeHit(FlexibleFace::East, {.10f, .20f}), FlexibleFace::East,
            FlexibleHorizontal::North);
        require(eastNorth && std::abs(eastNorth->u - .90f) < .0001f
            && std::abs(eastNorth->v - .20f) < .0001f,
            "east face flips horizontal UV");
        for (unsigned face = 0; face < 6; ++face) {
            for (auto facing : facings) {
                for (auto uv : regions) {
                    auto const plan = planFlexiblePlacement(
                        support, static_cast<FlexibleFace>(face), uv,
                        FlexibleRotation | FlexibleOffset | FlexibleAdjacent, facing);
                    require(plan.valid, "six faces x four facings x five regions plan");
                    require(plan.desiredYaw == desiredFlexibleYaw(
                        flexibleRotationFacing(plan.region, facing)),
                        "rotation yaw follows region/player facing independently of click face");
                }
            }
        }
        require(!validFlexibleUV({std::numeric_limits<float>::quiet_NaN(), .5f}), "NaN rejected");
        require(!validFlexibleUV({.5f, std::numeric_limits<float>::infinity()}), "infinity rejected");
        require(!planFlexiblePlacement({kFlexibleHorizontalLimit, 0, 0}, FlexibleFace::East, {.5f, .5f}, 0,
            FlexibleHorizontal::North).valid, "out of range transform rejected");
        require(horizontalFacingFromYaw(0.0f) == FlexibleHorizontal::South, "yaw south");
        require(horizontalFacingFromYaw(90.0f) == FlexibleHorizontal::West, "yaw west");
        require(horizontalFacingFromYaw(180.0f) == FlexibleHorizontal::North, "yaw north");
        require(horizontalFacingFromYaw(270.0f) == FlexibleHorizontal::East, "yaw east");

        // Tweakeroo positioning modes are independent: Adjacent returns a
        // side neighbour on the clicked plane, Offset returns the diagonal,
        // and their combination reaches a second step on the same axis.
        auto const baseCell = FlexibleCell{0, 64, 0};
        auto const adjacency = planFlexiblePlacement(baseCell, FlexibleFace::Up,
            {.1f, .5f}, FlexibleAdjacent, FlexibleHorizontal::South);
        auto const diagonal = planFlexiblePlacement(baseCell, FlexibleFace::Up,
            {.1f, .5f}, FlexibleOffset, FlexibleHorizontal::South);
        auto const combined = planFlexiblePlacement(baseCell, FlexibleFace::Up,
            {.1f, .5f}, FlexibleOffset | FlexibleAdjacent, FlexibleHorizontal::South);
        require(adjacency.valid && diagonal.valid && combined.valid,
            "offset and adjacent independently plan valid cells");
        require(adjacency.destination != diagonal.destination
            && adjacency.destination != combined.destination
            && diagonal.destination != combined.destination,
            "adjacent, offset and combination have three distinct targets");
        require(adjacency.destination.y == baseCell.y,
            "adjacent stays on clicked top-face plane");
        require(diagonal.destination.y == baseCell.y + 1,
            "offset stays one block above top-face support");
        auto const onlyOriginalSolid = [baseCell](FlexibleCell c) { return c == baseCell; };
        auto fixedAdjacent = resolveFlexiblePositionSupport(adjacency, onlyOriginalSolid);
        require(fixedAdjacent.destination == adjacency.destination,
            "support resolution never changes Tweakeroo's destination");
        require(fixedAdjacent.useSupport == baseCell,
            "adjacent finds original solid block as support");
        require(flexibleAdd(fixedAdjacent.useSupport,
                flexibleNormal(fixedAdjacent.placementFace)) == fixedAdjacent.destination,
            "adjacent native face points at its intended destination");
        auto noSolid = resolveFlexiblePositionSupport(adjacency,
            [](FlexibleCell) { return false; });
        require(noSolid.useSupport == adjacency.useSupport
            && noSolid.placementFace == adjacency.placementFace,
            "air fallback retains original plan");
        auto fixedRotated = resolveFlexiblePositionSupport(planFlexiblePlacement(
            baseCell, FlexibleFace::Up, {.1f, .5f}, FlexibleRotation | FlexibleAdjacent,
            FlexibleHorizontal::South), onlyOriginalSolid);
        require(fixedRotated.useSupport != fixedAdjacent.useSupport,
            "rotation face ownership is never silently rewritten");
        auto const centerAdjacent = planFlexiblePlacement(baseCell, FlexibleFace::Up,
            {.5f, .5f}, FlexibleAdjacent, FlexibleHorizontal::South);
        require(centerAdjacent.destination == FlexibleCell{0, 65, 0},
            "adjacent center retains vanilla target");

        auto noModes = planFlexiblePlacement({0, 0, 0}, FlexibleFace::Up, {.5f, .5f}, 0,
            FlexibleHorizontal::North);
        require(noModes.destination == FlexibleCell{0, 1, 0}, "plain destination");
        require(noModes.useSupport == FlexibleCell{0, 0, 0}, "plain support");
        auto blocked = FlexibleAdmission{false, true, true, true, false, false};
        require(admitFlexiblePlacement(noModes, blocked) == FlexibleAdmissionReason::DestinationOccupied,
            "occupied target rejected");
        blocked = {true, false, true, true, false, false};
        require(admitFlexiblePlacement(noModes, blocked) == FlexibleAdmissionReason::MissingSupport,
            "missing support rejected");
        blocked = {true, true, true, false, false, false};
        require(admitFlexiblePlacement(noModes, blocked) == FlexibleAdmissionReason::NotBlockItem,
            "non block item rejected");
        blocked = {true, true, true, true, false, true};
        require(admitFlexiblePlacement(noModes, blocked) == FlexibleAdmissionReason::CompetingPlacement,
            "competing placement rejected");
        require(flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::DestinationOccupied),
            "blocked transformed placement consumes the action safely");
        require(flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::OutOfReach),
            "out-of-reach transformed placement consumes the action safely");
        require(!flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::NotBlockItem),
            "non-block item remains a vanilla pass-through");
        require(!flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::InteractiveTarget),
            "interactive target remains a vanilla pass-through");
        require(flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::NativeUnsupported),
            "unsupported native transform is a safe failure");
        require(flexibleTransformFailureIsSafeFailure(FlexibleAdmissionReason::RepeatSuppressed),
            "held repeat transform is a safe failure");
        require(flexibleRotationFacing(FlexibleRegion::Center, FlexibleHorizontal::East)
            == FlexibleHorizontal::West, "center rotation uses opposite player yaw");
        require(flexibleRotationFacing(FlexibleRegion::Left, FlexibleHorizontal::East)
            == FlexibleHorizontal::North, "left rotation uses player yaw counter-clockwise");
        require(flexibleRotationFacing(FlexibleRegion::Right, FlexibleHorizontal::East)
            == FlexibleHorizontal::South, "right rotation uses player yaw clockwise");

        FlexibleInputLatch latch;
        latch.update(true, true, true, false, true, false, false);
        require(latch.active && latch.rotationHeld, "held activation admitted");
        latch.update(true, true, true, true, true, true, true);
        require(!latch.active && !latch.rotationHeld, "GUI capture clears latches");
        latch.update(true, false, true, false, true, true, true);
        require(!latch.active, "non-gameplay clears latches");
        latch.update(true, true, true, false, false, false, true);
        require(latch.active && latch.adjacentHeld, "adjacent hold remains independent");
        latch.clear();
        require(!latch.active, "lifecycle clear");

        require(flexibleModifiersAdmitted(true, false, false, false), "arbitration admits standalone");
        require(!flexibleModifiersAdmitted(true, true, false, false), "LHolo arbitration");
        require(!flexibleModifiersAdmitted(true, false, true, false), "inventory arbitration");
        require(advanceFlexibleUse(FlexibleUseStage::Idle, FlexibleUseEvent::PrepareRotation)
            == FlexibleUseStage::RotationPrepared, "rotation before transaction");
        require(advanceFlexibleUse(FlexibleUseStage::RotationPrepared, FlexibleUseEvent::SendTransaction)
            == FlexibleUseStage::TransactionSent, "transaction after rotation");
        require(advanceFlexibleUse(FlexibleUseStage::TransactionSent, FlexibleUseEvent::RestoreRotation)
            == FlexibleUseStage::Idle, "camera restore after transaction");
        require(advanceFlexibleUse(FlexibleUseStage::TransactionSent, FlexibleUseEvent::Abort)
            == FlexibleUseStage::Idle, "abort restores lifecycle");

        RuntimeConfig config;
        require(config.keybinds.size() == featureCount, "new feature binding appended");
        require(config.keybinds[static_cast<unsigned>(Feature::FlexibleBlockPlacement)] == 'P', "P default toggle");
        require(validRuntimeConfig(config), "new default config valid");
        config.flexibleBlockPlacement = true;
        auto published = runtimeSettings(config, kAllRenderPasses, featureRegistryBit(
            static_cast<unsigned>(Feature::FlexibleBlockPlacement)));
        require(published.on(Feature::FlexibleBlockPlacement), "new feature publication");
        require(published.flexiblePlacement.rotationKey == 'R', "rotation option publication");
        config.flexiblePlacementOptions.rotationKey = 0x79;
        require(!validRuntimeConfig(config), "reserved key rejected");

        std::cout << "Flexible placement checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
