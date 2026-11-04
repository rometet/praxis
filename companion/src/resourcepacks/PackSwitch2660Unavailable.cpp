#include "resourcepacks/TitlePackSwitch.h"
#include "resourcepacks/WorldPackSwitch.h"

// 26.60 SDK does not export the original 26.51 native RP apply path.
// Keep browse/catalog features intact, but never claim apply/reload worked.
// The 26.51 implementations remain in TitlePackSwitch.cpp / WorldPackSwitch.cpp.
namespace praxis::companion::packs {
bool installTitlePackSwitch() noexcept { return false; }
bool stopTitlePackSwitch() noexcept { return true; }
bool resourcePackImageVerified() noexcept { return false; }
bool installWorldPackSwitch() noexcept { return false; }
bool stopWorldPackSwitch() noexcept { return true; }
void invalidateWorldPackOperation() noexcept {}
}