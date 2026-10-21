#pragma once
class BaseActorRenderContext;
namespace praxis::companion {
void renderLightOverlay(BaseActorRenderContext& context, bool alphaLayer);
void clearLightOverlay() noexcept;
}
