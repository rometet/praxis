#pragma once
class LocalPlayer;
namespace praxis::companion {
bool installHandRestock();
bool removeHandRestock() noexcept;
void tickHandRestock(LocalPlayer& player) noexcept;
void serviceHandRestock() noexcept;
void resetHandRestock() noexcept;
void requestHandRestockReset() noexcept;
char const* handRestockStatus() noexcept;
}
