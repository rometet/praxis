#pragma once
#include "core/TabTpsProtocol.h"
#include <cstdint>
#include <optional>

namespace praxis::companion::tabtps {
struct LiveView {
    Status status{Status::Unavailable};
    double currentTps{};
    std::optional<double> currentMspt;
};
// Returns immutable values only; safe to call on the rendering thread.
LiveView liveView(std::uint64_t nowMs) noexcept;
// A new connection/world must not inherit a prior server's telemetry.
void resetLiveView() noexcept;
bool tabHudNeeded() noexcept;
void drawTabHud(void* imguiContext) noexcept;
} // namespace praxis::companion::tabtps
