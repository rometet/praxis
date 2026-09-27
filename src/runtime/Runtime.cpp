#include "runtime/Runtime.h"
#include "core/RuntimePolicy.h"
#include "platform/Native.h"
#include "render/Renderer.h"
#include "render/WorldOverlays.h"
#include "gui/GuiBridge.h"
#include "ll/api/Config.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/dimension/Dimension.h"
#include <Windows.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <cctype>

namespace praxis::companion {
namespace {
std::mutex serviceMutex;
RuntimeConfig config;
std::filesystem::path configPath;
ZoomState zoomState;
std::chrono::steady_clock::time_point previousZoomUpdate{};
std::chrono::steady_clock::time_point previousMouseUpdate{};
bool active{};
bool configDirty{};
std::chrono::steady_clock::time_point configChangedAt{};
LocalPlayer const* observedPlayer{};
Dimension const* observedDimension{};
const auto disabledSettings = std::make_shared<Settings const>();

bool flushConfigLocked() {
    if (!configDirty) return true;
    if (configPath.empty() || !ll::config::saveConfig(config, configPath)) return false;
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

// Called under serviceMutex from the render/tick callbacks; no game object is retained or dereferenced.
void serviceFrame() {
    if (!active) return;
    if (configDirty && std::chrono::steady_clock::now() - configChangedAt >= std::chrono::milliseconds(350))
        flushConfigLocked();
    auto next = runtimeSettings(config, availableRenderPasses.load(std::memory_order_acquire),
                                availableFeatures.load(std::memory_order_acquire));
    if (settingsChanged(*settings(), next)) {
        publishedSettings.store(std::make_shared<Settings const>(next), std::memory_order_release);
    }
    frameHudNeeded.store(next.on(Feature::ChunkBorder) && next.chunkBorder.showHud
                         && chunkHudPlayerSeen.load(std::memory_order_acquire), std::memory_order_release);
    bool allowed = inputAllowed.load(std::memory_order_acquire) && gameHasFocus() && !guiInputCaptured();
    bool held = allowed && (GetAsyncKeyState(config.zoomKey) & 0x8000) != 0;
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
    if (!content || content->empty()) return false;
    auto data = nlohmann::ordered_json::parse(*content, nullptr, true, true);
    if (!data.is_object() || !data.contains("version") || !data["version"].is_number_integer()) return false;
    int const sourceVersion = data["version"].get<int>();
    if (sourceVersion != 1 && sourceVersion != 2) return false;
    // Merge the existing document over v2 defaults. A v1 file retains its
    // NoRender, Fullbright, brightness, Zoom, key and multiplier values.
    auto merged = ll::reflection::serialize<nlohmann::ordered_json>(candidate).value();
    merged.merge_patch(data);
    if (sourceVersion == 1 && data.contains("zoomMultiplier") && data["zoomMultiplier"].is_number())
        merged["zoomOptions"]["targetFov"] = data["zoomMultiplier"].get<float>() * 70.0f;
    if (sourceVersion == 1 && data.contains("brightness") && data["brightness"].is_number())
        merged["brightness"] = std::clamp(data["brightness"].get<float>(), 1.0f, 25.0f);
    merged["version"] = 2;
    ll::reflection::deserialize(candidate, merged).value();
    if (!validRuntimeConfig(candidate)) return false;
    if (sourceVersion == 1) {
        auto backup = path;
        backup += ".v1.backup";
        if (!std::filesystem::exists(backup)) std::filesystem::copy_file(path, backup);
        if (!ll::config::saveConfig(candidate, path)) return false;
    }
    config = candidate;
    configPath = path;
    configDirty = false;
    return true;
}

RuntimeConfig runtimeConfigSnapshot() {
    std::lock_guard lock(serviceMutex);
    return config;
}

bool updateRuntimeConfig(RuntimeConfig const& next) {
    std::lock_guard lock(serviceMutex);
    if (!active || configPath.empty() || !validRuntimeConfig(next)) return false;
    config = next;
    configDirty = true;
    configChangedAt = std::chrono::steady_clock::now();
    return true;
}

bool flushRuntimeConfig() noexcept {
    std::lock_guard lock(serviceMutex);
    return flushConfigLocked();
}

std::vector<std::string> listRuntimeProfiles() {
    std::lock_guard lock(serviceMutex);
    std::vector<std::string> names;
    try {
        if (configPath.empty()) return names;
        auto const directory = configPath.parent_path() / "profiles";
        if (!std::filesystem::exists(directory)) return names;
        for (auto const& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                auto name = entry.path().stem().string();
                if (validProfileName(name)) names.push_back(std::move(name));
            }
        }
    } catch (...) { names.clear(); }
    std::sort(names.begin(), names.end());
    return names;
}

bool saveRuntimeProfile(std::string const& name) noexcept {
    try {
        std::lock_guard lock(serviceMutex);
        if (!active || configPath.empty() || !validProfileName(name)) return false;
        auto const path = profilePath(name);
        std::filesystem::create_directories(path.parent_path());
        return ll::config::saveConfig(config, path);
    } catch (...) { return false; }
}

bool loadRuntimeProfile(std::string const& name) noexcept {
    try {
        std::lock_guard lock(serviceMutex);
        if (!active || configPath.empty() || !validProfileName(name)) return false;
        auto content = ll::file_utils::readFile(profilePath(name));
        if (!content || content->empty()) return false;
        auto data = nlohmann::ordered_json::parse(*content);
        RuntimeConfig candidate;
        ll::reflection::deserialize(candidate, data).value();
        if (!validRuntimeConfig(candidate)) return false;
        config = std::move(candidate);
        configDirty = true;
        configChangedAt = std::chrono::steady_clock::now();
        return true;
    } catch (...) { return false; }
}

bool deleteRuntimeProfile(std::string const& name) noexcept {
    try {
        std::lock_guard lock(serviceMutex);
        return !configPath.empty() && validProfileName(name)
            && std::filesystem::remove(profilePath(name));
    } catch (...) { return false; }
}

void activateRuntime() {
    {
    std::lock_guard lock(serviceMutex);
    auto next = std::make_shared<Settings const>(runtimeSettings(config, availableRenderPasses.load(),
                                                                 availableFeatures.load()));
    publishedSettings.store(std::move(next), std::memory_order_release);
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    zoomHeld = false;
    zoomFactor = 1.0f;
    active = true;
    runtimeReady = true;
    }
    // Selection waits for the first UI frame, after initial mod enable.
}

void deactivateRuntime() noexcept {
    std::lock_guard lock(serviceMutex);
    flushConfigLocked();
    active = false;
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    resetFullbrightReadiness();
    clearWorldOverlays();
    publishedSettings.store(disabledSettings, std::memory_order_release);
    inputAllowed = false;
    runtimeReady = false;
    zoomHeld = false;
    zoomFactor = 1.0f;
    frameHudNeeded = false;
    Renderer::get().setGuiOpen(false);
}

void updateWorld(LocalPlayer& player) {
    std::lock_guard lock(serviceMutex);
    if (!active) return;
    auto const* dimension = &player.getDimension();
    if (observedPlayer != &player || observedDimension != dimension) {
        observedPlayer = &player;
        observedDimension = dimension;
        zoomState.reset();
        resetFullbrightReadiness();
        clearWorldOverlays();
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
    std::lock_guard lock(serviceMutex);
    observedPlayer = nullptr;
    observedDimension = nullptr;
    chunkHudPlayerSeen = false;
    frameHudNeeded = false;
    resetFullbrightReadiness();
    zoomState.reset();
    previousZoomUpdate = previousMouseUpdate = {};
    zoomHeld = false;
    zoomFactor = 1.0f;
    inputAllowed = false;
    clearWorldOverlays();
    availableFeatures.fetch_and(~((1u << static_cast<unsigned>(Feature::ChunkBorder))
        | (1u << static_cast<unsigned>(Feature::LightOverlay))
        | (1u << static_cast<unsigned>(Feature::RedstoneOverlay))
        | (1u << static_cast<unsigned>(Feature::ShulkerPreview))), std::memory_order_acq_rel);
    worldGeneration.fetch_add(1, std::memory_order_acq_rel);
}
} // namespace praxis::companion
