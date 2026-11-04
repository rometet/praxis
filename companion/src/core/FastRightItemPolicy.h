#pragma once
#include <string_view>

namespace praxis::companion {
// Bedrock 1.26.51 client-only XP-bottle fast-use experiments did not
// produce measurable additional throws. Suppress only the proven no-op
// extra calls; vanilla right-click still owns the physical input.
// Keep all other item types on the existing, separately-tested path.
inline constexpr bool fastRightExtraUseSupported(std::string_view id) noexcept {
    return id != "minecraft:experience_bottle";
}
} // namespace praxis::companion
