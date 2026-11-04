#pragma once
#include "resourcepacks/PassiveReloadTrace.h"
#include <utility>
namespace praxis::companion::packs {
// Fixed callback adapter used by production and the real close/writer fixture.
// Sink never consults the trace: its journal mutex is held by close().
template<class Sink> void passiveReloadTerminalNotice(void* context,ReloadTraceEnd end) noexcept {
    static_cast<Sink*>(context)->notifyStop(static_cast<unsigned>(end));
}
// Preserve the actual UpdateHook order, including native exceptions skipping
// after. Its native boundary can be held in a fixture without calling Minecraft.
template<class Begin,class Native,class After>
void runPassiveReloadUpdate(Begin&& begin,Native&& native,After&& after){
    std::forward<Begin>(begin)();std::forward<Native>(native)();std::forward<After>(after)();
}
}
