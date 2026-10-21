#include "runtime/Runtime.h"
#include "platform/EffectPatches.h"
#include "runtime/ConfigStorage.h"
#include "runtime/ConfigMigration.h"
#include "core/RuntimePolicy.h"
#include "platform/Native.h"
#include "platform/WeatherChanger.h"
#include "platform/CameraCandidate.h"
#include "platform/HandRestock.h"
#include "platform/InventoryRequest.h"
#include "platform/InventoryActions.h"
#include "render/Renderer.h"
#include "render/WorldOverlays.h"
#include "gui/GuiBridge.h"
#include "gui/HudTools.h"
#include "map/MapRuntime.h"
#include "resourcepacks/WorldPackSwitch.h"
#include "runtime/InformationRuntime.h"
#include "ll/api/Config.h"
#include "runtime/ConfigCodec.h"
#include "runtime/ProfileDocument.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/dimension/Dimension.h"
#include <Windows.h>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <cctype>
#include <fstream>

namespace praxis::companion {
namespace {
std::mutex serviceMutex;
RuntimeConfig config;
std::filesystem::path configPath;
std::string configSourceDocument;
ProfileEditHistory configEdits;
ZoomState zoomState;
std::chrono::steady_clock::time_point previousZoomUpdate{};
std::chrono::steady_clock::time_point previousMouseUpdate{};
bool active{};
bool previousMapKeyDown{};
std::array<bool, map::mapActionCount> previousMapActionKeyDown{};
bool configDirty{};
std::uint64_t configRevision{};
std::chrono::steady_clock::time_point configChangedAt{};
LocalPlayer const* observedPlayer{};
Dimension const* observedDimension{};
std::array<bool, featureCount> previousModuleKeyDown{};
const auto disabledSettings = std::make_shared<Settings const>();

bool flushConfigLocked() {
    if (!configDirty) return true;
    if (configPath.empty() || !queueConfigSave(config, configPath,configSourceDocument,configEdits)) return false;
    configDirty = false;
    return true;
}

bool validProfileName(std::string const& name) {
    if (name.empty() || name.size() > 48) return false;
    for (unsigned char ch : name)
        if (!(std::isalnum(ch) || ch == '-' || ch == '_')) return false;
    return true;
}

std::filesystem::path profilePath(std::string const& name) {
    return configPath.parent_path() / "profiles" / (name + ".json");
}

bool gameHasFocus() noexcept {
    DWORD process{};
    auto window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}

bool toggleFeatureRequest(Feature feature) {
    bool* value{};
    switch (feature) {
    case Feature::NoRender: value = &config.noRender.enabled; break;
    case Feature::ChunkBorder: value = &config.chunkBorder.enabled; break;
    case Feature::Fullbright: value = &config.fullbright; break;
    case Feature::ShulkerPreview: value = &config.shulkerPreview; break;
    case Feature::RedstoneOverlay: value = &config.redstoneOverlay; break;
    case Feature::LightOverlay: value = &config.lightOverlay; break;
    case Feature::Zoom: value = &config.zoom; break;
    case Feature::TimeChanger: value = &config.timeChanger; break;
    case Feature::WeatherChanger: value = &config.weatherChanger; break;
    case Feature::ToolProtection: value = &config.toolProtection; break;
    case Feature::WeaponSwitch: value = &config.weaponSwitch; break;
    case Feature::HandRestock: value = &config.handRestock; break;
    case Feature::InventorySort: value = &config.inventorySort; break;
    case Feature::InventoryTransfer: value = &config.inventoryTransfer; break;
    case Feature::Hitbox: value = &config.hitboxOptions.enabled; break;
    case Feature::InventoryHUD: value = &config.inventoryHud; break;
    case Feature::AntiEffect: value = &config.antiEffect; break;
    default: return false;
    }
    *value = !*value;
    for(unsigned i=0;i<featureProfileFields.size();++i) {
        auto const& owner=featureProfileFields[i];
        if(owner.slot==static_cast<unsigned>(feature)
            &&(feature==Feature::Hitbox||!std::string_view(owner.field).ends_with("Options")))
            configEdits.fields|=featureRegistryBit(i);
    }
    ++configRevision;
    configDirty = true;
    configChangedAt = std::chrono::steady_clock::now();
    char message[160]{};
    sprintf_s(message, "Keybind toggled %s: %s",
              featureNames[static_cast<unsigned>(feature)], *value ? "ON" : "OFF");
    logGuiInfo(message);
    return true;
}

unsigned currentModifiers() noexcept {
 return modifierBits((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,
  (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,(GetAsyncKeyState(VK_MENU)&0x8000)!=0);
}
void pollModuleKeybinds(bool dispatch) {
    auto const modifiers=currentModifiers();
    bool const mapDown = config.mapToggleKey > 0 && (GetAsyncKeyState(config.mapToggleKey) & 0x8000) != 0;
    if (dispatch && mapDown && !previousMapKeyDown && modifiersMatch(config.mapToggleModifiers,modifiers)) toggleMapEnabled();
    previousMapKeyDown = mapDown;
    for (unsigned i = 0; i < featureCount; ++i) {
        // Only implemented slots can dispatch; reserved future slots persist.
        if (!(ownedFeatureSlotMask & (1u<<i))) {previousModuleKeyDown[i]=false;continue;}
        // Camera's formal event ledger exclusively owns session activation.
        if(i==static_cast<unsigned>(Feature::Freelook)||i==static_cast<unsigned>(Feature::FreeCamera)
            ||i==static_cast<unsigned>(Feature::OffhandSwap))continue;
        int const key = config.keybinds[i];
        bool const down = key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
        if (dispatch && down && !previousModuleKeyDown[i]
            && modifiersMatch(config.keybindModifiers[i],modifiers))
            toggleFeatureRequest(static_cast<Feature>(i));
        previousModuleKeyDown[i] = down;
    }
}

void applyStorageCompletionLocked() {
    if (auto loaded=takeLoadedProfile(); loaded && loaded->requestRevision==configRevision) {
        config=std::move(loaded->config);
        configSourceDocument=std::move(loaded->sourceDocument);
        configEdits={};
        configureInformationRuntime(config.informationOptions);
        ++configRevision;
        configDirty=true;
        configChangedAt=std::chrono::steady_clock::now();
    }
}

void pollMapKeybinds(bool dispatch) {
    auto const keys = mapActionBindings();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        int const key = keys[i];
        bool const down = key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
        // Preserve the legacy enable binding without applying a second action
        // when a newer binding uses the same key.
        if (dispatch && down && !previousMapActionKeyDown[i] && key != config.mapToggleKey)
            (void)dispatchMapAction(static_cast<map::MapAction>(i));
        previousMapActionKeyDown[i] = down;
    }
}

// Called under serviceMutex from the render/tick callbacks; no game object is retained or dereferenced.
void serviceFrame() {
    if (!active) return;
    applyStorageCompletionLocked();
    bool const allowed = inputAllowed.load(std::memory_order_acquire)
        && gameHasFocus() && !guiInputCaptured();
    // Match legacy ModuleManager::pollKeybinds(): update edge state even while
    // dispatch is blocked, but only toggle modules while gameplay input is free.
    pollModuleKeybinds(allowed);
    pollMapKeybinds(allowed);
    if (configDirty && std::chrono::steady_clock::now() - configChangedAt >= std::chrono::milliseconds(350))
        flushConfigLocked();
    auto next = runtimeSettings(config, availableRenderPasses.load(std::memory_order_acquire),
                                availableFeatures.load(std::memory_order_acquire));
    if (settingsChanged(*settings(), next)) {
        publishedSettings.store(std::make_shared<Settings const>(next), std::memory_order_release);
    }
    pollWeatherChangerDiagnostics(config.weatherChanger);
    frameHudNeeded.store(next.on(Feature::ChunkBorder) && next.chunkBorder.showHud
                         && chunkHudPlayerSeen.load(std::memory_order_acquire), std::memory_order_release);
    bool held = allowed && config.zoomKey>0 && (GetAsyncKeyState(config.zoomKey) & 0x8000) != 0
        && modifiersMatch(config.zoomModifiers,currentModifiers());
    auto const now = std::chrono::steady_clock::now();
    float const seconds = previousZoomUpdate.time_since_epoch().count() == 0 ? 1.0f / 60.0f
        : std::chrono::duration<float>(now - previousZoomUpdate).count();
    previousZoomUpdate = now;
    float const factor = zoomState.update(config.zoom, allowed, held, config.zoomOptions, seconds);
    zoomHeld.store(zoomState.engaged(), std::memory_order_release);
    zoomFactor.store(factor, std::memory_order_release);
}
} // namespace

bool loadRuntimeConfig(std::filesystem::path const& path) {
    std::lock_guard lock(serviceMutex);
    if (active) return false;
    RuntimeConfig candidate;
    std::filesystem::create_directories(path.parent_path());
    if (!std::filesystem::exists(path) && !ll::config::saveConfig(candidate, path)) return false;
    auto content = ll::file_utils::readFile(path);
    if (!content || content->empty() || content->size()>1024*1024) return false;
    auto data = nlohmann::ordered_json::parse(*content, nullptr, true, true);
    if (!decodeRuntimeConfig(data,candidate)) return false;
    int const sourceVersion = data["version"].get<int>();
    if (sourceVersion == 1) {
        auto backup = path;
        backup += ".v1.backup";
        if (!std::filesystem::exists(backup)) std::filesystem::copy_file(path, backup);
        auto persisted=nlohmann::ordered_json::parse(*content,nullptr,true,true);
        overlayOwnedConfig(persisted,ll::reflection::serialize<nlohmann::ordered_json>(candidate).value());
        auto saved=persisted.dump(2);if(saved.size()>1024*1024)return false;
        auto temporary=path;temporary+=".v1.pending-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        if(std::filesystem::exists(temporary))return false;
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<saved;output.close();
        if(!output || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
    }
    config = candidate;
    configSourceDocument=*content;
    configEdits={};
    configPath = path;
    configureInformationRuntime(config.informationOptions);
    if (!startConfigStorage(std::filesystem::absolute(path))) return false;
    // The v1 migration uses the same owned atomic writer as later edits.
    configDirty = sourceVersion == 1;
    configChangedAt = std::chrono::steady_clock::now();
    return true;
}

RuntimeConfig runtimeConfigSnapshot() {
    std::lock_guard lock(serviceMutex);
    if (active) applyStorageCompletionLocked();
    return config;
}

RuntimeConfigView runtimeConfigView() {
    std::lock_guard lock(serviceMutex);
    if(active)applyStorageCompletionLocked();
    return {config,configRevision};
}

bool updateRuntimeConfigIfRevision(RuntimeConfig const& next,std::uint64_t expectedRevision,std::uint32_t bindingEdits) {
    std::lock_guard lock(serviceMutex);
    if(!active||configPath.empty()||configRevision!=expectedRevision||!validRuntimeConfig(next))return false;
    recordProfileEdits(configEdits,ll::reflection::serialize<ConfigJson>(config).value(),
        ll::reflection::serialize<ConfigJson>(next).value());
    // Explicit Clear/Reset may leave a value unchanged. Claim these slots only
    // when the same GUI draft is accepted; rejected drafts own no history.
    configEdits.keys|=bindingEdits&ownedFeatureSlotMask;
    configEdits.modifiers|=bindingEdits&ownedFeatureSlotMask;
    config=next;configureInformationRuntime(config.informationOptions);
    ++configRevision;configDirty=true;configChangedAt=std::chrono::steady_clock::now();
    return true;
}

bool updateRuntimeConfig(RuntimeConfig const& next) {
    std::lock_guard lock(serviceMutex);
    if (!active || configPath.empty() || !validRuntimeConfig(next)) return false;
    recordProfileEdits(configEdits,ll::reflection::serialize<ConfigJson>(config).value(),
        ll::reflection::serialize<ConfigJson>(next).value());
    config = next;
    configureInformationRuntime(config.informationOptions);
    ++configRevision;
    configDirty = true;
    configChangedAt = std::chrono::steady_clock::now();
    return true;
}

bool flushRuntimeConfig() noexcept {
    std::lock_guard lock(serviceMutex);
    return flushConfigLocked();
}

std::vector<std::string> listRuntimeProfiles() { return cachedProfileNames(); }
bool saveRuntimeProfile(std::string const& name) noexcept {
    try { std::lock_guard lock(serviceMutex);
        return active && !configPath.empty() && validProfileName(name) && queueConfigSave(config, profilePath(name),configSourceDocument,configEdits);
    } catch (...) { return false; }
}
bool loadRuntimeProfile(std::string const& name) noexcept {
    try { std::lock_guard lock(serviceMutex);
        if (!active || configPath.empty() || !validProfileName(name)) return false;
        auto nextRevision=configRevision+1;
        if (!queueProfileLoad(profilePath(name), nextRevision)) return false;
        configRevision=nextRevision;
        return true;
    } catch (...) { return false; }
}
bool deleteRuntimeProfile(std::string const& name) noexcept {
    try { std::lock_guard lock(serviceMutex);
        return active && !configPath.empty() && validProfileName(name) && queueProfileDelete(profilePath(name));
    } catch (...) { return false; }
}

void activateRuntime() {
    {
    std::lock_guard lock(serviceMutex);
    if (!startConfigStorage(std::filesystem::absolute(configPath))
        || !initializeMap(std::filesystem::absolute(configPath.parent_path()/"map")))
        throw std::runtime_error("Failed to start owned storage services");
    auto next = std::make_shared<Settings const>(runtimeSettings(config, availableRenderPasses.load(),
                                                                 availableFeatures.load()));
    publishedSettings.store(std::move(next), std::memory_order_release);
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    previousModuleKeyDown.fill(false);
    previousMapKeyDown = false;
    previousMapActionKeyDown.fill(false);
    zoomHeld = false;
    zoomFactor = 1.0f;
    configureInformationRuntime(config.informationOptions);
    active = true;
    runtimeReady = true;
    }
    setMapActionDispatcherAvailable(true);
    // Selection waits for the first UI frame, after initial mod enable.
}

void deactivateRuntime() noexcept {
    requestCameraCandidateReset();
    cancelUnifiedHotkeyCapture();
    std::lock_guard lock(serviceMutex);
    flushConfigLocked();
    active = false;
    runtimeReady=false;
    expireWeatherChangerDiagnostics();
    configureInformationRuntime(InformationOptions{});
    retireMap();
    resetInformationRuntime();
    worldGeneration.fetch_add(1);
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    previousModuleKeyDown.fill(false);
    previousMapKeyDown = false;
    previousMapActionKeyDown.fill(false);
    resetFullbrightReadiness();
    clearWorldOverlays();
    publishedSettings.store(disabledSettings, std::memory_order_release);
    inputAllowed = false;
    runtimeReady = false;
    zoomHeld = false;
    zoomFactor = 1.0f;
    frameHudNeeded = false;
    Renderer::get().setGuiOpen(false);
    setMapActionDispatcherAvailable(false);
}

void updateWorld(LocalPlayer& player) {
    std::lock_guard lock(serviceMutex);
    if (!active) return;
    auto const* dimension = &player.getDimension();
    if (observedPlayer != &player || observedDimension != dimension) {
        packs::invalidateWorldPackOperation();
        requestCameraCandidateReset();
        cancelUnifiedHotkeyCapture();
        observedPlayer = &player;
        observedDimension = dimension;
        zoomState.reset();
        resetFullbrightReadiness();
        if(!restoreEffectPatches())logGuiError("World/dimension effect restoration failed");
        clearWorldOverlays();
        retireMap();
        resetInformationRuntime();
        worldGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    auto const position = player.getPosition();
    int const x = blockFloor(position.x), z = blockFloor(position.z);
    chunkHudX.store(chunkCoordinate(x), std::memory_order_relaxed);
    chunkHudZ.store(chunkCoordinate(z), std::memory_order_relaxed);
    chunkHudInX.store(positiveMod(x, 16), std::memory_order_relaxed);
    chunkHudInZ.store(positiveMod(z, 16), std::memory_order_relaxed);
    chunkHudY.store(blockFloor(position.y), std::memory_order_relaxed);
    chunkHudPlayerSeen.store(true, std::memory_order_release);
    updateMap(player, worldGeneration.load(std::memory_order_acquire));
    serviceFrame();
}

void drawWorld(BaseActorRenderContext& context, bool alphaLayer) {
    if (!alphaLayer) {
        std::lock_guard lock(serviceMutex);
        serviceFrame();
    }
    renderWorldOverlays(context, alphaLayer);
}

bool handleZoomScroll(int direction) {
    std::lock_guard lock(serviceMutex);
    if (!active || !inputAllowed.load(std::memory_order_acquire) || !gameHasFocus()
        || guiInputCaptured()) return false;
    return zoomState.scroll(direction, config.zoomOptions);
}

void adjustZoomMouse(short& dx, short& dy) {
    std::lock_guard lock(serviceMutex);
    if (!active || !inputAllowed.load(std::memory_order_acquire) || !gameHasFocus()
        || guiInputCaptured()) return;
    auto const now = std::chrono::steady_clock::now();
    float const seconds = previousMouseUpdate.time_since_epoch().count() == 0 ? 1.0f / 60.0f
        : std::chrono::duration<float>(now - previousMouseUpdate).count();
    previousMouseUpdate = now;
    zoomState.adjustMouse(dx, dy, config.zoomOptions, seconds);
}

void resetWorldRuntime() noexcept {
    packs::invalidateWorldPackOperation();
    requestCameraCandidateReset();
    cancelUnifiedHotkeyCapture();
    std::lock_guard lock(serviceMutex);
    retireMap();
    resetInformationRuntime();
    requestHandRestockReset();
    resetInventoryActions();
    inventory::resetRequests();
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    frameHudNeeded = false;
    resetFullbrightReadiness();
    if(!restoreEffectPatches())logGuiError("World exit effect restoration failed");
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    previousModuleKeyDown.fill(false);
    previousMapKeyDown = false;
    previousMapActionKeyDown.fill(false);
    zoomHeld = false;
    zoomFactor = 1.0f;
    inputAllowed = false;
    clearWorldOverlays();
    availableFeatures.fetch_and(~((1u << static_cast<unsigned>(Feature::ChunkBorder))
        | (1u << static_cast<unsigned>(Feature::LightOverlay))
        | (1u << static_cast<unsigned>(Feature::RedstoneOverlay))
        | (1u << static_cast<unsigned>(Feature::ShulkerPreview))
        | featureRegistryBit(unsigned(Feature::InventoryHUD))), std::memory_order_acq_rel);
    worldGeneration.fetch_add(1, std::memory_order_acq_rel);
}
} // namespace praxis::companion
