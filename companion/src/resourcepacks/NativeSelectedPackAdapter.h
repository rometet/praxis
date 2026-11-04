#pragma once
// Read-only replacement for the private 26.51 sorted-selected-content API.
// Included within WorldPackSwitch.cpp's anonymous namespace after native
// SDK types and the existing keys()/nativeVanillaPackChange() helpers.
//
// The native Settings selection is read only. Existing selected subpack
// identities are copied unchanged; new selections may never invent subpack
// names or marketplace entitlements. Exact delta is verified separately.
struct NativeSelectedPackView {
    std::uint64_t visualOrder{};
    std::size_t contentIndex{};
    PackInstanceId identity;
    NativeSelectedPackView(std::uint64_t order,std::size_t index,PackInstanceId id)
        :visualOrder(order),contentIndex(index),identity(std::move(id)) {}
};

std::optional<std::vector<PackInstanceId>> nativeSelectedGlobalPacks(
    PackManagerContentSourceImpl const& source,
    std::vector<PackInstanceId> const& original
) {
    if(source.mContentType!=ContentType::Resource || original.size()>512)
        return std::nullopt;
    auto const& list=source.mPackDataContent.get();
    if(list.empty() || list.size()>1024)return std::nullopt;
    std::vector<NativeSelectedPackView> entries;
    entries.reserve(list.size());
    std::vector<std::string> seen;
    for(std::size_t i=0;i<list.size();++i) {
        auto const& content=list[i];
        if(!content)return std::nullopt;
        auto const* position=content->mListPosition.get().get();
        if(!position)return std::nullopt;
        auto const type=position->mListType;
        if(type!=PackListType::Selected) {
            if(type!=PackListType::Available)return std::nullopt;
            continue;
        }
        // Never infer a subpack name from this SDK's UI index: existing
        // selected packs preserve their exact original native identity.
        auto const* manifest=content->mManifest.get();
        if(!manifest)return std::nullopt;
        auto const& id=manifest->mIdentity.get();
        if(id.mPackType!=PackType::Resources
            &&id.mPackType!=PackType::Invalid)return std::nullopt;
        auto fingerprint=id.mId->asString()+"/"+id.mVersion->asString();
        if(std::find(seen.begin(),seen.end(),fingerprint)!=seen.end())
            return std::nullopt;
        seen.push_back(std::move(fingerprint));
        // A previously selected pack keeps its exact original ID type and
        // subpack name. 26.60 sometimes records Invalid vs Resources in
        // the two structures; UUID and version identify that owner.
        auto old=std::find_if(original.begin(),original.end(),[&](auto const& value) {
            return value.mPackId->mId->asString()==id.mId->asString()
                &&value.mPackId->mVersion->asString()==id.mVersion->asString();
        });
        if(old!=original.end()) {
            entries.emplace_back(position->mPosition,i,*old);
        }else{
            if(position->mSubpackIndex>0)return std::nullopt;
            // Only the ordinary default-subpack local texture candidate
            // may be added. R29 separately verifies its repository owner,
            // original manifest, source path and entitlement-safe identity.
            entries.emplace_back(position->mPosition,i,
                PackInstanceId(id,std::string{}));
        }
        if(entries.size()>512)return std::nullopt;
    }
    if(entries.empty() && !original.empty())return std::nullopt;
    std::vector<std::size_t> visualIndices;
    visualIndices.reserve(entries.size());
    for(std::size_t i=0;i<entries.size();++i)visualIndices.push_back(i);
    // PackInstanceId has no linked assignment operator in 26.60: sort
    // integer indices, never move-assign the native ID itself.
    std::sort(visualIndices.begin(),visualIndices.end(),[&](auto a,auto b) {
        return entries[a].visualOrder<entries[b].visualOrder ||
            (entries[a].visualOrder==entries[b].visualOrder
                && entries[a].contentIndex<entries[b].contentIndex);
    });
    for(std::size_t i=1;i<visualIndices.size();++i)
        if(entries[visualIndices[i-1]].visualOrder==entries[visualIndices[i]].visualOrder)
            return std::nullopt;
    std::vector<PackInstanceId> ascending;
    ascending.reserve(entries.size());
    for(auto idx:visualIndices)ascending.emplace_back(entries[idx].identity);
    std::vector<PackInstanceId> descending;
    descending.reserve(ascending.size());
    for(auto it=ascending.rbegin();it!=ascending.rend();++it)
        descending.emplace_back(*it);

    auto oldKeys=keys(original);
    auto ascDelta=nativeVanillaPackChange(oldKeys,keys(ascending)).delta;
    auto descDelta=nativeVanillaPackChange(oldKeys,keys(descending)).delta;
    auto supported=[](NativeVanillaPackDelta value) {
        return value==NativeVanillaPackDelta::Unchanged
            ||value==NativeVanillaPackDelta::EnableOne
            ||value==NativeVanillaPackDelta::DisableOne;
    };
    // If the UI's visual order cannot be anchored to the original native
    // global stack, neither direction is admitted. Do not arbitrarily
    // reverse selected content or infer a new stack ordering.
    if(supported(ascDelta) && !supported(descDelta))return ascending;
    if(!supported(ascDelta) && supported(descDelta))return descending;
    if(supported(ascDelta) && supported(descDelta)
        &&keys(ascending)==keys(descending))return ascending;
    return std::nullopt;
}
