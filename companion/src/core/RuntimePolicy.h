#pragma once

#include "core/Settings.h"
#include "core/InventoryHudPolicy.h"
#include "core/AntiEffectPolicy.h"
#include "core/InformationPolicy.h"
#include "core/HotkeyPolicy.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace praxis::companion {

struct LoaderVersionView {
    std::uint16_t major{};
    std::uint16_t minor{};
    std::uint16_t patch{};
    bool preRelease{};
    bool buildMetadata{};

    constexpr bool operator==(LoaderVersionView const&) const noexcept = default;
};

inline constexpr LoaderVersionView kSupportedLoaderVersion{26, 51, 5, false, false};

inline constexpr bool supportedLoaderVersion(LoaderVersionView const& version) noexcept {
    return version == kSupportedLoaderVersion;
}

struct GuiStyleOptions {
    float widthRatio = 0.78f;
    float heightRatio = 0.78f;
    std::uint32_t accent = 0xFFED5C6E;
    std::uint32_t accentOutline = 0xFF7A333D;
    std::uint32_t background = 0xFA160F0E;
    float backgroundOpacity = 0.94f;
    float roundingRadius = 10.0f;
    float roundingSmoothness = 5.0f;
    float outlineSize = 1.0f;
    int fontFamily = 0;

    bool operator==(GuiStyleOptions const&) const = default;
};

struct RuntimeConfig {
    int version = 2;
    unsigned featureSlotSchema = praxis::companion::featureSlotSchema;
    NoRenderSettings noRender{};
    bool fullbright = false;
    float brightness = 25.0f;
    bool zoom = false;
    // Per-module toggle bindings. 0 means unbound. Zoom's hold/action key is
    // intentionally separate so enabling/disabling the module does not fight
    // the zoom action itself.
    std::vector<int> keybinds=std::vector<int>(featureCount,0);
    std::vector<unsigned> keybindModifiers=std::vector<unsigned>(featureCount,anyModifiers);
    unsigned zoomModifiers{anyModifiers},mapToggleModifiers{anyModifiers};
    int zoomKey = 'C';
    float zoomMultiplier = 30.0f / 70.0f;
    ZoomOptions zoomOptions{};
    ChunkBorderOptions chunkBorder{};
    LightOverlayOptions lightOptions{};
    RedstoneOverlayOptions redstoneOptions{};
    ShulkerPreviewOptions shulkerOptions{};
    GuiStyleOptions guiStyle{};
    InformationOptions informationOptions{};
    int mapToggleKey = 'M';
    std::uint32_t favoriteModules{};
    bool shulkerPreview{};
    bool redstoneOverlay{};
    bool lightOverlay{};
    bool timeChanger{};
    bool weatherChanger{};
    WeatherOptions weatherOptions{};
    HitboxOptions hitboxOptions{};
    bool toolProtection{};
    ToolProtectionOptions toolProtectionOptions{};
    bool weaponSwitch{};
    WeaponSwitchOptions weaponSwitchOptions{};
    bool handRestock{};
    HandRestockOptions handRestockOptions{};
    bool inventorySort{};
    InventorySortOptions inventorySortOptions{};
    bool inventoryTransfer{};
    bool freelook{},freeCamera{};
    bool offhandSwap{};
    CameraOptions freelookOptions{},freeCameraOptions{};
    InventoryTransferOptions inventoryTransferOptions{};
    bool inventoryHud{};
    InventoryHudOptions inventoryHudOptions{};
    bool antiEffect{};
    AntiEffectOptions antiEffectOptions{};
};

inline bool validRuntimeConfig(RuntimeConfig const& config) noexcept {
    return config.version == 2
        && config.featureSlotSchema == featureSlotSchema
        && config.keybinds.size()>=featureCount && config.keybinds.size()<=featureRegistryBits
        && config.keybindModifiers.size()==config.keybinds.size()
        && validInventoryHudOptions(config.inventoryHudOptions)
        && validHitboxOptions(config.hitboxOptions)
        && validCameraOptions(config.freelookOptions) && validCameraOptions(config.freeCameraOptions)
        && validWeatherOptions(config.weatherOptions)
        && validModifiers(config.zoomModifiers) && validModifiers(config.mapToggleModifiers)
        && std::all_of(config.keybindModifiers.begin(),config.keybindModifiers.end(),validModifiers)
        && validInformationOptions(config.informationOptions)
        && validToolProtectionOptions(config.toolProtectionOptions)
        && validWeaponSwitchOptions(config.weaponSwitchOptions)
        && validHandRestockOptions(config.handRestockOptions)
        && validInventorySortOptions(config.inventorySortOptions)
        && validInventoryTransferOptions(config.inventoryTransferOptions)
        && config.mapToggleKey >= 0 && config.mapToggleKey <= 255
        && config.mapToggleKey != 0x79 && config.mapToggleKey != 0x7A
        && std::isfinite(config.brightness) && config.brightness >= 1.0f && config.brightness <= 25.0f
        && std::all_of(config.keybinds.begin(), config.keybinds.end(), [](int key) { return key >= 0 && key <= 255; })
        && config.zoomKey >= 0 && config.zoomKey <= 255
        && std::isfinite(config.zoomMultiplier) && config.zoomMultiplier >= 0.05f && config.zoomMultiplier <= 1.0f
        && validZoomOptions(config.zoomOptions)
        && config.chunkBorder.renderDistance >= 1 && config.chunkBorder.renderDistance <= 12
        && config.chunkBorder.heightAbove >= 4 && config.chunkBorder.heightAbove <= 128
        && config.chunkBorder.heightBelow >= 2 && config.chunkBorder.heightBelow <= 96
        && std::isfinite(config.chunkBorder.lineWidth)
        && config.chunkBorder.lineWidth >= 0.5f && config.chunkBorder.lineWidth <= 5.0f
        && config.chunkBorder.lineBudget >= 256 && config.chunkBorder.lineBudget <= 8192
        && config.lightOptions.range >= 1 && config.lightOptions.range <= 16
        && config.lightOptions.spawnThreshold >= 0 && config.lightOptions.spawnThreshold <= 15
        && config.lightOptions.scanBudget >= 1 && config.lightOptions.scanBudget <= 64
        && config.lightOptions.drawBudget >= 16 && config.lightOptions.drawBudget <= 2048
        && config.redstoneOptions.range >= 1 && config.redstoneOptions.range <= 16
        && config.redstoneOptions.scanBudget >= 1 && config.redstoneOptions.scanBudget <= 64
        && config.redstoneOptions.drawBudget >= 16 && config.redstoneOptions.drawBudget <= 2048
        && std::isfinite(config.guiStyle.widthRatio)
        && config.guiStyle.widthRatio >= 0.35f && config.guiStyle.widthRatio <= 1.0f
        && std::isfinite(config.guiStyle.heightRatio)
        && config.guiStyle.heightRatio >= 0.35f && config.guiStyle.heightRatio <= 1.0f
        && std::isfinite(config.guiStyle.backgroundOpacity)
        && config.guiStyle.backgroundOpacity >= 0.0f && config.guiStyle.backgroundOpacity <= 1.0f
        && std::isfinite(config.guiStyle.roundingRadius)
        && config.guiStyle.roundingRadius >= 0.0f && config.guiStyle.roundingRadius <= 32.0f
        && std::isfinite(config.guiStyle.roundingSmoothness)
        && config.guiStyle.roundingSmoothness >= 0.0f && config.guiStyle.roundingSmoothness <= 16.0f
        && std::isfinite(config.guiStyle.outlineSize)
        && config.guiStyle.outlineSize >= 0.0f && config.guiStyle.outlineSize <= 6.0f
        && config.guiStyle.fontFamily >= 0 && config.guiStyle.fontFamily <= 2
        // All 32 persisted favorite bits are representable; only owned bits have UI actions.
        && validShulkerPreviewOptions(config.shulkerOptions);
}

inline Settings runtimeSettings(RuntimeConfig const& config, RenderMask available,
                                unsigned availableFeatureMask = 0) noexcept {
    Settings result;
    result.enabled[static_cast<unsigned>(Feature::NoRender)] = config.noRender.enabled;
    result.enabled[static_cast<unsigned>(Feature::Fullbright)] = config.fullbright;
    result.enabled[static_cast<unsigned>(Feature::Zoom)] = config.zoom;
    result.enabled[static_cast<unsigned>(Feature::TimeChanger)] = config.timeChanger
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::TimeChanger))) != 0;
    result.enabled[static_cast<unsigned>(Feature::WeatherChanger)] = config.weatherChanger
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::WeatherChanger))) != 0;
    result.weatherOptions = config.weatherOptions;
    result.renderMask = (config.noRender.requestedMask()|antiEffectMask(config.antiEffect,config.antiEffectOptions)) & available & kAllRenderPasses;
    // The v1 migration normalizes brightness before publishing v2 settings.
    result.brightness = std::clamp(config.brightness, 1.0f, 25.0f);
    std::copy_n(config.keybinds.begin(),featureCount,result.keys.begin());
    std::copy_n(config.keybindModifiers.begin(),featureCount,result.keyModifiers.begin());
    result.toolProtection = config.toolProtectionOptions;
    result.weaponSwitch = config.weaponSwitchOptions;
    result.handRestock = config.handRestockOptions;
    result.inventorySort = config.inventorySortOptions;
    result.inventoryTransfer = config.inventoryTransferOptions;
    result.enabled[static_cast<unsigned>(Feature::InventoryTransfer)] = config.inventoryTransfer
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::InventoryTransfer))) != 0;
    result.enabled[static_cast<unsigned>(Feature::InventorySort)] = config.inventorySort
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::InventorySort))) != 0;
    result.enabled[static_cast<unsigned>(Feature::HandRestock)] = config.handRestock
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::HandRestock))) != 0;
    result.enabled[static_cast<unsigned>(Feature::OffhandSwap)] = config.offhandSwap
        && (availableFeatureMask & featureRegistryBit(static_cast<unsigned>(Feature::OffhandSwap))) != 0;
    result.enabled[static_cast<unsigned>(Feature::WeaponSwitch)] = config.weaponSwitch
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::WeaponSwitch))) != 0;
    result.enabled[static_cast<unsigned>(Feature::ToolProtection)] = config.toolProtection
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::ToolProtection))) != 0;
    result.freelookOptions=config.freelookOptions;result.freeCameraOptions=config.freeCameraOptions;
    result.zoomActionKey=config.zoomKey;result.zoomActionModifiers=config.zoomModifiers;
    result.mapToggleActionKey=config.mapToggleKey;result.mapToggleActionModifiers=config.mapToggleModifiers;
    result.enabled[static_cast<unsigned>(Feature::Freelook)]=config.freelook
        && (availableFeatureMask&featureRegistryBit(static_cast<unsigned>(Feature::Freelook)));
    result.enabled[static_cast<unsigned>(Feature::FreeCamera)]=config.freeCamera
        && (availableFeatureMask&featureRegistryBit(static_cast<unsigned>(Feature::FreeCamera)));
    // Slot23 is retired even when an old config enables it. Keep its stored data.
    result.enabled[unsigned(Feature::InventoryHUD)]=false;
    result.enabled[unsigned(Feature::AntiEffect)]=config.antiEffect&&(available&antiEffectPasses);
    result.hitboxOptions=config.hitboxOptions;
    result.enabled[static_cast<unsigned>(Feature::Hitbox)]=config.hitboxOptions.enabled;
    result.zoomOptions = config.zoomOptions;
    result.chunkBorder = config.chunkBorder;
    result.lightOptions = config.lightOptions;
    result.redstoneOptions = config.redstoneOptions;
    result.shulkerOptions = config.shulkerOptions;
    result.enabled[static_cast<unsigned>(Feature::ChunkBorder)] = config.chunkBorder.enabled
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::ChunkBorder))) != 0;
    result.enabled[static_cast<unsigned>(Feature::ShulkerPreview)] = config.shulkerPreview
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::ShulkerPreview))) != 0;
    result.enabled[static_cast<unsigned>(Feature::RedstoneOverlay)] = config.redstoneOverlay
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::RedstoneOverlay))) != 0;
    result.enabled[static_cast<unsigned>(Feature::LightOverlay)] = config.lightOverlay
        && (availableFeatureMask & (1u << static_cast<unsigned>(Feature::LightOverlay))) != 0;
    return result;
}

inline float runtimeZoomFactor(RuntimeConfig const& config, bool allowed, bool keyDown) noexcept {
    return config.zoom && allowed && (!config.zoomOptions.hold || keyDown)
        ? config.zoomOptions.targetFov / 70.0f : 1.0f;
}

inline bool settingsChanged(Settings const& current, Settings const& next) noexcept {
    return current != next;
}

} // namespace praxis::companion
