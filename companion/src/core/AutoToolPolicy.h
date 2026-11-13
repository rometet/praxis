#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace praxis::companion {
struct AutoToolOptions {
    bool switchBack = true;
    bool requireImprovement = true;
    bool skipLowDurability = true;
    int cooldownMs = 250;
    bool operator==(AutoToolOptions const&) const = default;
};
inline bool validAutoToolOptions(AutoToolOptions const& o) noexcept {
    return o.cooldownMs >= 0 && o.cooldownMs <= 2000;
}
struct MiningCandidate {
    float speed{};
    int remaining{};
    int maximum{};
    bool protectedItem{};
    bool empty{true};
};
inline int chooseMiningTool(std::array<MiningCandidate, 9> const& slots, int current,
                            AutoToolOptions const& o) noexcept {
    if (current < 0 || current >= 9 || !validAutoToolOptions(o)) return -1;
    auto speed = [](MiningCandidate const& c) {
        return std::isfinite(c.speed) && c.speed > 0 ? c.speed : 0.0f;
    };
    auto eligible = [&](MiningCandidate const& c) {
        return !c.empty && std::isfinite(c.speed) && c.speed > 0
            && !(o.skipLowDurability && c.protectedItem);
    };
    int best = -1;
    for (int slot = 0; slot < 9; ++slot) {
        auto const& c = slots[slot];
        if (!eligible(c)) continue;
        if (best < 0) { best = slot; continue; }
        auto const& b = slots[best];
        float const cs = speed(c), bs = speed(b);
        if (cs > bs + 1e-4f) best = slot;
        else if (std::abs(cs - bs) <= 1e-4f) {
            if (c.remaining > b.remaining) best = slot;
            else if (c.remaining == b.remaining && slot == current) best = slot;
            else if (c.remaining == b.remaining && best != current && slot < best) best = slot;
        }
    }
    if (best < 0 || best == current) return best;
    if (o.requireImprovement && eligible(slots[current])
        && speed(slots[best]) <= speed(slots[current]) + 1e-4f) return current;
    return best;
}
// One decision per distinct target; repeated ticks never select a slot again.
struct MiningTargetKey {
    int x{}, y{}, z{}, dimension{};
    std::uint64_t generation{};
    bool operator==(MiningTargetKey const&) const = default;
};
struct MiningSwitchState {
    MiningTargetKey target{};
    bool hasTarget{};
    int selectedSlot{-1};
    int previousSlot{-1};
    std::uint64_t generation{};
    std::uint64_t lastSwitchMs{};
    bool observe(MiningTargetKey const& next, std::uint64_t now, std::uint32_t cooldownMs) noexcept {
        if (hasTarget && target == next) return false;
        target = next;
        hasTarget = true;
        selectedSlot = previousSlot = -1;
        if (lastSwitchMs && now >= lastSwitchMs && cooldownMs && now - lastSwitchMs < cooldownMs) return false;
        lastSwitchMs = now;
        return true;
    }
    void release() noexcept {
        hasTarget = false;
        target = {};
        previousSlot = -1;
    }
    void clear() noexcept { *this = {}; }
};
} // namespace praxis::companion
