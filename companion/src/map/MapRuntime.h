#pragma once
#include <cstdint>
#include <filesystem>
#include "map/MapTypes.h"
class LocalPlayer;
namespace praxis::companion {
// Consumer entrypoints. The integration owner wires these into existing hosts.
bool initializeMap(std::filesystem::path const &ownedDataRoot) noexcept;
void updateMap(LocalPlayer &, std::uint64_t generation) noexcept;
void retireMap() noexcept;
void shutdownMap() noexcept;
void toggleMapEnabled() noexcept;
bool mapHudNeeded() noexcept;
void drawMapHud(void *suppliedContext) noexcept;
void drawMapSettings() noexcept;
// Existing action dispatcher supplies allowed key edges; Map never polls input.
std::array<int, map::mapActionCount> mapActionBindings() noexcept;
bool setMapActionBinding(map::MapAction,int key) noexcept;
bool dispatchMapAction(map::MapAction) noexcept;
void setMapActionDispatcherAvailable(bool) noexcept;
} // namespace praxis::companion
