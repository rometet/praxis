#pragma once

#include <atomic>
#include <cmath>
#include <optional>
#include <utility>

namespace praxis::companion {

// A player alone is not evidence that the world has reached gameplay. Once a
// gameplay screen has rendered, inventory and pause screens retain readiness.
class FullbrightReadiness {
    std::atomic<void const*> observedPlayer_{};
    std::atomic<void const*> readyPlayer_{};

public:
    void observe(void const* player, bool gameplayScreen) noexcept {
        auto const* previous = observedPlayer_.exchange(player, std::memory_order_acq_rel);
        if (player != previous || !player) {
            readyPlayer_.store(nullptr, std::memory_order_release);
        }
        if (player && gameplayScreen) {
            readyPlayer_.store(player, std::memory_order_release);
        }
    }

    bool readyFor(void const* player) const noexcept {
        return player && readyPlayer_.load(std::memory_order_acquire) == player;
    }

    void reset() noexcept {
        observedPlayer_.store(nullptr, std::memory_order_release);
        readyPlayer_.store(nullptr, std::memory_order_release);
    }
};

template <class Original, class Override>
float fullbrightGamma(Original&& original, Override&& overrideValue) {
    float const vanilla = std::forward<Original>(original)();
    auto const replacement = std::forward<Override>(overrideValue)();
    if (replacement && std::isfinite(*replacement)
        && *replacement >= 1.0f && *replacement <= 25.0f) {
        return *replacement;
    }
    return vanilla;
}

} // namespace praxis::companion
