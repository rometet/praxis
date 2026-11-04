#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace praxis::companion::packs {
struct Pack {
    std::string key, name, uuid, version, reason, categories;
    bool selectable{};
    // Informational classification; selectable still requires independent
    // manifest and native repository checks. No game/pack mutations here.
    std::string visualCoverage;
    bool blockAtlasMetadata{};
};
struct Catalog {
    std::uint64_t generation{};
    std::filesystem::path root;
    std::vector<Pack> packs;
    std::string error;
    bool complete{};
};
// Reads bounded manifests only; does not load textures, install or mutate packs.
Catalog scanCatalog(std::filesystem::path const&, std::atomic_bool const& cancel);
struct Selection {
    std::uint64_t generation{};
    std::vector<std::string> order; // highest priority first, proposed only
    void reconcile(Catalog const&);
    bool enable(Catalog const&, std::string const&, bool);
    bool move(std::string const&, int direction);
};
enum class ApplyResult { Busy, StaleCatalog, UnsupportedPack, Valid };
// All return paths preserve the current Minecraft stack and the proposal.
ApplyResult checkApply(Catalog const&, Selection const&, bool busy) noexcept;
}
