#pragma once
#include "core/SaturationPolicy.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
namespace praxis::companion {
// Called only with Player::SATURATION() and its typed getter result in the
// owning UI callback. The registered ID is read, never guessed or hardcoded.
inline SaturationObservation readSaturationObservation(Attribute const& registered,
    AttributeInstanceConstRef const& ref) noexcept {
    auto* instance=ref.mPtr;
    if(!instance)return {};
    if(instance->mAttribute!=&registered
        ||registered.mName.get().getString()!="minecraft:player.saturation")return {true,false};
    return {true,true,
        registered.mSyncable,registered.mIDValue,instance->mCurrentMinValue,
        instance->mCurrentMaxValue,instance->mDefaultValue,instance->mCurrentValue};
}
}
