#pragma once
#include "resourcepacks/PackCatalog.h"
#include <memory>
#include <string_view>
namespace praxis::companion::packs {
struct BrowserView {
    std::shared_ptr<Catalog const> catalog;
    bool running{}, busy{};
    std::string status;
};
bool startBrowser();
// Waits at most 500ms for ownership retirement. False requires DLL retention.
bool stopBrowser() noexcept;
bool requestScan(std::filesystem::path const&);
// UI-only folder discovery. Prefer the engine's copied native repository
// path; the LeviLauncher Shared data root is an existing-folder fallback.
std::string suggestedLocalPackRoot(std::string_view nativeRoot, bool development=false) noexcept;
void cancelScan() noexcept;
BrowserView browserView();
}
