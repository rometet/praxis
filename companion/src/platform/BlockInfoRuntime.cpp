#include "platform/BlockInfoRuntime.h"
#include "core/BlockInfoPolicy.h"
#include "core/BlockRaycast.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/actor/Actor.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>

namespace praxis::companion {
namespace {
std::mutex mutex;
std::atomic_bool installed{};
std::atomic_bool loggedFirstLine{};
std::atomic<unsigned> shown{};

// Reach used for the crosshair probe. The vanilla pick reach is 5 blocks in creative
// survival; the panel follows what the player could actually touch.
constexpr double blockInfoReach = 5.0;

// The crosshair target is resolved with Insight's outline-accurate raycast rather than
// ClientInstance::getLatestHitResult(), because the latter reports a thin block (torch,
// fence, flower, button, slab) as its whole cell: aiming at the empty half of the cell
// still named the torch. See core/BlockRaycast.h for the rationale and provenance.
// No SDK reference is allowed to be stored by this module: currentLine() copies the eye and
// view vector, re-fetches the region from the live dimension, and returns an owned string.

// The chunks that can possibly be read are those within blockInfoReach of the eye, because
// every read below (the clip, the own-shape check, getChunkAt, getBlock) is confined to that
// sphere. `r` is in block units, so the reach is rounded up, and the cell under the eye is
// included too. Without this check a read that lands on a chunk the client does not have (a
// dimension edge, the first frames after a teleport, a chunk the server has not sent) makes
// BlockSource index a chunk folder that is not there: the classic 0xC0000005 nobody can
// catch with try/catch, because it is not an exception but a bad dereference.
bool probeVolumeLoaded(BlockSource const& region, Vec3 const& eye) noexcept {
    int const radius = static_cast<int>(blockInfoReach) + 1;
    // One tick in each axis: a partial block at the edge of the reach must be inside the
    // loaded volume, not one cell outside it.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            BlockPos const cell{
                static_cast<int>(std::floor(eye.x)) + dx,
                static_cast<int>(std::floor(eye.y)),
                static_cast<int>(std::floor(eye.z)) + dz,
            };
            if (!region.hasChunksAt(cell, radius, /*ignoreClientChunk=*/false)) return false;
        }
    }
    return true;
}

// Never retain a BlockSource across frames. `Dimension::getBlockSourceFromMainChunkSource()`
// hands back the dimension's OWN mutable source, and the dimension destroys/replaces it
// when the level goes away (menu, disconnect, dimension change) or a remote chunk swap
// restacks its chunk source. The HUD draws once per frame, so every probe re-fetches the
// region from the live player and uses it only for the duration of this call; everything
// that outlives the call (eye, view vector, the formatted line) is an owned copy.
// Verified locally 2026-11-24: the same probe sequence with a retained source reads a
// retired source on the next frame and faults with 0xC0000005; re-fetching returns "".
std::string currentLine() {
    auto       client = ll::service::getClientInstance();
    if (!client) return {};
    LocalPlayer* player = client->getLocalPlayer();
    if (!player) return {};
    // Only a player that is actually in a level has a dimension with a main chunk source;
    // asking a connecting/leaving player for one is what made the HUD unsafe on transition
    // frames.
    if (!player->isAlive()) return {};

    Vec3 const eye  = player->getEyePos();
    Vec3 const look = player->getViewVector(1.0f);
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)
        || !std::isfinite(look.x) || !std::isfinite(look.y) || !std::isfinite(look.z)) {
        return {};
    }

    // Fetched here, not earlier: the reference below is only valid while `player` and its
    // dimension are. Nothing stores it.
    BlockSource& region = player->getDimension().getBlockSourceFromMainChunkSource();
    if (!probeVolumeLoaded(region, eye)) return {};

    auto const hit = raycastToBlock(region, eye, look, blockInfoReach, /*passLiquids=*/false);
    if (!hit) return {};

    // The hit cell must belong to a chunk this source currently has. The raycast can name a
    // cell at the reach edge that was unloaded between the clip and this read, and an
    // unguarded getBlock() indexes a null chunk (the classic border crash).
    if (!region.getChunkAt(hit->pos)) return {};
    Block const& block = region.getBlock(hit->pos);
    if (block.isAir()) return {};

    BlockInfoInput in;
    in.name   = hit->typeName;
    in.liquid = isLiquidBlock(block);
    in.air    = false;
    if (in.name.empty()) return {};

    BlockInfoOptions options;
    if (!blockInfoVisible(in, options)) return {};
    // Distance is the one extra Insight shows that the policy line does not carry; it is
    // appended inside the policy so the 96-character cap is still enforced.
    return blockInfoLineWithDistance(in, options, hit->distance, true);
}
} // namespace

bool installBlockInfoRuntime() {
    if (installed.exchange(true)) return true;
    logGuiInfo("BLOCKINFO_RUNTIME_READY raycast=Insight-outline-line-of-sight reach=5.0");
    return true;
}

// Called once per frame by the GUI host; returns the line to draw (empty = nothing).
std::string blockInfoLineForHud() {
    if (!installed.load(std::memory_order_acquire)) return {};
    std::lock_guard lock(mutex);
    std::string line;
    try {
        line = currentLine();
    } catch (...) {
        return {};
    }
    if (!line.empty()) {
        ++shown;
        // One-shot evidence line: proves the crosshair -> BlockSource -> policy path
        // actually ran in a live client. The HUD itself is ImGui, which no external
        // harness can tree-read, so the log is the verification surface.
        if (!loggedFirstLine.exchange(true)) {
            std::string message = "BLOCKINFO_FIRST_LINE ";
            message += line;
            logGuiInfo(message.c_str());
        }
    }
    return line;
}

bool blockInfoRuntimeReady() noexcept {
    if (!installed.load(std::memory_order_acquire)) return false;
    try {
        auto client = ll::service::getClientInstance();
        return client && client->getLocalPlayer();
    } catch (...) {
        return false;
    }
}

std::string blockInfoStatus() {
    char text[160]{};
    std::snprintf(text, sizeof(text), "BlockInfo %s shown=%u",
                  installed.load() ? "ready" : "off", shown.load());
    return text;
}
} // namespace praxis::companion
