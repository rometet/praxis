#include "core/RuntimePolicy.h"
#include "core/FullbrightPolicy.h"
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
using namespace praxis::companion;
static int checks{};
static void require(bool value, char const* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        require(supportedLoaderVersion({26, 51, 5, false, false}), "exact loader version accepted");
        constexpr std::array unsupported{
            LoaderVersionView{26, 51, 4, false, false},
            LoaderVersionView{26, 51, 6, false, false},
            LoaderVersionView{26, 50, 5, false, false},
            LoaderVersionView{27, 51, 5, false, false},
            LoaderVersionView{26, 51, 5, true, false},
            LoaderVersionView{26, 51, 5, false, true},
        };
        for (auto const& version : unsupported)
            require(!supportedLoaderVersion(version), "non-exact loader version rejected");

        RuntimeConfig config;
        require(validRuntimeConfig(config), "default config valid");
        auto defaults = runtimeSettings(config, kAllRenderPasses);
        for (bool enabled : defaults.enabled) require(!enabled, "every feature defaults OFF");
        require(defaults.renderMask == 0, "default render mask OFF");
        config.chunkBorder.enabled = true;
        require(!runtimeSettings(config, 0, 0).on(Feature::ChunkBorder), "chunk border waits for native backend");
        require(runtimeSettings(config, 0, 1u << static_cast<unsigned>(Feature::ChunkBorder))
                    .on(Feature::ChunkBorder), "chunk border follows confirmed backend");
        config.chunkBorder.enabled = false;
        auto changed = defaults;
        require(!settingsChanged(defaults, changed), "equal settings do not republish");
        changed.brightness = 12.0f;
        require(settingsChanged(defaults, changed), "brightness change publishes");
        changed = defaults;
        changed.enabled[static_cast<unsigned>(Feature::Fullbright)] = true;
        require(settingsChanged(defaults, changed), "fullbright change publishes");
        changed = defaults;
        changed.enabled[static_cast<unsigned>(Feature::Zoom)] = true;
        require(settingsChanged(defaults, changed), "zoom change publishes");
        changed = defaults;
        changed.keys[static_cast<unsigned>(Feature::Zoom)] = 'V';
        require(settingsChanged(defaults, changed), "module keybind change publishes");
        config.keybinds[static_cast<unsigned>(Feature::LightOverlay)] = 'L';
        auto withKeybind = runtimeSettings(config, kAllRenderPasses);
        require(withKeybind.keys[static_cast<unsigned>(Feature::LightOverlay)] == 'L',
                "module keybind is published to runtime settings");
        config.keybinds[static_cast<unsigned>(Feature::LightOverlay)] = 0;

        FullbrightReadiness readiness;
        int firstPlayer{};
        int secondPlayer{};
        readiness.observe(nullptr, false);
        require(!readiness.readyFor(&firstPlayer), "no player is not ready");
        readiness.observe(&firstPlayer, false);
        require(!readiness.readyFor(&firstPlayer), "loading player is not ready");
        auto checkGamma = [&](bool enabled, float brightness, float expected) {
            int originalCalls = 0;
            float const result = fullbrightGamma(
                [&] { ++originalCalls; return 0.75f; },
                [&]() -> std::optional<float> {
                    if (enabled && readiness.readyFor(&firstPlayer)) return brightness;
                    return std::nullopt;
                });
            require(originalCalls == 1, "original gamma called exactly once");
            require(result == expected, "gamma result follows readiness and enable state");
        };
        checkGamma(false, 25.0f, 0.75f);
        checkGamma(true, 25.0f, 0.75f);
        readiness.observe(&firstPlayer, true);
        checkGamma(true, 25.0f, 25.0f);
        readiness.observe(&firstPlayer, false);
        require(readiness.readyFor(&firstPlayer), "inventory retains gameplay readiness");
        checkGamma(false, 25.0f, 0.75f);
        checkGamma(true, 26.0f, 0.75f);
        readiness.observe(&secondPlayer, false);
        require(!readiness.readyFor(&secondPlayer), "new player begins unready");
        readiness.observe(nullptr, false);
        require(!readiness.readyFor(&firstPlayer), "world leave clears readiness");
        readiness.observe(&firstPlayer, true);
        readiness.reset();
        require(!readiness.readyFor(&firstPlayer), "disable resets readiness");

        config.noRender.enabled = true;
        config.noRender.passes.fill(true);
        config.fullbright = true;
        config.zoom = true;
        auto enabled = runtimeSettings(config, passBit(RenderPass::Fog));
        require(enabled.renderMask == passBit(RenderPass::Fog), "unavailable passes filtered");
        require(enabled.on(Feature::NoRender) && enabled.on(Feature::Fullbright) && enabled.on(Feature::Zoom), "native services enabled");
        config.brightness = 100.0f;
        require(!validRuntimeConfig(config), "v2 rejects brightness above legacy limit");
        config.brightness = 25.0f;
        require(validRuntimeConfig(config), "normalized v2 brightness accepted");
        for (auto feature : {Feature::ChunkBorder, Feature::ShulkerPreview, Feature::RedstoneOverlay, Feature::LightOverlay})
            require(!enabled.on(feature), "unimplemented feature never advertised");
        require(runtimeZoomFactor(config, true, true) == config.zoomMultiplier, "hold zoom");
        require(runtimeZoomFactor(config, false, true) == 1.0f, "screen/focus gate restores FOV");
        require(runtimeZoomFactor(config, true, false) == 1.0f, "release restores FOV");
        config.zoom = false;
        require(runtimeZoomFactor(config, true, true) == 1.0f, "disabled zoom restores FOV");
        for (float invalid : {0.0f, 101.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            auto bad = config; bad.brightness = invalid;
            require(!validRuntimeConfig(bad), "invalid brightness rejected");
        }
        for (float invalid : {0.0f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            auto bad = config; bad.zoomMultiplier = invalid;
            require(!validRuntimeConfig(bad), "invalid multiplier rejected");
        }
        for (int key : {-1, 256}) {
            auto bad = config; bad.zoomKey = key;
            require(!validRuntimeConfig(bad), "invalid key rejected");
        }
        {
            auto bad = config;
            bad.chunkBorder.lineBudget = 8193;
            require(!validRuntimeConfig(bad), "unbounded chunk line budget rejected");
        }
        {
            auto bad = config;
            bad.guiStyle.backgroundOpacity = std::numeric_limits<float>::quiet_NaN();
            require(!validRuntimeConfig(bad), "invalid GUI opacity rejected");
            bad = config;
            bad.guiStyle.roundingRadius = 33.0f;
            require(!validRuntimeConfig(bad), "excessive GUI rounding rejected");
        }
        config.version = 3;
        require(!validRuntimeConfig(config), "unknown config version rejected");
        std::cout << "Runtime policy checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
