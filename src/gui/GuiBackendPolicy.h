#pragma once

namespace praxis::companion {
enum class GuiBackendKind { WaitingForProviders, Standalone, LholoBridge, ConflictBlocked, Failed };
enum class LholoBridgeStatus { Absent, Compatible, Incompatible };

constexpr GuiBackendKind selectGuiBackend(bool lholoPresent, LholoBridgeStatus bridge) noexcept {
    if (!lholoPresent) return GuiBackendKind::Standalone;
    return bridge == LholoBridgeStatus::Compatible
        ? GuiBackendKind::LholoBridge : GuiBackendKind::ConflictBlocked;
}
constexpr GuiBackendKind finishGuiBackendStart(GuiBackendKind selected, bool started) noexcept {
    return started ? selected : GuiBackendKind::Failed;
}
}
