#pragma once
#include "core/PlayerHudPolicy.h"
#include <cstdint>
#include <type_traits>
namespace praxis::companion {
// Owned SDK fields only. Availability/identity are checked inside the native
// callback; no Actor/Attribute/AttributeInstance reference escapes it.
struct SaturationObservation {
    bool present{},canonicalIdentity{},syncable{};
    std::uint32_t attributeId{};
    float minimum{},maximum{},defaultValue{},current{};
};
static_assert(std::is_trivially_copyable_v<SaturationObservation>);
inline bool validSaturationObservation(SaturationObservation const& s) noexcept {
    return s.present&&s.canonicalIdentity&&s.syncable&&std::isfinite(s.minimum)
        &&std::isfinite(s.maximum)&&std::isfinite(s.defaultValue)&&std::isfinite(s.current)
        &&s.minimum>=0&&s.maximum<=20&&s.maximum>s.minimum
        &&s.defaultValue>=s.minimum&&s.defaultValue<=s.maximum
        &&s.current>=s.minimum&&s.current<=s.maximum;
}
// A getter has no receipt marker. Never accept an unchanged initial value as
// proof of synchronization. A later observed change permits CLIENT state only;
// it may include prediction and is not server-observed/authoritative provenance.
class ClientSaturationTracker {
    bool initialized{},changed{};
    std::uint64_t generation{},observedMs{};
    int dimension{};
    SaturationObservation previous{};
public:
    void reset() noexcept {*this={};}
    FoodValue observe(SaturationObservation const& s,std::uint64_t nextGeneration,
        int nextDimension,std::uint64_t now) noexcept {
        if(!validSaturationObservation(s)){reset();return {};}
        bool boundary=!initialized||generation!=nextGeneration||dimension!=nextDimension
            ||now<observedMs||now-observedMs>1000||previous.attributeId!=s.attributeId
            ||previous.minimum!=s.minimum||previous.maximum!=s.maximum||previous.defaultValue!=s.defaultValue;
        if(boundary)changed=false;
        else if(previous.current!=s.current)changed=true;
        initialized=true;generation=nextGeneration;dimension=nextDimension;observedMs=now;previous=s;
        return changed?FoodValue{s.current,FoodSource::ClientAttribute}:FoodValue{};
    }
};
}
