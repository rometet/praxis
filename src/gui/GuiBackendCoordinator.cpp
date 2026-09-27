#include "gui/GuiBridge.h"
#include "gui/StandaloneGuiHost.h"
#include "gui/LholoBridgeGuiHost.h"
#include "render/Renderer.h"
#include "runtime/Runtime.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include <mutex>
#include <atomic>

namespace praxis::companion {
namespace {
std::mutex coordinatorMutex;
std::atomic<GuiBackendKind> backend{GuiBackendKind::WaitingForProviders};
StandaloneGuiHost standalone;
LholoBridgeGuiHost lholo;

void logMessage(char const* message, bool error) noexcept {
    try {
        auto mod = ll::mod::ModManagerRegistry::getInstance().getMod("PraxisCompanion");
        if (mod) {
            if (error) mod->getLogger().error("{}", message);
            else mod->getLogger().info("{}", message);
        }
    } catch (...) {}
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}
}
void logGuiInfo(char const* message) noexcept { logMessage(message, false); }
void logGuiError(char const* message) noexcept { logMessage(message, true); }

GuiBackendKind guiBackendKind() noexcept { return backend; }

void ensureGuiBackend() noexcept {
    if (!runtimeReady.load(std::memory_order_acquire)) return;
    if (backend == GuiBackendKind::Standalone) {
        try {
            if (ll::mod::ModManagerRegistry::getInstance().getMod("LHolo")) {
                std::lock_guard lock(coordinatorMutex);
                if (backend == GuiBackendKind::Standalone) {
                    bool const stopped = standalone.stop();
                    backend = stopped ? GuiBackendKind::ConflictBlocked : GuiBackendKind::Failed;
                    logGuiError("LHolo appeared after standalone GUI selection; standalone hooks removed. Restart Minecraft to select the bridge.");
                }
            }
        } catch (...) { logGuiError("GUI provider recheck failed"); }
        return;
    }
    if (backend != GuiBackendKind::WaitingForProviders) return;
    std::lock_guard lock(coordinatorMutex);
    if (backend != GuiBackendKind::WaitingForProviders) return;
    logGuiInfo("GUI backend selection started");
    try {
        // This is reached from the first rendered UI frame, after initial mod enable.
        auto provider = ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
        bool const present = provider != nullptr;
        logGuiInfo(present ? "LHolo provider: PRESENT" : "LHolo provider: ABSENT");
        bool const compatible = present && lholoBridgeCompatible();
        if (!present) logGuiInfo("LHolo bridge: NOT_AVAILABLE");
        else if (!compatible) logGuiInfo("LHolo bridge: INCOMPATIBLE");
        auto selected = selectGuiBackend(present, compatible
            ? LholoBridgeStatus::Compatible : LholoBridgeStatus::Incompatible);
        if (selected == GuiBackendKind::ConflictBlocked) {
            backend = selected;
            logGuiError("PraxisCompanion GUI standalone backend not installed: LHolo overlay detected without compatible bridge.");
            logGuiInfo("GUI backend selected: CONFLICT_BLOCKED");
            return;
        }
        bool const started = selected == GuiBackendKind::LholoBridge
            ? lholo.start() : standalone.start();
        backend = finishGuiBackendStart(selected, started);
        if (started) {
            if (selected == GuiBackendKind::LholoBridge) logGuiInfo("LHolo bridge: COMPATIBLE");
            logGuiInfo(selected == GuiBackendKind::LholoBridge
                ? "GUI backend selected: LHOLO_BRIDGE" : "GUI backend selected: STANDALONE");
        } else {
            if (selected == GuiBackendKind::LholoBridge) logGuiError("LHolo bridge: INCOMPATIBLE");
            logGuiError(selected == GuiBackendKind::LholoBridge
                ? "LHolo bridge registration failed; GUI disabled for this session. Native utilities remain active."
                : "Standalone GUI initialization failed; native utilities remain active.");
        }
    } catch (...) {
        backend = GuiBackendKind::Failed;
        logGuiError("GUI backend selection failed with an exception.");
    }
}

bool shutdownGuiBackend() noexcept {
    std::lock_guard lock(coordinatorMutex);
    bool const standaloneStopped = standalone.stop();
    bool const bridgeStopped = lholo.stop();
    backend = GuiBackendKind::WaitingForProviders;
    return standaloneStopped && bridgeStopped;
}

void closePraxisGui() noexcept {
    if (backend == GuiBackendKind::Standalone) standalone.setVisible(false);
    else if (backend == GuiBackendKind::LholoBridge) lholo.setVisible(false);
}

bool guiInputCaptured() noexcept {
    if (backend == GuiBackendKind::Standalone) return standalone.inputCaptured();
    if (backend == GuiBackendKind::LholoBridge) return lholo.inputCaptured();
    return false;
}
}
