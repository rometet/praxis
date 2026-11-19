#pragma once
// Reusable server-side 1Hz sampler. No BDS hooks: caller supplies completed
// simulation ticks and verified full-frame duration on the server thread.
#include "core/TabTpsProtocol.h"
#include <cstdint>
#include <limits>
#include <optional>

namespace praxis::companion::tabtps {
class ServerSampler {
public:
    void start(std::uint32_t nonzeroEpoch, std::uint64_t monotonicNowMs) noexcept {
        epoch_ = nonzeroEpoch;
        nextSequence_ = 1;
        startWindow(monotonicNowMs);
    }
    bool completedTick(std::uint64_t monotonicNowMs,
                       std::optional<std::uint32_t> fullFrameUs = std::nullopt) noexcept {
        if (!epoch_ || monotonicNowMs < windowStartMs_) return false;
        if (tickCount_ == 1200) { overflow_ = true; return false; }
        ++tickCount_;
        if (!fullFrameUs || *fullFrameUs == 0 || *fullFrameUs > 5000000) {
            allFramesMeasured_ = false;
        } else {
            frameSumUs_ += *fullFrameUs;
        }
        return true;
    }
    // Call at most once per tick or from a regular server-thread scheduler.
    // Does not fabricate data during pauses, and never emits from a stalled tick.
    std::optional<Sample> poll(std::uint64_t monotonicNowMs) noexcept {
        if (!epoch_ || monotonicNowMs < windowStartMs_) {
            if (epoch_) startWindow(monotonicNowMs);
            return std::nullopt;
        }
        auto const elapsed = monotonicNowMs - windowStartMs_;
        if (elapsed < 1000) return std::nullopt;
        auto const ticks = tickCount_;
        auto const overflowed = overflow_;
        auto const hasCompleteMspt = allFramesMeasured_ && ticks != 0;
        auto const meanFrameUs = hasCompleteMspt ? static_cast<std::uint32_t>(
            (frameSumUs_ + ticks / 2u) / ticks) : 0u;
        auto const sequence = nextSequence_;
        startWindow(monotonicNowMs);
        // Require a new server epoch before sequence wrap.
        if (!sequence || sequence == std::numeric_limits<std::uint32_t>::max()
            || elapsed > 60000 || !ticks || overflowed) return std::nullopt;
        Sample s{epoch_, sequence, ticks, static_cast<std::uint32_t>(elapsed), meanFrameUs};
        if (!valid(s)) return std::nullopt;
        ++nextSequence_;
        return s;
    }
private:
    void startWindow(std::uint64_t nowMs) noexcept {
        windowStartMs_ = nowMs;
        tickCount_ = 0;
        frameSumUs_ = 0;
        overflow_ = false;
        allFramesMeasured_ = true;
    }
    std::uint32_t epoch_{};
    std::uint32_t nextSequence_{};
    std::uint64_t windowStartMs_{};
    std::uint32_t tickCount_{};
    std::uint64_t frameSumUs_{};
    bool overflow_{};
    bool allFramesMeasured_{true};
};
} // namespace praxis::companion::tabtps
