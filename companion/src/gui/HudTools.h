#pragma once
#include "core/RuntimePolicy.h"
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
#include "api/HudControlApi.h"
#include <vector>
#endif
namespace praxis::companion {
void drawHudLayoutEditor(RuntimeConfig&,bool&);
void drawUnifiedHotkeys(RuntimeConfig&,bool&,std::uint32_t& bindingEdits);
void cancelUnifiedHotkeyCapture() noexcept;
#ifdef PRAXIS_HUD_TOOLS_HEADLESS
struct UnifiedHotkeyUiProbe {
    int group{}, id{}, category{};
    float assignX{}, assignY{}, clearX{}, clearY{}, resetX{}, resetY{};
    bool visible{}, compact{}, withinClip{true};
    float nameBottom{}, contextTop{}, contextBottom{}, conflictTop{-1.f};
};
extern std::array<bool,256> unifiedHotkeyTestKeys;
extern bool unifiedHotkeyTestOwner;
extern HudControlApiV1 unifiedHotkeyTestApi;
extern std::vector<UnifiedHotkeyUiProbe> unifiedHotkeyUiProbes;
extern std::array<float,4> unifiedHotkeyHeaderProbe;
extern unsigned hudLayoutTestTargetCount;
bool unifiedHotkeyTestCapturing() noexcept;
#endif
}
