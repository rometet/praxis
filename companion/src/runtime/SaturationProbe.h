#pragma once
#include "core/SaturationPolicy.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
namespace praxis::companion {
// Called only with a typed Player attribute getter result in the owning UI
// callback. The registered ID and name are read, never guessed or hardcoded.
inline SaturationObservation readPlayerAttributeObservation(Attribute const& registered,
    AttributeInstanceConstRef const& ref, char const* expectedName) noexcept {
    auto* instance=ref.mPtr;
    if(!instance)return {};
    if(instance->mAttribute!=&registered || !expectedName
        ||registered.mName.get().getString()!=expectedName)return {true,false};
    return {true,true,
        registered.mSyncable,registered.mIDValue,instance->mCurrentMinValue,
        instance->mCurrentMaxValue,instance->mDefaultValue,instance->mCurrentValue};
}
inline SaturationObservation readSaturationObservation(Attribute const& registered,
    AttributeInstanceConstRef const& ref) noexcept {
    return readPlayerAttributeObservation(registered,ref,"minecraft:player.saturation");
}
inline SaturationObservation readExhaustionObservation(Attribute const& registered,
    AttributeInstanceConstRef const& ref) noexcept {
    return readPlayerAttributeObservation(registered,ref,"minecraft:player.exhaustion");
}
} // namespace praxis::companion
