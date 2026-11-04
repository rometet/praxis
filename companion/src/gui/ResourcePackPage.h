#pragma once
#include "resourcepacks/PackPlan.h"
#include "resourcepacks/TitlePackControl.h"
namespace praxis::companion {
// Production receives copied fresh-title observations from its native owner.
// Other contexts remain Unknown. Tests can supply explicit CPU fixtures.
void drawResourcePackPage(packs::AppliedStackView const& current = {});
enum class ResourcePackAction {None,Apply,Reload};
ResourcePackAction drawResourcePackApplyControls(packs::PackPlan const&,packs::TitlePackView const& = {});
}
