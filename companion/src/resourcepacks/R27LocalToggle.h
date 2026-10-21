#pragma once
// R27: a single registered, ordinary local texture RP may be appended to
// Global or removed from its tail in the specific disposable offline world.
// Never replace a non-tail entry, reorder protected packs, or silently swap
// one pack for another. This is a pure shape check; it grants no native API.
#include "resourcepacks/PackPlan.h"
#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace praxis::companion::packs {
enum class R27LocalToggle {Invalid,Enable,Disable,NoChange};

inline R27LocalToggle r27NativeToggleShape(
    std::span<std::string const> oldKeys,
    std::span<std::string const> proposedKeys,
    std::size_t requestedPackCount,
    std::size_t pinnedLocalPackCount) noexcept {
    if(pinnedLocalPackCount!=1)return R27LocalToggle::Invalid;
    if(requestedPackCount==1
        && proposedKeys.size()==oldKeys.size()+1
        && std::equal(oldKeys.begin(),oldKeys.end(),proposedKeys.begin()))
        return R27LocalToggle::Enable;
    if(requestedPackCount==0
        && oldKeys.size()==proposedKeys.size()+1
        && std::equal(proposedKeys.begin(),proposedKeys.end(),oldKeys.begin()))
        return R27LocalToggle::Disable;
    return R27LocalToggle::Invalid;
}

// Runtime PackInstanceId identifiers in Bedrock 1.26.51.01 may use
// PackType::Invalid (0) even when the separately verified manifest is
// PackType::Resources (6). Compare exact UUID+version+no subpack and require
// this pack to be a selectable registered texture candidate. The native
// setter additionally checks its repository owner and actual manifest.
inline bool r27MatchesLocalRegisteredPack(Pack const& pack,AppliedPack const& active) noexcept {
    return pack.selectable&&active.subpack.empty()
        &&(active.packType==0||active.packType==6)
        &&pack.uuid==active.uuid&&pack.version==active.version;
}
struct R27UiToggleState {
    bool targetActive{},oneEditableCurrent{},atEditableTail{},
         canEnable{},canDisable{};
};
// The UI uses the game's OBSERVED Global state, not an optimistic pending
// checkmark. This routine does not change ResourcePackManager in any way.
inline R27UiToggleState r27UiToggleState(
    Catalog const& catalog,AppliedStackView const& current,Pack const& target) noexcept {
    R27UiToggleState s{};
    if(!catalog.complete||!current.known||!target.selectable)return s;
    std::size_t editable{};
    for(std::size_t i=0;i<current.order.size();++i) {
        auto const& active=current.order[i];
        bool isEditable=std::any_of(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& pack){return r27MatchesLocalRegisteredPack(pack,active);});
        if(isEditable){
            ++editable;
            if(i==current.order.size()-1)s.atEditableTail=true;
        }
        if(r27MatchesLocalRegisteredPack(target,active))s.targetActive=true;
    }
    s.oneEditableCurrent=editable==1;
    s.canEnable=!s.targetActive&&editable==0;
    s.canDisable=s.targetActive&&s.oneEditableCurrent&&s.atEditableTail;
    return s;
}
} // namespace praxis::companion::packs
