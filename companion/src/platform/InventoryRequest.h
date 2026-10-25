#pragma once
#include "core/InventoryRequestPolicy.h"
#include <cstdint>
class ContainerManagerController;
namespace praxis::companion::inventory {
bool installRequests();
bool removeRequests() noexcept;
std::uint64_t beginRequest(ContainerManagerController& controller);
void finishRequest(ContainerManagerController& controller, std::uint64_t token);
RequestResult requestResult(std::uint64_t token);
bool requestPathBusy() noexcept;
void releaseRequest(std::uint64_t token) noexcept;
void resetRequests() noexcept;
}
