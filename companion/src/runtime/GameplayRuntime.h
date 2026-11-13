#pragma once
namespace praxis::companion {
// Registers only LeviLamina public event listeners. No native hook, offset or
// memory scan is introduced by this runtime.
bool startGameplayRuntime();
bool stopGameplayRuntime() noexcept;
void resetGameplayRuntime() noexcept;
} // namespace praxis::companion
