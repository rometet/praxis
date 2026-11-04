#pragma once
#include "gui/GuiBackendPolicy.h"

namespace praxis::companion {
class GuiHost {
public:
    virtual ~GuiHost() = default;
    virtual bool start() noexcept = 0;
    virtual bool stop() noexcept = 0;
    virtual bool visible() const noexcept = 0;
    virtual void setVisible(bool value) noexcept = 0;
    virtual bool inputCaptured() const noexcept = 0;
    virtual GuiBackendKind kind() const noexcept = 0;
};
}
