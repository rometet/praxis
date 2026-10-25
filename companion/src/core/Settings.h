#pragma once
#include "core/RenderPolicy.h"
#include "core/ChunkBorderGeometry.h"
#include "core/ZoomPolicy.h"
#include "core/LightOverlayPolicy.h"
#include "core/RedstoneOverlayPolicy.h"
#include "core/ShulkerPreviewPolicy.h"
#include "core/ToolProtectionPolicy.h"
#include "core/FeatureSlots.h"
#include "core/HotkeyPolicy.h"
#include "core/WeaponSwitchPolicy.h"
#include "core/HandRestockPolicy.h"
#include "core/InventorySortPolicy.h"
#include "core/InventoryTransferPolicy.h"
#include "core/PlayerHudPolicy.h"
#include "core/FeatureRegistryPolicy.h"
#include "core/CameraCandidatePolicy.h"
#include "core/WeatherOptions.h"
#include "core/FlexiblePlacementPolicy.h"
#include <array>
#include <atomic>
#include <memory>
namespace praxis::companion {
// Storage extent28; own0..14,20/21,24,26/27. Retired InventoryHUD23 and unknown slots stay opaque.
inline constexpr std::uint32_t ownedFeatureSlotMask=0x0D307FFFu;
struct Settings {
 HitboxOptions hitboxOptions{};
 RenderMask renderMask{};
 std::array<bool,featureCount> enabled{};
 std::array<int,featureCount> keys{};
 std::array<unsigned,featureCount> keyModifiers=[]{std::array<unsigned,featureCount> v;v.fill(anyModifiers);return v;}();
 int chunkRadius=3; int heightAbove=24; int heightBelow=8; bool regions=true;
 float brightness=25.0f;
 int scanRadius=8; int scanHeight=6; int scanBudget=512; bool skyLight=false;
 bool onlyDark=false; bool previewRequireShift=false; float previewScale=1.0f;
 float zoomFov=20.0f; float zoomSpeed=12.0f; bool zoomHold=true;
 bool zoomSensitivity=true;
 ChunkBorderOptions chunkBorder{};
 ZoomOptions zoomOptions{};
 LightOverlayOptions lightOptions{};
 RedstoneOverlayOptions redstoneOptions{};
 ShulkerPreviewOptions shulkerOptions{};
 ToolProtectionOptions toolProtection{};
 WeaponSwitchOptions weaponSwitch{};
 HandRestockOptions handRestock{};
 InventorySortOptions inventorySort{};
 InventoryTransferOptions inventoryTransfer{};
 FlexiblePlacementOptions flexiblePlacement{};
 CameraOptions freelookOptions{},freeCameraOptions{};
 WeatherOptions weatherOptions{};
 int zoomActionKey='C',mapToggleActionKey='M';
 unsigned zoomActionModifiers=anyModifiers,mapToggleActionModifiers=anyModifiers;
 bool on(Feature feature) const noexcept{return enabled[static_cast<unsigned>(feature)];}
 bool operator==(Settings const&) const noexcept = default;
};
inline std::atomic<std::shared_ptr<Settings const>> publishedSettings{std::make_shared<Settings>()};
inline std::shared_ptr<Settings const> settings() noexcept{return publishedSettings.load(std::memory_order_acquire);}
inline std::atomic<RenderMask> availableRenderPasses{};
inline std::atomic<unsigned> availableFeatures{};
inline std::atomic_bool inputAllowed{};
inline std::atomic_bool zoomHeld{};
inline std::atomic<float> zoomFactor{1.0f};
inline std::atomic_bool runtimeReady{};
inline std::atomic_bool frameHudNeeded{};
inline std::atomic_bool chunkHudPlayerSeen{};
inline std::atomic<int> chunkHudX{}, chunkHudZ{}, chunkHudInX{}, chunkHudInZ{}, chunkHudY{};
inline std::atomic<unsigned long long> worldGeneration{};
inline bool featureAvailable(Feature f) noexcept {return static_cast<unsigned>(f)<featureCount&&(availableFeatures.load(std::memory_order_acquire)&featureRegistryBit(static_cast<unsigned>(f)))!=0;}
}
