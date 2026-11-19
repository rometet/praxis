#include "core/TabTpsServerSampler.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/network/packet/Packet.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <Windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace praxis::server::tabtps {
namespace {
namespace wire = praxis::companion::tabtps;

// Must match the client packet's exact ID, version, optional flag, codec,
// and receiving side. The server never requires clients to install the mod.
struct SamplePacket final : ll::network::PacketBase<SamplePacket> {
    static constexpr std::string_view CustomPacketId{wire::customPacketId};
    static constexpr uint PacketVersion{wire::customPacketVersion};
    static constexpr ll::network::PacketSide Side{ll::network::PacketSide::Client};
    static constexpr bool Optional{true};

    std::string payload;

    void write(BinaryStream& out) const override {
        out.writeString(payload, "tps_sample", nullptr);
    }
    Bedrock::Result<void> read(ReadOnlyBinaryStream& in) override {
        auto decoded = in.getString(128);
        if (!decoded) return nonstd::make_unexpected(decoded.error());
        payload = std::move(decoded.value());
        return {};
    }
};

std::uint32_t randomEpoch() noexcept {
    std::uint32_t epoch{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&epoch),
                        sizeof(epoch), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return 0; // No random epoch => don't transmit potentially ambiguous samples.
    return epoch;
}
} // namespace

class TabTpsServerMod final {
    std::shared_ptr<ll::mod::NativeMod> self_;
    ll::event::ListenerPtr tickListener_;
    std::mutex samplerMutex_;
    wire::ServerSampler sampler_;
    std::atomic_bool enabled_{};
    std::atomic<std::uint32_t> observedWindows_{};
    std::uint32_t epoch_{};

    void tick(ll::event::ServerLevelTickEvent& event) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        std::optional<wire::Sample> value;
        auto const now = GetTickCount64();
        {
            std::lock_guard guard(samplerMutex_);
            if (!enabled_.load(std::memory_order_relaxed)) return;

            // ServerLevelTickEvent observes actual simulation ticks.
            // This is NOT a verified full-frame duration hook, so MSPT is
            // explicitly unavailable rather than estimated from TPS/intervals.
            sampler_.completedTick(now, std::nullopt);
            value = sampler_.poll(now);
        }
        if (!value || !enabled_.load(std::memory_order_acquire)) return;

        auto encoded = wire::encode(*value);
        if (encoded.empty() || encoded.size() > 128) return;
        SamplePacket packet;
        packet.payload = std::move(encoded);

        // Optional capability negotiation: vanilla clients and clients without
        // the exact registered packet are skipped. Never send speculative packets.
        unsigned eligibleClients{};
        event.level().forEachPlayer([&](Player& player) -> bool {
            if (enabled_.load(std::memory_order_relaxed)
                && SamplePacket::canSendTo(player)) {
                ++eligibleClients;
                packet.sendTo(player);
            }
            return true;
        });
        // Bounded heartbeat provides a server-only measurement smoke test
        // without requiring a player connection or logging player identities.
        auto const windows = observedWindows_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (self_ && (windows == 1 || windows % 30 == 0))
            self_->getLogger().info("TabTPS window: observed ticks={}, elapsedMs={}, TPS={:.2f}, eligibleClients={}",
                                    value->ticks, value->windowMs, wire::tps(*value), eligibleClients);
    }

public:
    static TabTpsServerMod& instance() {
        static TabTpsServerMod value;
        return value;
    }
    bool load() {
        self_ = ll::mod::NativeMod::current();
        return bool(self_);
    }
    bool enable() {
        if (enabled_.load(std::memory_order_acquire)) return true;
        observedWindows_.store(0, std::memory_order_relaxed);
        epoch_ = randomEpoch();
        if (!epoch_) {
            if (self_) self_->getLogger().error("TabTPS: random epoch generation failed");
            return false;
        }
        {
            std::lock_guard guard(samplerMutex_);
            sampler_.start(epoch_, GetTickCount64());
        }
        try {
            tickListener_ = ll::event::EventBus::getInstance()
                .emplaceListener<ll::event::ServerLevelTickEvent>(
                    [this](ll::event::ServerLevelTickEvent& event) {
                        try { tick(event); }
                        catch (...) { enabled_.store(false, std::memory_order_release); }
                    });
        } catch (...) { tickListener_.reset(); }
        if (!tickListener_) return false;
        enabled_.store(true, std::memory_order_release);
        if (self_) self_->getLogger().info(
            "TabTPS server enabled. Reports observed simulation TPS once/second. "
            "MSPT unavailable until a verified full-frame timer is implemented.");
        return true;
    }
    bool disable() {
        enabled_.store(false, std::memory_order_release);
        bool removed = true;
        if (tickListener_) {
            removed = ll::event::EventBus::getInstance().removeListener(tickListener_);
            if (removed) tickListener_.reset();
        }
        return removed;
    }
    bool unload() {
        if (!disable()) return false;
        self_.reset();
        return true;
    }
};
} // namespace praxis::server::tabtps

LL_REGISTER_MOD(praxis::server::tabtps::TabTpsServerMod,
                praxis::server::tabtps::TabTpsServerMod::instance());
