#pragma once
#include "resourcepacks/PassiveReloadTrace.h"

namespace praxis::companion::packs {
extern PassiveReloadTrace& passiveReloadTrace;
// Optional diagnostics. OFF or any guard/hook failure preserves normal startup.
bool preparePassiveReloadTrace() noexcept;
void reportPassiveReloadRegistration(unsigned state, char const* reason) noexcept;
void* passiveReloadFlushTarget() noexcept;
bool passiveReloadFlushCaller(void*) noexcept;
void emitPassiveReloadTrace(unsigned limit=32) noexcept;
void stopPassiveReloadTrace(ReloadTraceEnd=ReloadTraceEnd::Stopped) noexcept;
}
