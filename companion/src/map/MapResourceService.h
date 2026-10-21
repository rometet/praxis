#pragma once
#include "resourcepacks/WorldPackParticipant.h"

namespace praxis::companion::map {
// CPU-only Map retirement, serialized by MapRuntime's mutex. The caller is
// an existing current-client UI/player callback, never participant Query.
// A paused simulation must not be required to acknowledge its own retirement.
template<class Retire,class RegionReady>
void serviceMapResourceOwner(packs::WorldPackParticipant& participant,
    std::uint64_t world,std::uintptr_t client,std::uintptr_t renderer,
    Retire&& retire,RegionReady&& regionReady) {
    if(!participant.ownerMatches(world,client,renderer))return;
    auto epoch=participant.resourceEpoch();
    retire();
    bool ready=participant.state()==packs::WorldPackReloadPhase::Reacquiring&&regionReady();
    participant.ownerServiced(false,epoch,true,ready);
}
}
