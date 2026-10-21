#include "runtime/SaturationProbe.h"
#include "mc/world/actor/player/Player.h"
#include <type_traits>
using namespace praxis::companion;
// Compile-only against the actual pinned client SDK. All field access and the
// static attribute/getter signature must compile; no Minecraft fixture is faked.
SaturationObservation probePinnedClientSaturation(Player const& player) {
    auto const& attribute=Player::SATURATION();
    auto ref=player.getAttribute(attribute);
    return readSaturationObservation(attribute,ref);
}
static_assert(std::is_same_v<decltype(probePinnedClientSaturation(std::declval<Player const&>())),SaturationObservation>);
