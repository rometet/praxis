#pragma once
class LocalPlayer;
namespace praxis::companion {
void installFastClicks() noexcept;
void removeFastClicks() noexcept;
void resetFastClicks() noexcept;
void tickFastClicks(::LocalPlayer& player) noexcept;
}
