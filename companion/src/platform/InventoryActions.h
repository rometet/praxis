#pragma once
namespace praxis::companion {
bool installInventoryActions();
bool removeInventoryActions() noexcept;
void resetInventoryActions() noexcept;
void serviceInventoryActions() noexcept;
bool handleInventoryMouse(int button,int data,bool alreadyCancelled) noexcept;
}
