#include "platform/TabTpsClient.h"
#include "core/Settings.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include <Windows.h>
#include <mutex>
#include <string>
#include <string_view>

namespace praxis::companion::tabtps {
namespace {
std::mutex telemetryMutex;
Receiver receiver;
std::uint64_t activeGeneration{};

struct SamplePacket final : ll::network::PacketBase<SamplePacket> {
    static constexpr std::string_view CustomPacketId{"praxis:tps_sample"};
    static constexpr uint PacketVersion{1};
    static constexpr ll::network::PacketSide Side{ll::network::PacketSide::Client};
    static constexpr bool Optional{true};
    std::string wire;

    void write(BinaryStream& stream) const override {
        stream.writeString(wire, "tps_sample", nullptr);
    }
    Bedrock::Result<void> read(ReadOnlyBinaryStream& stream) override {
        auto decoded = stream.getString(128);
        if (!decoded) return nonstd::make_unexpected(decoded.error());
        wire = std::move(decoded.value());
        return {};
    }
};

struct SamplePacketHandler final
    : ll::network::PacketHandlerBase<SamplePacketHandler, SamplePacket> {
    void handlePacket(NetworkIdentifier const&, NetEventCallback&,
                      ll::network::PacketSide side, SamplePacket const& packet) const {
        if (side != ll::network::PacketSide::Client
            || !runtimeReady.load(std::memory_order_acquire)) return;
        auto sample = decode(packet.wire);
        if (!sample) return;
        auto client = ll::service::getClientInstance();
        if (!client || client->getClientInstanceState() != ClientInstanceState::Playing
            || !client->getLocalPlayer()) return;
        auto const generation = worldGeneration.load(std::memory_order_acquire);
        if (!generation) return;
        std::lock_guard lock(telemetryMutex);
        if (activeGeneration != generation) {
            activeGeneration = generation;
            receiver.reset(generation);
        }
        receiver.accept(*sample, generation, GetTickCount64());
    }
};
// Packet/handler registration is tied to these concrete types by LL's
// StaticRegistrationAnchor. No inbound packet is required for vanilla peers.
[[maybe_unused]] SamplePacketHandler packetHandler;
} // namespace

LiveView liveView(std::uint64_t nowMs) noexcept {
    LiveView result;
    auto const generation = worldGeneration.load(std::memory_order_acquire);
    std::lock_guard lock(telemetryMutex);
    if (generation != activeGeneration) return result;
    result.status = receiver.status(generation, nowMs);
    if (auto value = receiver.freshSample(generation, nowMs)) {
        result.currentTps = tps(*value);
        result.currentMspt = mspt(*value);
    }
    return result;
}
void resetLiveView() noexcept {
    std::lock_guard lock(telemetryMutex);
    activeGeneration = worldGeneration.load(std::memory_order_acquire);
    receiver.reset(activeGeneration);
}
} // namespace praxis::companion::tabtps
