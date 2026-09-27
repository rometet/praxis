#pragma once
#include "gui/GuiBridge.h"
#include "gui/GuiHost.h"
#include "render/Renderer.h"

namespace praxis::companion {
class LholoBridgeGuiHost final : public GuiHost {
public:
    bool start() noexcept override { return startLholoBridge(); }
    bool stop() noexcept override { return stopLholoBridge(); }
    bool visible() const noexcept override { return Renderer::get().isGuiOpen(); }
    void setVisible(bool value) noexcept override { if (!value) closeLholoBridge(); }
    bool inputCaptured() const noexcept override { return lholoBridgeInputCaptured(); }
    GuiBackendKind kind() const noexcept override { return GuiBackendKind::LholoBridge; }
};
}
