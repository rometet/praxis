#pragma once
namespace praxis::companion {
// No new native hook is registered by this runtime. It publishes a movement
// intent to the already-installed Praxis FreeCamera input extraction bridge.
// Caller validates that the hook is installed and the local input is current.
struct ToggleInputIntent {
    bool sneak{}, sprint{};
};
ToggleInputIntent gameplayToggleInputIntent() noexcept;
bool startGameplayRuntime();
bool stopGameplayRuntime() noexcept;
void resetGameplayRuntime() noexcept;
} // namespace praxis::companion
