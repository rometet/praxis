#include "platform/LholoFlexiblePlacementAbi.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace praxis::companion::lholo_abi;

int main() {
    static_assert(std::is_standard_layout_v<FlexiblePlacementRequest>);
    static_assert(!std::is_pointer_v<decltype(FlexiblePlacementRequest::targetBlockId)>);
    FlexiblePlacementRequest request{};
    if (request.version != kVersion || sizeof(request) != 64)
        throw std::runtime_error("LHolo flexible placement ABI layout");
    if ((EasyPlaceOwnsPlacement | ManualOwnsPlacement | RangeOwnsPlacement) != 7u)
        throw std::runtime_error("LHolo ownership flags");
    static_assert(std::is_standard_layout_v<FlexiblePlacementRequestV2>);
    static_assert(std::is_standard_layout_v<FlexiblePlacementStatusV2>);
    if (sizeof(FlexiblePlacementRequestV2) != 104 || sizeof(FlexiblePlacementStatusV2) != 40
        || FlexiblePlacementRequestV2{}.version != kVersion2)
        throw std::runtime_error("LHolo flexible placement v2 ABI layout");
    request.supportX = -12345;
    request.supportY = -64;
    request.supportZ = 9876;
    request.relativeHitX = 1.0f;
    request.relativeHitY = .5f;
    request.relativeHitZ = 0.25f;
    if (request.supportX != -12345 || request.supportZ != 9876
        || request.relativeHitX != 1.0f || request.relativeHitY != .5f)
        throw std::runtime_error("POD request values");
    std::cout << "LHolo flexible placement ABI checks: 6 PASS\n";
}

