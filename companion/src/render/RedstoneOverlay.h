#pragma once
class BaseActorRenderContext;
namespace praxis::companion {
void renderRedstoneOverlay(BaseActorRenderContext& context, bool alphaLayer);
void clearRedstoneOverlay() noexcept;
}
