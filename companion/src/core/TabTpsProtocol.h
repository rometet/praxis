#pragma once
// Praxis TabTPS v1: transport-independent wire format and connection state.
// No client hooks, LL packet registration, or BDS runtime dependency here.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace praxis::companion::tabtps {
inline constexpr std::string_view wirePrefix{"PTPS1"};
inline constexpr std::string_view customPacketId{"praxis:tps_sample"};
inline constexpr unsigned customPacketVersion = 1;
inline constexpr std::uint64_t freshnessMs = 2500;

struct Sample {
    std::uint32_t epoch{};          // random nonzero identifier per server start
    std::uint32_t sequence{};       // increment once per measurement
    std::uint32_t ticks{};          // observed server simulation ticks
    std::uint32_t windowMs{};       // elapsed steady-clock measurement window
    std::uint32_t avgFrameUs{};     // mean measured simulation tick work (ServerLevel::_subTick) in microseconds; 0 means unavailable
    bool operator==(Sample const&) const = default;
};

inline bool valid(Sample const& s) noexcept {
    if (!s.epoch || !s.sequence || !s.ticks || s.ticks > 1200
        || s.windowMs < 250 || s.windowMs > 60000
        || s.avgFrameUs > 5000000) return false;
    auto const rawTps = 1000.0 * s.ticks / s.windowMs;
    return rawTps > 0 && rawTps <= 120.0;
}
inline double tps(Sample const& s) noexcept {
    return valid(s) ? std::min(20.0, 1000.0 * s.ticks / s.windowMs) : 0.0;
}
inline std::optional<double> mspt(Sample const& s) noexcept {
    if (!valid(s) || !s.avgFrameUs) return std::nullopt;
    return static_cast<double>(s.avgFrameUs) / 1000.0;
}
inline std::string encode(Sample const& s) {
    if (!valid(s)) return {};
    return std::string(wirePrefix) + "|" + std::to_string(s.epoch) + "|" +
        std::to_string(s.sequence) + "|" + std::to_string(s.ticks) + "|" +
        std::to_string(s.windowMs) + "|" + std::to_string(s.avgFrameUs);
}
inline bool parseUint(std::string_view field, std::uint32_t& value) noexcept {
    if (field.empty() || field.size() > 10) return false;
    for (char c : field) if (c < '0' || c > '9') return false;
    auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
    return ec == std::errc{} && end == field.data() + field.size();
}
inline std::optional<Sample> decode(std::string_view wire) noexcept {
    if (wire.size() > 128 || wire.substr(0, wirePrefix.size()) != wirePrefix
        || wire.size() <= wirePrefix.size() || wire[wirePrefix.size()] != '|') return {};
    std::uint32_t fields[5]{};
    std::size_t pos = wirePrefix.size() + 1;
    for (int i = 0; i < 5; ++i) {
        auto const end = wire.find('|', pos);
        if ((i == 4 && end != std::string_view::npos)
            || (i < 4 && end == std::string_view::npos)) return {};
        auto const next = end == std::string_view::npos ? wire.size() : end;
        if (!parseUint(wire.substr(pos, next - pos), fields[i])) return {};
        pos = next + 1;
    }
    Sample s{fields[0],fields[1],fields[2],fields[3],fields[4]};
    return valid(s) ? std::optional{s} : std::nullopt;
}

enum class Status { Unavailable, Fresh, Stale };
class Receiver {
public:
    void reset(std::uint64_t connectionGeneration) noexcept {
        generation_ = connectionGeneration;
        last_.reset();
        receivedAtMs_ = 0;
    }
    bool accept(Sample s, std::uint64_t connectionGeneration, std::uint64_t nowMs) noexcept {
        if (connectionGeneration != generation_ || !valid(s)) return false;
        if (last_) {
            // Reject delayed packets and unexpected server restarts until reconnection.
            if (last_->epoch != s.epoch || s.sequence <= last_->sequence
                || nowMs < receivedAtMs_) return false;
        }
        last_ = s;
        receivedAtMs_ = nowMs;
        return true;
    }
    Status status(std::uint64_t connectionGeneration, std::uint64_t nowMs) const noexcept {
        if (connectionGeneration != generation_ || !last_) return Status::Unavailable;
        if (nowMs < receivedAtMs_ || nowMs - receivedAtMs_ > freshnessMs)
            return Status::Stale;
        return Status::Fresh;
    }
    std::optional<Sample> freshSample(std::uint64_t connectionGeneration, std::uint64_t nowMs) const noexcept {
        return status(connectionGeneration, nowMs) == Status::Fresh ? last_ : std::nullopt;
    }
private:
    std::uint64_t generation_{};
    std::uint64_t receivedAtMs_{};
    std::optional<Sample> last_;
};
} // namespace praxis::companion::tabtps
