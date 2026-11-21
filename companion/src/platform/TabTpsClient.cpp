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
source::Receiver receiver;
std::uint64_t activeGeneration{};

struct SamplePacketV2 final : ll::network::PacketBase<SamplePacketV2> {
    static constexpr std::string_view CustomPacketId{source::packetId};
    static constexpr uint PacketVersion{source::packetVersion};
    static constexpr ll::network::PacketSide Side{ll::network::PacketSide::Client};
    static constexpr bool Optional{true};
    std::string wire;

    void write(BinaryStream& stream) const override {
        stream.writeString(wire, "tabtps_telemetry", nullptr);
    }
    Bedrock::Result<void> read(ReadOnlyBinaryStream& stream) override {
        auto decoded=stream.getString(256);
        if(!decoded)return nonstd::make_unexpected(decoded.error());
        wire=std::move(decoded.value());
        return {};
    }
};

struct SamplePacketHandlerV2 final
    : ll::network::PacketHandlerBase<SamplePacketHandlerV2,SamplePacketV2> {
    void handlePacket(NetworkIdentifier const&,NetEventCallback&,
        ll::network::PacketSide side,SamplePacketV2 const& packet)const {
        if(side!=ll::network::PacketSide::Client
            ||!runtimeReady.load(std::memory_order_acquire))return;
        auto sample=source::decode(packet.wire);
        if(!sample)return;
        auto client=ll::service::getClientInstance();
        if(!client||client->getClientInstanceState()!=ClientInstanceState::Playing
            ||!client->getLocalPlayer())return;
        auto const generation=worldGeneration.load(std::memory_order_acquire);
        if(!generation)return;
        std::lock_guard lock(telemetryMutex);
        if(activeGeneration!=generation){
            activeGeneration=generation;
            receiver.reset(generation);
        }
        receiver.accept(*sample,generation,GetTickCount64());
    }
};
// Levilamina's StaticRegistrationAnchor binds this actual type with the
// optional client packet. Old 1s TPS v1 packets are intentionally not accepted.
[[maybe_unused]] SamplePacketHandlerV2 packetHandler;
} // namespace

std::optional<source::Snapshot> telemetryView(std::uint64_t nowMs) noexcept {
    const auto generation=worldGeneration.load(std::memory_order_acquire);
    if(!generation || !runtimeReady.load(std::memory_order_acquire))return {};
    std::lock_guard lock(telemetryMutex);
    if(generation!=activeGeneration)return {};
    return receiver.fresh(generation,nowMs);
}
LiveView liveView(std::uint64_t nowMs) noexcept {
    LiveView result;
    auto s=telemetryView(nowMs);
    if(!s)return result;
    result.status=Status::Fresh;
    if(s->tpsCenti>=0)result.currentTps=s->tpsCenti/100.0;
    if(s->msptUs>=0)result.currentMspt=s->msptUs/1000.0;
    return result;
}
void resetLiveView() noexcept {
    std::lock_guard lock(telemetryMutex);
    activeGeneration=worldGeneration.load(std::memory_order_acquire);
    receiver.reset(activeGeneration);
}
} // namespace praxis::companion::tabtps
