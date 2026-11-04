// SPDX-License-Identifier: GPL-3.0-only
// Derived from ChiyanMap, sunjing2 and contributors.
// https://github.com/sunjing2/ChiyanMap
// Pinned commit: 1785278a865e6dc93d38cea76ccbffaaf64f6659
// Modified 2026-10-02 for Praxis: value-only types, bounded native adapters,
// no upstream renderer/hooks/global world caches. See PROVENANCE.json.
// Original comments/colour data are retained. Full GPL text: LICENSE.
#pragma once
#include <string>
#include <array>
namespace praxis::companion::map::chiyan {
struct Color {
    float r{},g{},b{},a{};
    constexpr Color()=default;
    constexpr Color(float r,float g,float b,float a=1):r(r),g(g),b(b),a(a){}
    bool operator==(Color const&) const = default;
};
struct Tints { Color grass,foliage,water; };
void getBiomeTints(std::string const&,Color&,Color&,Color&);
Tints biomeTints(std::string const&);
Color getBlockColor(std::string const&,Color,Color,Color,bool* recognized=nullptr);
bool IsInvisibleOrTechnicalOverlay(std::string const&);
bool IsPillarLogOrStem(std::string const&);
std::string GetHorizontalWoodName(std::string const&);
bool IsBiomeTintedBlock(std::string const&) noexcept;
Color BlendWaterOverFloor(Color,Color);
void GetRingCoords(int,int,int&,int&) noexcept;
Tints blendTints(std::array<Tints,4> const&,float,float) noexcept;
Color shadeTerrain(Color,float,float,float) noexcept;
}
