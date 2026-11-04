#pragma once
#include <array>
#include <cstdint>
namespace praxis::companion {
// Exact offline PE and clang-cl SDK ABI evidence is under
// weather-sunny-isolation-evidence. Runtime binding is required before installation.
struct SunnyNativeFunction { std::uintptr_t rva; unsigned size; std::uint64_t fingerprint; };
inline constexpr std::array<SunnyNativeFunction,4> sunnyNativeFunctions{{
    {0x6235bc0, 855, 0x894a357e68e1785dULL}, // Color
    {0x62355d0, 912, 0x1862b7ec496e589aULL}, // Cloud
    {0x6236250, 525, 0xde7c845d2dfa3672ULL}, // Fog
    {0x230e620, 407, 0x7cf66e954d6043e6ULL}, // SkyBrightness: runtime bind only
}};
inline constexpr std::uintptr_t sunnyCloudLookupRva=0x11d6973c; // native instruction 62356bc
inline constexpr unsigned sunnyImageSize = 314576896;
inline constexpr unsigned sunnyImageTimestamp = 1789166333;
inline constexpr std::array<unsigned char,16> sunnyCodeViewGuid{0xa9,0xb4,0x53,0x03,0xaa,0x3e,0xe2,0xe2,0x4c,0x4c,0x44,0x20,0x50,0x44,0x42,0x2e};
}
