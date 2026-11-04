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
std::atomic<ULONGLONG> nextProviderCheck{};
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
    auto current = backend.load(std::memory_order_acquire);
    if (current == GuiBackendKind::ConflictBlocked || current == GuiBackendKind::Failed) return;
    auto const now = GetTickCount64();
    if (now < nextProviderCheck.load(std::memory_order_acquire)) return;
    std::lock_guard lock(coordinatorMutex);
    if (!runtimeReady.load(std::memory_order_acquire)) return;
    current = backend.load(std::memory_order_acquire);
    if (current == GuiBackendKind::ConflictBlocked || current == GuiBackendKind::Failed
        || now < nextProviderCheck.load(std::memory_order_acquire)) return;
    nextProviderCheck.store(now + 500, std::memory_order_release);
    try {
        // Called by AfterUIRenderEvent, outside our Present and GUI callbacks.
        // Retirement uses the existing stop paths; no live context migration.
        auto provider = ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
        bool const present = provider != nullptr;
        if (current == GuiBackendKind::Standalone) {
            if (!present) return;
            bool const stopped = standalone.stop();
            backend = stopped ? GuiBackendKind::ConflictBlocked : GuiBackendKind::Failed;
            logGuiError(stopped
                ? "LHolo appeared after standalone selection; standalone retired. Restart Minecraft to select the shared host."
                : "Standalone retirement failed after LHolo appeared; restart Minecraft before continuing.");
            return;
        }
        if (current == GuiBackendKind::LholoBridge) {
            if (present && provider->isEnabled() && lholoBridgeRegistered()) return;
            bool const stopped = lholo.stop();
            backend = stopped ? GuiBackendKind::ConflictBlocked : GuiBackendKind::Failed;
            logGuiError(stopped
                ? "LHolo provider stopped; shared GUI retired. Restart Minecraft before selecting another host."
                : "LHolo callback retirement failed; no replacement host will be started.");
            return;
        }
        // Presence alone reserves the chain throughout provider startup.
        if (present && !provider->isEnabled()) return;
        logGuiInfo("GUI backend selection started");
        logGuiInfo(present ? "LHolo provider: PRESENT" : "LHolo provider: ABSENT");
        bool const compatible = present && lholoBridgeCompatible();
        if (!present) logGuiInfo("LHolo bridge: NOT_AVAILABLE");
        else if (!compatible) logGuiInfo("LHolo bridge: INCOMPATIBLE");
        auto const selected = selectGuiBackend(present, compatible
            ? LholoBridgeStatus::Compatible : LholoBridgeStatus::Incompatible);

        if (selected == GuiBackendKind::ConflictBlocked) {
            backend = selected;
            logGuiError("Praxis GUI blocked: LHolo requires a compatible shared-context v2 bridge; no standalone hooks installed.");
            return;
        }

        bool started{};
        if (selected == GuiBackendKind::LholoBridge) {
            started = lholo.start();
            if (!started) {
                bool const stopped = lholo.stop();
                backend = stopped ? GuiBackendKind::ConflictBlocked : GuiBackendKind::Failed;
                logGuiError(stopped
                    ? "LHolo v2 registration/ABI rejected; Praxis GUI blocked without standalone fallback."
                    : "LHolo v2 registration cleanup failed; no replacement host will be started.");
                return;
            }
        } else if (selected == GuiBackendKind::Standalone) {
            started = standalone.start();
        }

        backend = finishGuiBackendStart(selected, started);
        if (started) {
            if (selected == GuiBackendKind::LholoBridge)
                logGuiInfo("LHolo bridge: COMPATIBLE");
            logGuiInfo(selected == GuiBackendKind::LholoBridge
                ? "GUI backend selected: LHOLO_BRIDGE"
                : "GUI backend selected: STANDALONE");
        } else {
            logGuiError("Standalone GUI initialization failed; native utilities remain active.");
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
    backend = standaloneStopped && bridgeStopped
        ? GuiBackendKind::WaitingForProviders : GuiBackendKind::Failed;
    nextProviderCheck = 0;
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
