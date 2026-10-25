#pragma once

class BaseActorRenderContext;
class LocalPlayer;

namespace praxis::companion {
bool installFlexiblePlacement();
bool removeFlexiblePlacement() noexcept;
void tickFlexiblePlacement(LocalPlayer& player) noexcept;
void resetFlexiblePlacement() noexcept;
void renderFlexiblePlacementOverlay(BaseActorRenderContext& context, bool alphaLayer) noexcept;
bool flexiblePlacementModifiersHeld() noexcept;
}
