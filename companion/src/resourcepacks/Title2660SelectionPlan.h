#pragma once
#include "resourcepacks/PackCatalog.h"
#include "resourcepacks/PackPlan.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace praxis::companion::packs {

enum class Title2660SelectionResult {
    ReorderReady, Unchanged, InvalidCatalog, UnknownNative,
    UnsupportedOrNotActive, UnsupportedPackCount, ForeignNativeEntry
};

struct Title2660SelectionPlan {
    Title2660SelectionResult result{Title2660SelectionResult::UnknownNative};
    std::vector<AppliedPack> ordered;
};

// 26.60 phase 1: support ONLY rearranging existing, uniquely identified
// registered plain texture packs. Never silently enable/disable a pack.
// Pack entries that the scanner refuses to edit (UI/subpack/behavior/etc.)
// keep their exact original position, ID, subpack, version and pack type.
inline Title2660SelectionPlan planTitle2660Reorder(
    Catalog const& catalog, Selection const& selection,
    AppliedStackView const& native)
{
    Title2660SelectionPlan plan;
    if (!native.known) {plan.result=Title2660SelectionResult::UnknownNative;return plan;}
    if (!catalog.complete || !catalog.error.empty() ||
        selection.generation!=catalog.generation) {
        plan.result=Title2660SelectionResult::InvalidCatalog;return plan;
    }
    for(std::size_t i=0;i<selection.order.size();++i){
        auto const& key=selection.order[i];
        if(std::find(selection.order.begin(),selection.order.begin()+i,key)
           !=selection.order.begin()+i ||
           std::none_of(catalog.packs.begin(),catalog.packs.end(),
               [&](Pack const& p){return p.key==key&&p.selectable;})) {
            plan.result=Title2660SelectionResult::InvalidCatalog;return plan;
        }
    }
    if (native.order.empty() || native.order.size()>512 ||
        selection.order.empty() || selection.order.size()>native.order.size()) {
        plan.result=Title2660SelectionResult::UnsupportedPackCount;return plan;
    }

    // A selected pack must match exactly one *already active* native entry,
    // and every native entry editable by this particular catalog must be
    // present in the selection. This disallows removals and enables.
    std::vector<std::size_t> editableIndices;
    std::vector<AppliedPack> proposed;
    editableIndices.reserve(native.order.size());
    proposed.reserve(selection.order.size());
    for (std::size_t i=0;i<native.order.size();++i) {
        auto const& active=native.order[i];
        auto it=std::find_if(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& p){return p.selectable && matchesApplied(p,active);});
        if(it!=catalog.packs.end()){
            if (std::count_if(catalog.packs.begin(),catalog.packs.end(),
                [&](Pack const& p){return p.selectable && matchesApplied(p,active);})!=1) {
                plan.result=Title2660SelectionResult::ForeignNativeEntry;return plan;
            }
            editableIndices.push_back(i);
        }
    }
    if(editableIndices.size()!=selection.order.size()){
        plan.result=Title2660SelectionResult::UnsupportedOrNotActive;return plan;
    }
    for(auto const& key:selection.order){
        auto it=std::find_if(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& p){return p.key==key&&p.selectable;});
        if(it==catalog.packs.end()){
            plan.result=Title2660SelectionResult::UnsupportedOrNotActive;return plan;
        }
        std::vector<AppliedPack> matched;
        for(auto const i:editableIndices)if(matchesApplied(*it,native.order[i]))
            matched.push_back(native.order[i]);
        if(matched.size()!=1) {
            plan.result=Title2660SelectionResult::UnsupportedOrNotActive;return plan;
        }
        proposed.push_back(matched.front());
    }
    plan.ordered=native.order;
    for(std::size_t i=0;i<editableIndices.size();++i)
        plan.ordered[editableIndices[i]]=std::move(proposed[i]);
    plan.result=plan.ordered.size()==native.order.size() &&
        std::equal(plan.ordered.begin(),plan.ordered.end(),native.order.begin(),
            [](AppliedPack const& a,AppliedPack const& b) {
                return a.uuid==b.uuid&&a.version==b.version&&
                    a.subpack==b.subpack&&a.packType==b.packType;
            })?Title2660SelectionResult::Unchanged:Title2660SelectionResult::ReorderReady;
    return plan;
}

} // namespace praxis::companion::packs
