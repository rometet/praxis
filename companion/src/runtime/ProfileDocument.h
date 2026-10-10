#pragma once
#include "runtime/ConfigMigration.h"
#include "runtime/ProfileEditHistory.h"
#include <span>
namespace praxis::companion {
// Module fields absent from an old source profile inherit the
// destination unless edited after load. Other typed settings persist normally.
// Owners adding typed module fields must extend this descriptor list; untyped
// future fields already remain opaque in the source/destination documents.
struct ProfileFieldOwner {char const* field;unsigned slot;};
inline constexpr std::array<ProfileFieldOwner,23> featureProfileFields{{
    {"timeChanger",7},{"weatherChanger",8},{"hitboxOptions",9},
    {"toolProtection",10},{"toolProtectionOptions",10},
    {"weaponSwitch",11},{"weaponSwitchOptions",11},
    {"handRestock",12},{"handRestockOptions",12},
    {"inventorySort",13},{"inventorySortOptions",13},
    {"inventoryTransfer",14},{"inventoryTransferOptions",14},
    {"freelook",20},{"freelookOptions",20},
    {"freeCamera",21},{"freeCameraOptions",21},
    {"inventoryHud",23},{"inventoryHudOptions",23},
    {"antiEffect",24},{"antiEffectOptions",24},{"offhandSwap",26},{"weatherOptions",8}}};
static_assert(featureProfileFields.size()<=featureRegistryBits,
    "Each typed profile owner must have a representable history bit");
inline void recordProfileEdits(ProfileEditHistory& history,ConfigJson const& before,ConfigJson const& after) {
    for(unsigned i=0;i<featureCount;++i) {
        if(!(ownedFeatureSlotMask&featureRegistryBit(i)))continue;
        if(before["keybinds"][i]!=after["keybinds"][i])history.keys|=featureRegistryBit(i);
        if(before["keybindModifiers"][i]!=after["keybindModifiers"][i])history.modifiers|=featureRegistryBit(i);
    }
    history.favorites|=(before["favoriteModules"].get<std::uint32_t>()^after["favoriteModules"].get<std::uint32_t>())&ownedFeatureSlotMask;
    for(unsigned i=0;i<featureProfileFields.size();++i) {
        auto const* field=featureProfileFields[i].field;
        if(before[field]!=after[field])history.fields|=featureRegistryBit(i);
    }
}
inline std::size_t profileBindingExtent(nlohmann::ordered_json const& document) {
    std::size_t count{};
    for(auto field:{"keybinds","keybindModifiers"})
        if(auto value=document.find(field);value!=document.end())count=std::max(count,value->size());
    return count;
}
// current/baseline are typed codec output. source is the ORIGINAL document,
// before seven/eight/nine-slot padding; target is independently read at save.
inline nlohmann::ordered_json mergeRuntimeProfileDocument(nlohmann::ordered_json const& current,
    nlohmann::ordered_json const& baseline,nlohmann::ordered_json source,nlohmann::ordered_json target,
    std::span<ProfileFieldOwner const> fieldOwners=featureProfileFields,ProfileEditHistory history={}) {
    auto extent=profileBindingExtent(source);
    auto targetExtent=profileBindingExtent(target);
    auto targetFavorite=target.value("favoriteModules",std::uint32_t{});
    auto targetAuthority=(targetExtent?featureRegistryMask(static_cast<unsigned>(targetExtent)):
        target.contains("favoriteModules")?ownedFeatureSlotMask:0u)|targetFavorite;
    if(!normalizeFeatureKeybinds(source)||!normalizeFeatureKeybinds(target))
        throw std::runtime_error("Invalid source/destination module bindings");
    auto document=baseline;overlayProfileFields(document,target);
    auto sourceFields=source;
    for(auto field:{"keybinds","keybindModifiers","favoriteModules"})sourceFields.erase(field);
    overlayProfileFields(document,sourceFields);
    auto count=std::max({profileBindingExtent(document),profileBindingExtent(current),extent,
        static_cast<std::size_t>(featureCount)});
    if(count>featureRegistryBits)throw std::runtime_error("Unsupported registry extent");
    for(auto const& [field,defaultValue]:{std::pair{"keybinds",0},std::pair{"keybindModifiers",8}}) {
        auto& values=document[field];if(!values.is_array())throw std::runtime_error("Invalid saved binding array");
        while(values.size()<count)values.push_back(defaultValue);
        if(extent)for(std::size_t i=0;i<extent;++i)values[i]=source[field][i];
        // New bindings edited since load may legitimately occupy an appended
        // known slot; padding alone never erases the destination's tail.
        auto edits=std::string_view(field)=="keybinds"?history.keys:history.modifiers;
        for(unsigned i=0;i<featureCount;++i)
            if((ownedFeatureSlotMask&featureRegistryBit(i))
                &&((edits&featureRegistryBit(i))||current[field][i]!=baseline[field][i]))values[i]=current[field][i];
    }
    auto favorite=target.value("favoriteModules",baseline.value("favoriteModules",std::uint32_t{}));
    auto sourceFavoriteAuthority=extent?featureRegistryMask(static_cast<unsigned>(extent)):ownedFeatureSlotMask;
    if(source.contains("favoriteModules")) {
        auto incoming=source["favoriteModules"].get<std::uint32_t>();
        // A shorter source owns its declared prefix. Accepted favorite bits
        // beyond that prefix may fill a new destination, never erase/replace
        // existing destination metadata for a later slot.
        auto mask=sourceFavoriteAuthority|(incoming&~targetAuthority);
        favorite=(favorite&~mask)|(incoming&mask);
    }
    auto changed=(baseline["favoriteModules"].get<std::uint32_t>()^current["favoriteModules"].get<std::uint32_t>())
        &ownedFeatureSlotMask;
    changed|=history.favorites&ownedFeatureSlotMask;
    favorite=(favorite&~changed)|(current["favoriteModules"].get<std::uint32_t>()&changed);
    auto owned=current;
    for(auto field:{"keybinds","keybindModifiers","favoriteModules"})owned.erase(field);
    for(unsigned i=0;i<fieldOwners.size();++i) {
        auto const& owner=fieldOwners[i];
        if(!source.contains(owner.field)&&!(history.fields&featureRegistryBit(i))
            &&current.contains(owner.field)&&baseline.contains(owner.field)&&current[owner.field]==baseline[owner.field])
            owned.erase(owner.field);
    }
    // Root/nested fields outside the typed schema are retained; recognized HUD,
    // font/theme and other current settings are saved even when omitted in an
    // old source document. Ownership of omitted module fields is conditional.
    // InventoryHUD23 is data-only. Preserve source/destination bytes semantically,
    // even if a typed caller edits the retired fields or resets other HUD panels.
    owned.erase("inventoryHud");owned.erase("inventoryHudOptions");
    if(owned.contains("informationOptions") && owned["informationOptions"].contains("hudLayouts"))
        owned["informationOptions"]["hudLayouts"].erase("inventory");
    overlayProfileFields(document,owned);
    document["favoriteModules"]=favorite;document["version"]=current["version"];
    return document;
}
}
