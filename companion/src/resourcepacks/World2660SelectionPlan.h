#pragma once
#include "resourcepacks/PackCatalog.h"
#include "resourcepacks/PackPlan.h"
#include "resourcepacks/TitlePackTransaction.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace praxis::companion::packs {
enum class World2660PlanResult {
    Ready, Unchanged, StaleCatalog, UnknownNative, TooMany,
    DuplicateOrProtected, UnknownSelection
};
struct World2660Plan {
    World2660PlanResult result{World2660PlanResult::UnknownNative};
    std::vector<AppliedPack> target;
};
inline bool world2660Same(AppliedPack const& a,AppliedPack const& b){
    return a.uuid==b.uuid&&a.version==b.version&&
           a.subpack==b.subpack&&a.packType==b.packType;
}
// A world change edits only registered ordinary texture-pack identities in
// Global. The selected set may add, remove and reorder them, but all Level,
// server, treatment and protected Global entries are retained untouched.
// These pure CPU checks are repeated against native owners before the setter.
inline World2660Plan planWorld2660Global(Catalog const& catalog,
                    Selection const& selection,AppliedStackView const& original){
    World2660Plan plan;
    if(!original.known){plan.result=World2660PlanResult::UnknownNative;return plan;}
    if(!catalog.complete || !catalog.error.empty() ||
       selection.generation!=catalog.generation){
        plan.result=World2660PlanResult::StaleCatalog;return plan;
    }
    if(original.order.size()>512 || selection.order.size()>256){
        plan.result=World2660PlanResult::TooMany;return plan;
    }
    std::vector<AppliedPack> selected;
    selected.reserve(selection.order.size());
    for(auto const& key:selection.order){
        auto it=std::find_if(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& p){return p.key==key && p.selectable;});
        if(it==catalog.packs.end() || std::count_if(catalog.packs.begin(),
            catalog.packs.end(),[&](Pack const& p){return p.key==key && p.selectable;})!=1){
            plan.result=World2660PlanResult::UnknownSelection;return plan;
        }
        if(it->uuid.empty()||it->version.empty()){
            plan.result=World2660PlanResult::UnknownSelection;return plan;
        }
        AppliedPack chosen{it->uuid,it->version,{},6};
        if(std::any_of(selected.begin(),selected.end(),[&](auto const& x){
            return x.uuid==chosen.uuid;})){
            plan.result=World2660PlanResult::DuplicateOrProtected;return plan;
        }
        selected.push_back(std::move(chosen));
    }
    // An installed-but-protected pack is never the target of a mutation.
    // If the scanner lists two different keys with a common UUID, reject
    // the operation even when only one of them was selected.
    for(auto const& chosen:selected){
        if(std::count_if(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& p){return p.uuid==chosen.uuid;})!=1){
            plan.result=World2660PlanResult::DuplicateOrProtected;return plan;
        }
    }
    // At most one native instance per ordinary editable UUID. A duplicate
    // could be an engine/pack-management feature not visible in the scanner.
    for(std::size_t i=0;i<original.order.size();++i){
        auto const& native=original.order[i];
        if(std::count_if(original.order.begin(),original.order.end(),
            [&](AppliedPack const& x){return x.uuid==native.uuid;})!=1){
            plan.result=World2660PlanResult::DuplicateOrProtected;return plan;
        }
    }
    auto result=mergeTitleGlobal<AppliedPack>(
        std::span<AppliedPack const>(original.order.data(),original.order.size()),
        std::span<AppliedPack const>(selected.data(),selected.size()),
        [&](AppliedPack const& native){
            return std::count_if(catalog.packs.begin(),catalog.packs.end(),
                [&](Pack const& p){return p.selectable&&matchesApplied(p,native);})==1;
        },
        [](AppliedPack const& x,AppliedPack const& y){return x.uuid==y.uuid;});
    if(!result||result->size()>512){
        plan.result=World2660PlanResult::DuplicateOrProtected;return plan;
    }
    plan.target=std::move(*result);
    plan.result=std::equal(plan.target.begin(),plan.target.end(),
                 original.order.begin(),original.order.end(),world2660Same)
                 &&plan.target.size()==original.order.size()
        ?World2660PlanResult::Unchanged:World2660PlanResult::Ready;
    return plan;
}
} // namespace praxis::companion::packs
