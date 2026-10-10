#pragma once
#include <cmath>
#include <cstdint>
namespace praxis::companion {
// Data-only roundtrip for the separately owned Hitbox slot. No HUD backend
// or fabricated module is registered by inventory candidates.
struct HitboxOptions {
 bool enabled{},transparent{},showUnclassified{true};
 float range{64}; int maxActors{96}; std::uint32_t color{0xFFFFFFFF};
 bool operator==(HitboxOptions const&) const=default;
};
inline bool validHitboxOptions(HitboxOptions const& options) noexcept {
 return std::isfinite(options.range) && options.range>=8 && options.range<=192
   && options.maxActors>=8 && options.maxActors<=256;
}
}
