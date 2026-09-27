#pragma once

namespace praxis::companion {

enum class LegacyUiFontFamily : int {
    CorporateLogo = 0,
    NotoSansInter = 1,
    IbmPlexSans = 2,
};

char const* legacyUiFontDisplayName(LegacyUiFontFamily family) noexcept;
bool configureLegacyUiFont(LegacyUiFontFamily family, float pixelSize = 18.0f) noexcept;

} // namespace praxis::companion
