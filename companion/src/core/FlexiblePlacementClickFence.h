#pragma once
#include "FlexiblePlacementPolicy.h"
#include <cstdint>

namespace praxis::companion {
// The same held mouse press must not immediately use an interactive block
// that was just placed asynchronously. Unrelated cells and later clicks pass.
class FlexiblePlacementClickFence {
    FlexibleCell destination_{};
    std::uintptr_t actor_{};
    std::uint64_t generation_{};
    std::uint64_t acceptedAt_{};
    std::uint64_t claimedAt_{},claimedGeneration_{};
    bool active_{},claimed_{};
public:
    void clear() noexcept { active_ = claimed_ = false; }
    void accepted(FlexibleCell destination, std::uintptr_t actor,
                  std::uint64_t generation, std::uint64_t now) noexcept {
        destination_ = destination; actor_ = actor;
        generation_ = generation; acceptedAt_ = now; active_ = true;
    }
    void observe(bool buttonDown, std::uint64_t generation, std::uint64_t now) noexcept {
        // Only an observed mouse release or world change re-arms a press.
        // A held repeat cannot issue another network placement.
        if (!buttonDown || (claimed_ && (generation != claimedGeneration_ || now < claimedAt_)))
            claimed_ = false;
        if (!buttonDown || generation != generation_ || now < acceptedAt_) active_ = false;
    }
    // 26.60 useItemOn can report first=false even for a new physical press.
    // Do not trust that virtual parameter as a mouse-down edge: claim exactly
    // one transformed action per observed RMB press instead.
    bool claim(bool buttonDown, std::uint64_t generation, std::uint64_t now) noexcept {
        observe(buttonDown, generation, now);
        if (!buttonDown || claimed_) return false;
        claimed_ = true;
        claimedGeneration_ = generation;
        claimedAt_ = now;
        return true;
    }
    bool blocks(FlexibleCell cell, std::uintptr_t actor, std::uint64_t generation,
                bool buttonDown, std::uint64_t now) noexcept {
        observe(buttonDown, generation, now);
        return active_ && actor == actor_ && cell == destination_;
    }
};
} // namespace praxis::companion
