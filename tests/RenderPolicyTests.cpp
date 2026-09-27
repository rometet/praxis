#include "core/RenderPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace praxis::companion;
static int checks{};
static void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        static_assert(kRenderPassCount == 11);
        NoRenderSettings settings;
        require(settings.requestedMask() == 0, "disabled default");
        settings.passes.fill(true);
        require(settings.requestedMask() == 0, "master gate");
        settings.enabled = true;
        require(settings.requestedMask() == kAllRenderPasses, "all eleven preserved");
        for (std::size_t i = 0; i < kRenderPassCount; ++i) {
            settings.passes.fill(false);
            settings.passes[i] = true;
            auto mask = settings.requestedMask();
            auto pass = static_cast<RenderPass>(i);
            require(mask == passBit(pass), "independent flag");
            for (std::size_t j = 0; j < kRenderPassCount; ++j) {
                auto candidate = static_cast<RenderPass>(j);
                auto scope = candidate == RenderPass::Hud ? RenderScope::VanillaHud : RenderScope::VanillaWorld;
                require(suppress(mask, candidate, scope) == (i == j), "no cross-pass suppression");
                require(!suppress(kAllRenderPasses, candidate, RenderScope::Inventory), "inventory preserved");
                require(!suppress(kAllRenderPasses, candidate, RenderScope::ModOverlay), "mod overlay preserved");
            }
        }
        require(!suppress(kAllRenderPasses, RenderPass::Count, RenderScope::VanillaWorld), "invalid pass");
        require(!suppressActor(passBit(RenderPass::Entities), ActorVisual::DroppedItem, RenderScope::VanillaWorld), "item control independent");
        require(!suppressActor(passBit(RenderPass::Entities), ActorVisual::LightningBolt, RenderScope::VanillaWorld), "lightning control independent");
        require(suppressActor(passBit(RenderPass::Items), ActorVisual::DroppedItem, RenderScope::VanillaWorld), "item gate");
        require(suppressActor(passBit(RenderPass::Lightning), ActorVisual::LightningBolt, RenderScope::VanillaWorld), "lightning gate");
        for (int value = -128; value <= 128; ++value) {
            const int chunk = chunkCoordinate(value);
            require(chunk * 16 <= value && value < (chunk + 1) * 16, "signed chunk coordinate");
        }
        require(chunkCoordinate(std::numeric_limits<int>::min()) == -134217728, "negative integer boundary");
        require(chunkCoordinate(std::numeric_limits<int>::max()) == 134217727, "positive integer boundary");
        GuiKeyLatch key;
        require(key.down('L', true, false) && key.takeToggle(), "L opens GUI");
        require(key.down('L', true, false) && !key.takeToggle(), "repeat does not retoggle");
        require(key.up('L'), "consumed release");
        require(!key.down('L', false, false) && !key.takeToggle(), "typing passes through");
        require(!key.down('L', true, false) && !key.takeToggle(), "held typing key does not become shortcut");
        require(!key.up('L'), "typing release passes through");
        require(!key.down('L', true, true) && !key.takeToggle(), "modified L passes through");
        key.reset();
        require(!key.down('H', true, false) && !key.takeToggle(), "other mod shortcut untouched");
        require(key.down('L', true, false) && key.takeToggle(), "focus reset");
        settings.enabled = false;
        require(settings.requestedMask() == 0, "disable restores vanilla mask");
        std::cout << "Render policy checks: " << checks << " PASS\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
