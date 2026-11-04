#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace praxis::companion {
enum class RenderPass : std::uint8_t {
    Entities, Items, BlockEntities, Hud, Particles, Sky, Weather,
    PortalOverlay, FireOverlay, Lightning, Fog, Count
};
inline constexpr std::size_t kRenderPassCount = static_cast<std::size_t>(RenderPass::Count);
inline constexpr std::array<std::string_view, kRenderPassCount> kRenderPassKeys{
    "entities", "items", "blockEntities", "hud", "particles", "sky", "weather",
    "portalOverlay", "fireOverlay", "lightning", "fog"
};
inline constexpr std::array<std::string_view, kRenderPassCount> kRenderPassLabels{
    "Entities", "Dropped items", "Block entities", "HUD", "Particles", "Sky",
    "Weather", "Portal overlay", "Fire overlay", "Lightning", "Fog"
};
using RenderMask = std::uint16_t;
inline constexpr RenderMask kAllRenderPasses = (RenderMask{1} << kRenderPassCount) - 1;
constexpr RenderMask passBit(RenderPass pass) noexcept {
    const auto index = static_cast<unsigned>(pass);
    return index < kRenderPassCount ? static_cast<RenderMask>(RenderMask{1} << index) : RenderMask{0};
}
struct NoRenderSettings {
    bool enabled{};
    std::array<bool, kRenderPassCount> passes{};
    constexpr RenderMask requestedMask() const noexcept {
        RenderMask mask{};
        if (enabled) {
            for (std::size_t i = 0; i < passes.size(); ++i) {
                if (passes[i]) mask |= static_cast<RenderMask>(RenderMask{1} << i);
            }
        }
        return mask;
    }
};
enum class RenderScope : std::uint8_t { VanillaWorld, VanillaHud, Inventory, ModOverlay };
enum class ActorVisual : std::uint8_t { Other, DroppedItem, LightningBolt };
constexpr bool suppress(RenderMask active, RenderPass pass, RenderScope scope) noexcept {
    if (scope == RenderScope::Inventory || scope == RenderScope::ModOverlay) return false;
    if (pass == RenderPass::Hud && scope != RenderScope::VanillaHud) return false;
    if (pass != RenderPass::Hud && scope != RenderScope::VanillaWorld) return false;
    return (active & passBit(pass)) != 0;
}
constexpr bool suppressActor(RenderMask active, ActorVisual actor, RenderScope scope) noexcept {
    const auto pass = actor == ActorVisual::DroppedItem ? RenderPass::Items
        : actor == ActorVisual::LightningBolt ? RenderPass::Lightning : RenderPass::Entities;
    return suppress(active, pass, scope);
}
constexpr int chunkCoordinate(int blockCoordinate) noexcept {
    return blockCoordinate / 16 - (blockCoordinate % 16 < 0 ? 1 : 0);
}
class GuiKeyLatch {
public:
    static constexpr unsigned key = 'L';
    bool down(unsigned pressedKey, bool inputAvailable, bool modified) noexcept {
        if (pressedKey != key) return false;
        if (held_) return consumed_;
        held_ = true;
        consumed_ = inputAvailable && !modified;
        toggle_ = consumed_;
        return consumed_;
    }
    bool up(unsigned releasedKey) noexcept {
        if (releasedKey != key) return false;
        const bool consumed = consumed_;
        held_ = false;
        consumed_ = false;
        return consumed;
    }
    bool takeToggle() noexcept {
        const bool result = toggle_;
        toggle_ = false;
        return result;
    }
    void reset() noexcept { held_ = consumed_ = toggle_ = false; }
private:
    bool held_{};
    bool consumed_{};
    bool toggle_{};
};
}
