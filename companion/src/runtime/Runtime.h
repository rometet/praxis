#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "core/RuntimePolicy.h"

namespace praxis::companion {
struct RuntimeConfigView {RuntimeConfig config;std::uint64_t revision;};
RuntimeConfigView runtimeConfigView();
bool updateRuntimeConfigIfRevision(RuntimeConfig const&,std::uint64_t expectedRevision,std::uint32_t bindingEdits=0);
bool loadRuntimeConfig(std::filesystem::path const& path);
RuntimeConfig runtimeConfigSnapshot();
bool updateRuntimeConfig(RuntimeConfig const& next);
bool flushRuntimeConfig() noexcept;
std::vector<std::string> listRuntimeProfiles();
bool saveRuntimeProfile(std::string const& name) noexcept;
bool loadRuntimeProfile(std::string const& name) noexcept;
bool deleteRuntimeProfile(std::string const& name) noexcept;
bool handleZoomScroll(int direction);
void adjustZoomMouse(short& dx, short& dy);
void resetWorldRuntime() noexcept;
void activateRuntime();
void deactivateRuntime() noexcept;
} // namespace praxis::companion
