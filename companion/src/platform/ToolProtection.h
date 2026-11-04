#pragma once
class LocalPlayer;
namespace praxis::companion {
bool installToolProtection();
bool removeToolProtection() noexcept;
void resetToolProtection() noexcept;
void observeToolProtection(LocalPlayer& player) noexcept;
}
