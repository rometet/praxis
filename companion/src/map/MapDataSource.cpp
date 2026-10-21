#include "map/MapDataSource.h"
#include "ll/api/service/TargetedBedrock.h"
#include "map/MapCore.h"
#include "gui/GuiBridge.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/file/PathBuffer.h"
#include "mc/network/GameConnectionInfo.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/LevelStorage.h"
#include <string_view>
#include <cstdio>

namespace praxis::companion::map {
Session captureSession(LocalPlayer &player, std::uint64_t generation) {
    auto &level = player.getLevel();
    std::optional<std::string> identity;
    auto client = ll::service::getClientInstance();
    if (client && client->getLocalPlayer() == &player && level.hasLevelStorage()) {
        auto connection = client->getGameConnectionInfo();
        if (connection && connection->mType == Social::ConnectionType::Local)
            identity =
                localWorldIdentity(level.getLevelStorage().getFullPath().value, level.getLevelId(), true);
    }
    return {identity ? std::move(*identity) : "session:" + std::to_string(generation), generation,
            int(player.getDimension().getDimensionId()), bool(identity)};
}
std::vector<RadarPoint> captureRadar(LocalPlayer &player, int radius, bool names, std::uint32_t &visited) {
    std::vector<RadarPoint> result;
    result.reserve(128);
    auto const p = player.getPosition();
    auto const dim = int(player.getDimension().getDimensionId());
    visited = 0;
    // Callback iteration avoids materializing/copying the complete actor list.
    // This first native adapter displays players. Other actor categories require
    // a proven bounded iterator, rather than fetchEntities()'s all-actor copy.
    player.getLevel().forEachPlayer(std::function<bool(Player &)>{[&](Player &other) {
        if (visited >= 512)
            return false;
        ++visited;
        if (&other == static_cast<Player *>(&player) || !other.isAlive() || other.isInvisible() ||
            int(other.getDimension().getDimensionId()) != dim)
            return true;
        auto q = other.getPosition();
        double dx = q.x - p.x, dz = q.z - p.z;
        if (coordinate(q.x) && coordinate(q.z) && std::isfinite(q.y) &&
            dx * dx + dz * dz <= double(radius) * radius && result.size() < 128) {
            std::string name;
            if (names) {
                auto const &borrowed = other.getNameTag();
                name.assign(borrowed.data(), std::min<std::size_t>(borrowed.size(), 63));
            }
            result.push_back({q.x, q.y, q.z, std::move(name)});
        }
        return result.size() < 128;
    }});
    return result;
}
} // namespace praxis::companion::map
