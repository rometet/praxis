#pragma once
#include <filesystem>
class BaseActorRenderContext;
class IClientInstance;
class LocalPlayer;
namespace praxis::companion {
bool installNative();
bool beginNativeStop() noexcept;
bool removeNative() noexcept;
void captureClient(IClientInstance& client);
void resetFullbrightReadiness() noexcept;
void updateWorld(LocalPlayer& player);
void drawWorld(BaseActorRenderContext& context,bool alphaLayer);
bool installPreview();
void removePreview() noexcept;
bool installOverlay();
bool removeOverlay() noexcept;
void drawHud();
}
