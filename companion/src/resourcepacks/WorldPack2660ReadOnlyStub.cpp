#include "resourcepacks/WorldPackSwitch.h"
// 26.60 audit: preserve the working game and disable all world RP mutations.
namespace praxis::companion::packs {
bool installWorldPackSwitch() noexcept { return false; }
bool stopWorldPackSwitch() noexcept { return true; }
void invalidateWorldPackOperation() noexcept {}
}