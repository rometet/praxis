#pragma once

namespace praxis::companion {
enum class GuiBackendKind { WaitingForProviders, Standalone, LholoBridge, ConflictBlocked, Failed };
enum class LholoBridgeStatus { Absent, Compatible, Incompatible };

constexpr GuiBackendKind selectGuiBackend(bool lholoPresent, LholoBridgeStatus bridge) noexcept {
    if (!lholoPresent) return GuiBackendKind::Standalone;
    // A registered provider reserves graphics/input ownership even before
    // it enables. An incompatible provider never permits a second host.
    if (bridge == LholoBridgeStatus::Absent) return GuiBackendKind::WaitingForProviders;
    return bridge == LholoBridgeStatus::Compatible
        ? GuiBackendKind::LholoBridge : GuiBackendKind::ConflictBlocked;
}
constexpr GuiBackendKind finishGuiBackendStart(GuiBackendKind selected, bool started) noexcept {
    if (selected == GuiBackendKind::WaitingForProviders || selected == GuiBackendKind::ConflictBlocked
        || selected == GuiBackendKind::Failed) return selected;
    if (started) return selected;
    return selected == GuiBackendKind::LholoBridge
        ? GuiBackendKind::ConflictBlocked : GuiBackendKind::Failed;
}
}
