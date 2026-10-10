#pragma once
#include <cstdint>
#include <limits>
namespace praxis::companion {
inline constexpr unsigned featureRegistryBits=std::numeric_limits<std::uint32_t>::digits;
static_assert(featureRegistryBits==32&&std::numeric_limits<unsigned>::digits==32);
// Count 32 is valid: avoid shifting a 32-bit integer by 32.
inline constexpr std::uint32_t featureRegistryMask(unsigned count) noexcept {
    return count>=featureRegistryBits?(std::numeric_limits<std::uint32_t>::max)():
        (std::uint32_t{1}<<count)-1;
}
inline constexpr std::uint32_t featureRegistryBit(unsigned slot) noexcept {
    return slot<featureRegistryBits?std::uint32_t{1}<<slot:0;
}
}
