#pragma once
#include "resourcepacks/PackCatalog.h"
#include <memory>
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
void cancelScan() noexcept;
BrowserView browserView();
}
