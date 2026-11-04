#pragma once
#include <imgui.h>

namespace praxis::companion {

enum class LegacyUiFontFamily : int {
    CorporateLogo = 0,
    NotoSansInter = 1,
    IbmPlexSans = 2,
};

char const* legacyUiFontDisplayName(LegacyUiFontFamily family) noexcept;
bool configureLegacyUiFont(LegacyUiFontFamily family, float pixelSize = 18.0f) noexcept;

// Called by the shared GUI host only while its atlas is unlocked. This appends
// the embedded Praxis font without changing the host's default font or style.
void initializeSharedLegacyUiFont(void* context) noexcept;
bool sharedLegacyUiFontReady(void* context) noexcept;

// Use the accepted 18px Praxis font and the original ImGui spacing for one
// borrowed frame, then restore all host state before returning the callback.
class LegacyUiScope {
public:
    LegacyUiScope();
    ~LegacyUiScope();
    LegacyUiScope(LegacyUiScope const&) = delete;
    LegacyUiScope& operator=(LegacyUiScope const&) = delete;
private:
    ImGuiStyle savedStyle_;
    float savedFontGlobalScale_{};
};

} // namespace praxis::companion
