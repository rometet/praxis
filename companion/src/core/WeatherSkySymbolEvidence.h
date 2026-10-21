#pragma once
#include <array>
#include <cstdint>

namespace praxis::companion {
struct WeatherSkySymbol { char const* name; char const* decorated; };
// Exact export names emitted by the SDK26.51.5 compiler/prelink probe. These
// are identifiers, not function addresses or guessed signatures/offsets.
inline constexpr std::array<WeatherSkySymbol, 9> weatherSkySymbols{{
    {"skyDarken", "?getSkyDarken@Dimension@@QEBAMM@Z"},
    {"ambient", "?$getAmbientBrightness@LevelRendererCamera@@QEBAMXZ"},
    {"lightData", "?$createBaseLightTextureData@BaseLightTextureImageBuilder@@QEAA?AV?$unique_ptr@VBaseLightData@@U?$default_delete@VBaseLightData@@@std@@@std@@PEAVIClientInstance@@AEBVBaseLightData@@@Z"},
    {"atmosphere", "?_updateBiomeBlendingAndAtmosphericScattering@MinecraftGame@@QEAAXMMUDimensionType@@@Z"},
    {"view", "?createViewRenderObject@LevelRendererPlayer@@QEAA?AUViewRenderObject@@AEAVScreenContext@@W4SubClientId@@@Z"},
    {"preRender", "?$preRenderUpdate@LevelRendererPlayer@@QEAAXAEAVScreenContext@@AEAULevelRenderPreRenderUpdateParameters@@@Z"},
    {"fog", "?$setupFog@LevelRendererPlayer@@QEAAXAEAVScreenContext@@M@Z"},
    {"rainProxy", "?$getRainLevel@WeatherManagerProxy@@QEBAMXZ"},
    {"lightningProxy", "?$getLightningLevel@WeatherManagerProxy@@QEBAMXZ"},
}};
enum class WeatherSkyAddressStatus : unsigned { Missing, OutsideMainImage, MainImage };
struct WeatherSkyAddressResult { WeatherSkyAddressStatus status; std::uintptr_t rva{}; };
inline WeatherSkyAddressResult classifyWeatherSkyAddress(std::uintptr_t address,
    std::uintptr_t base, std::uintptr_t size) noexcept {
    if (!address) return {WeatherSkyAddressStatus::Missing};
    // Subtraction avoids overflow in base+size. No external-module address is
    // represented as a game RVA or retained for a later native call.
    if (!base || address < base || address - base >= size)
        return {WeatherSkyAddressStatus::OutsideMainImage};
    return {WeatherSkyAddressStatus::MainImage, address - base};
}
} // namespace praxis::companion
