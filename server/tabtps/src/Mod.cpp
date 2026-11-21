#include "core/TabTpsServerSampler.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/network/packet/Packet.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/server/ServerLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <Windows.h>
#include <bcrypt.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace praxis::server::tabtps {
namespace {
namespace wire = praxis::companion::tabtps;

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
        return 0;
    return epoch;
}

void recordCompletedSimulationTick(ServerLevel& level, std::uint64_t durationUs) noexcept;

// This is the same verified real level tick site used by LL's
// ServerLevelTickEvent emitter, except we measure AFTER origin() completes.
// The measured MSPT covers ServerLevel::_subTick execution (the simulation),
// not idle gaps or unrelated outer server loop/network processing.
LL_TYPE_INSTANCE_HOOK(SimulationTickTimingHook, ll::memory::HookPriority::Lowest,
    ServerLevel, &ServerLevel::$_subTick, void) {
    auto const began = std::chrono::steady_clock::now();
    origin();
    auto const duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - began).count();
    if (duration > 0)
        recordCompletedSimulationTick(*this, static_cast<std::uint64_t>(duration));
    else
        recordCompletedSimulationTick(*this, 0);
}
} // namespace

class TabTpsServerMod final {
    std::shared_ptr<ll::mod::NativeMod> self_;
    std::optional<ll::memory::HookRegistrar<SimulationTickTimingHook>> tickHook_;
    std::mutex samplerMutex_;
    wire::ServerSampler sampler_;
    std::atomic_bool enabled_{};
    std::atomic<std::uint32_t> observedWindows_{};
    std::atomic<std::uint64_t> completedTicks_{};
    std::uint32_t epoch_{};

    void tick(ServerLevel& level, std::uint64_t durationUs) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        auto const boundedDuration = durationUs > 0 && durationUs <= 5000000
            ? std::optional<std::uint32_t>{static_cast<std::uint32_t>(durationUs)}
            : std::nullopt;
        std::optional<wire::Sample> value;
        {
            std::lock_guard guard(samplerMutex_);
            if (!enabled_.load(std::memory_order_relaxed)) return;
            auto const now = GetTickCount64();
            sampler_.completedTick(now, boundedDuration);
            completedTicks_.fetch_add(1, std::memory_order_relaxed);
            value = sampler_.poll(now);
        }
        if (!value || !enabled_.load(std::memory_order_acquire)) return;

        auto encoded = wire::encode(*value);
        if (encoded.empty() || encoded.size() > 128) return;
        SamplePacket packet;
        packet.payload = std::move(encoded);
        unsigned eligibleClients{};
        level.forEachPlayer([&](Player& player) -> bool {
            if (enabled_.load(std::memory_order_relaxed)
                && SamplePacket::canSendTo(player)) {
                ++eligibleClients;
                packet.sendTo(player);
            }
            return true;
        });
        auto const windows = observedWindows_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (self_ && (windows == 1 || windows % 30 == 0)) {
            auto const measured = wire::mspt(*value);
            self_->getLogger().info(
                "TabTPS window: ticks={}, windowMs={}, TPS={:.2f}, "
                "simulationMSPT={:.3f}ms, measured={}, eligibleClients={}, totalTicks={}",
                value->ticks, value->windowMs, wire::tps(*value),
                measured.value_or(0.0), measured.has_value(),
                eligibleClients, completedTicks_.load(std::memory_order_relaxed));
        }
    }

public:
    static TabTpsServerMod& instance() {
        static TabTpsServerMod value;
        return value;
    }
    void completedTick(ServerLevel& level, std::uint64_t durationUs) noexcept {
        try { tick(level, durationUs); }
        catch (...) { enabled_.store(false, std::memory_order_release); }
    }
    bool load() {
        self_ = ll::mod::NativeMod::current();
        return bool(self_);
    }
    bool enable() {
        if (enabled_.load(std::memory_order_acquire)) return true;
        observedWindows_.store(0, std::memory_order_relaxed);
        completedTicks_.store(0, std::memory_order_relaxed);
        epoch_ = randomEpoch();
        if (!epoch_) {
            if (self_) self_->getLogger().error("TabTPS: random epoch generation failed");
            return false;
        }
        {
            std::lock_guard guard(samplerMutex_);
            sampler_.start(epoch_, GetTickCount64());
        }
        try { tickHook_.emplace(); }
        catch (...) { tickHook_.reset(); }
        if (!tickHook_) return false;
        enabled_.store(true, std::memory_order_release);
        if (self_) self_->getLogger().info(
            "TabTPS enabled: TPS counted on completed simulation ticks; MSPT "
            "measured across ServerLevel::_subTick, excluding idle wait.");
        return true;
    }
    bool disable() {
        enabled_.store(false, std::memory_order_release);
        tickHook_.reset();
        return true;
    }
    bool unload() {
        if (!disable()) return false;
        self_.reset();
        return true;
    }
};

namespace {
void recordCompletedSimulationTick(ServerLevel& level, std::uint64_t durationUs) noexcept {
    TabTpsServerMod::instance().completedTick(level, durationUs);
}
}
} // namespace praxis::server::tabtps

LL_REGISTER_MOD(praxis::server::tabtps::TabTpsServerMod,
                praxis::server::tabtps::TabTpsServerMod::instance());
