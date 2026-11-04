#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace praxis::companion {
// Legacy final-preview contract: renderGuiItemNew's foil flag does not disable
// the nested GuiItemPopTransform. Leave every ordinary inventory call intact.
inline bool previewAllowsAnimation(bool injecting, bool requested) noexcept {
    return !injecting && requested;
}
// Verified 26.51 SDK order: context,item,frame,x,y,foil,transparency,
// lightMultiplier,scale,zOrder. 060cfbb preserves item colour with light=1;
// the 1.21 FrameCapture route's light=0 is not compatible with this path.
// foil=false does NOT disable the nested InChunk animation; that is guarded
// separately by previewAllowsAnimation at the actual lower hook.
template<class Renderer,class Context,class Item>
void renderPreviewNativeIcon(Renderer& renderer,Context& context,Item& item,float x,float y,float scale) {
    renderer.renderGuiItemNew(context,item,0,x,y,false,1.0f,1.0f,scale,17);
}
inline bool shulkerIdentity(std::string_view name) noexcept {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name == "shulker_box" || (name.size() > 12 && name.ends_with("_shulker_box"));
}
struct ShulkerSlotView {
    bool present{};
    std::uint8_t count{};
    bool operator==(ShulkerSlotView const&) const = default;
};
using ShulkerSlotViews = std::array<ShulkerSlotView, 27>;
struct ShulkerPreviewOptions {
    bool showInHotbar{true};
    bool showSlotNumbers{};
    bool showItemCount{true};
    float previewScale{1.0f};
    // Legacy Praxis default: {0.15, 0.10, 0.20, 1.0} packed as AABBGGRR.
    std::uint32_t backgroundColor{0xFF331A26};
    bool operator==(ShulkerPreviewOptions const&) const = default;
};
inline bool hotbarPreviewActive(std::uint64_t elapsedMs) noexcept {
    return elapsedMs < 2200;
}
inline bool validShulkerPreviewOptions(ShulkerPreviewOptions const& options) noexcept {
    return std::isfinite(options.previewScale)
        && options.previewScale >= 0.75f && options.previewScale <= 2.0f;
}
inline bool acceptShulkerSlot(int slot, int count) noexcept {
    return slot >= 0 && slot < 27 && count > 0 && count <= 127;
}
inline bool storeShulkerSlot(ShulkerSlotViews& slots, int slot, int count) noexcept {
    if (!acceptShulkerSlot(slot, count) || slots[slot].present) return false;
    slots[slot] = {true, static_cast<std::uint8_t>(count)};
    return true;
}
} // namespace praxis::companion
