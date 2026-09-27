#pragma once
#include "gui/GuiBackendPolicy.h"
namespace praxis::companion {
void ensureGuiBackend() noexcept;
bool shutdownGuiBackend() noexcept;
bool guiInputCaptured() noexcept;
GuiBackendKind guiBackendKind() noexcept;
void drawPraxisContent(void* context) noexcept;
void drawChunkHudContent(void* context) noexcept;
bool chunkHudNeeded() noexcept;
void closePraxisGui() noexcept;

// The optional provider is resolved only by GuiBackendCoordinator.
bool lholoBridgeCompatible() noexcept;
bool startLholoBridge() noexcept;
bool stopLholoBridge() noexcept;
bool lholoBridgeInputCaptured() noexcept;
void closeLholoBridge() noexcept;
void logGuiInfo(char const* message) noexcept;
void logGuiError(char const* message) noexcept;
}
