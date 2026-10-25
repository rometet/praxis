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
    bool active_{};
public:
    void clear() noexcept { active_ = false; }
    void accepted(FlexibleCell destination, std::uintptr_t actor,
                  std::uint64_t generation, std::uint64_t now) noexcept {
        destination_ = destination; actor_ = actor;
        generation_ = generation; acceptedAt_ = now; active_ = true;
    }
    void observe(bool buttonDown, std::uint64_t generation, std::uint64_t now) noexcept {
        // One fixed-size record, owned by the physical press. A timeout would
        // let a long held press toggle the block after that timeout expired.
        if (!buttonDown || generation != generation_ || now < acceptedAt_) clear();
    }
    bool blocks(FlexibleCell cell, std::uintptr_t actor, std::uint64_t generation,
                bool buttonDown, std::uint64_t now) noexcept {
        observe(buttonDown, generation, now);
        return active_ && actor == actor_ && cell == destination_;
    }
};
} // namespace praxis::companion
