#pragma once
class BaseActorRenderContext;
namespace praxis::companion {
void renderWorldOverlays(BaseActorRenderContext& context, bool alphaLayer);
void clearWorldOverlays() noexcept;
}
