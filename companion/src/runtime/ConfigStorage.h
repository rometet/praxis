#pragma once
#include "core/RuntimePolicy.h"
#include "runtime/ProfileEditHistory.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <memory>
namespace praxis::companion {
struct LoadedProfile { RuntimeConfig config; std::uint64_t requestRevision{}; std::string sourceDocument; };
bool startConfigStorage(std::filesystem::path const& configFile);
bool queueConfigSave(RuntimeConfig const&, std::filesystem::path const&, std::string sourceDocument = {},ProfileEditHistory = {});
bool queueProfileLoad(std::filesystem::path const&, std::uint64_t revision);
bool queueProfileDelete(std::filesystem::path const&);
std::vector<std::string> cachedProfileNames();
std::optional<LoadedProfile> takeLoadedProfile();
std::string configStorageStatus();
bool stopConfigStorage() noexcept;
}
