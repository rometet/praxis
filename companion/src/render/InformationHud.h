#pragma once
#include "core/UiLayoutPolicy.h"
#include <cstdint>
namespace praxis::companion {
void publishDurabilityHudBounds(HudRect, std::uint64_t generation) noexcept;
HudRect durabilityHudBounds() noexcept;
HudRect informationHudBounds() noexcept;
bool informationHudNeeded() noexcept;
void drawInformationHud(void* context) noexcept;
} // namespace praxis::companion
