#pragma once
#include <array>
#include <cstdint>
#include "core/FeatureRegistryPolicy.h"

namespace praxis::companion {
// Persisted slot ABI. Count is storage extent, never the number of GUI cards.
enum class Feature : unsigned {
 NoRender=0, ChunkBorder=1, Fullbright=2, ShulkerPreview=3,
 RedstoneOverlay=4, LightOverlay=5, Zoom=6, TimeChanger=7,
 WeatherChanger=8, Hitbox=9, ToolProtection=10, WeaponSwitch=11,
 HandRestock=12, InventorySort=13, InventoryTransfer=14,
 AutoTool=15, EffectTimer=16, AngleSnap=17, ToggleSneakSprint=18, DebugScreen=19,
 Freelook=20, FreeCamera=21,
 Resource=22, InventoryHUD=23, // Retired; retain the persisted slot, never reuse it.
 AntiEffect=24, Inspector=25, OffhandSwap=26, FlexibleBlockPlacement=27, AccurateBlockPlacement=28, FastRightClick=29, FastLeftClick=30, Count=31
};
inline constexpr unsigned featureCount=static_cast<unsigned>(Feature::Count);
inline constexpr unsigned featureSlotSchema=1;
inline constexpr std::uint32_t featureSlotMask=featureRegistryMask(featureCount);
static_assert(featureCount<=32);
inline constexpr std::array<char const*,featureCount> featureNames{
 "NoRender","ChunkBorders","Fullbright","ShulkerPreview","RedstoneOverlay",
 "LightOverlay","Zoom","TimeChanger","WeatherChanger","Hitbox",
 "ToolProtection","WeaponSwitch","HandRestock","InventorySort","InventoryTransfer",
 "AutoTool","EffectTimer","AngleSnap","ToggleSneakSprint","DebugScreen","Freelook","FreeCamera",
 "Reserved22","Reserved23","AntiEffect","Reserved25","OffhandSwap","FlexibleBlockPlacement","AccurateBlockPlacement","FastRightClick","FastLeftClick"};
// The first eight favorite bits were GUI card positions, not Feature IDs.
// Their meaning is retained; appended cards use the persisted Feature slot.
inline constexpr unsigned favoriteSlotForCard(unsigned card, Feature feature) noexcept {
 return card<8 ? card : static_cast<unsigned>(feature);
}
}
