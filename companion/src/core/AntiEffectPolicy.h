#pragma once
#include "core/RenderPolicy.h"
namespace praxis::companion {
// Supported visual owners only. Darkness/Blindness/Nausea have no verified
// per-effect renderer contract here and deliberately have no working toggle.
inline constexpr RenderMask antiEffectPasses=passBit(RenderPass::FireOverlay)|passBit(RenderPass::PortalOverlay);
struct AntiEffectOptions {
    bool fire{true},portal{true};
    bool operator==(AntiEffectOptions const&) const = default;
};
inline constexpr RenderMask antiEffectMask(bool enabled,AntiEffectOptions const& o) noexcept {
    return enabled?((o.fire?passBit(RenderPass::FireOverlay):0)|(o.portal?passBit(RenderPass::PortalOverlay):0)):0;
}
inline constexpr RenderMask frameEffectMask(RenderMask requested,bool gameplay,bool ready) noexcept {
    return gameplay&&ready?requested&antiEffectPasses:0;
}
}
