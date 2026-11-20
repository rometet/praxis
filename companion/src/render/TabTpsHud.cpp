#include "platform/TabTpsClient.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace praxis::companion::tabtps {
namespace {
bool foreground() noexcept {
    DWORD owner{};
    HWND window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &owner)
        && owner == GetCurrentProcessId();
}
}
bool tabHudNeeded() noexcept {
    return runtimeReady.load(std::memory_order_acquire)
        && inputAllowed.load(std::memory_order_acquire)
        && !guiInputCaptured()
        && foreground()
        && (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
}
void drawTabHud(void* imguiContext) noexcept {
    if (!imguiContext || !tabHudNeeded()) return;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguiContext));
    try {
        auto const dimensions = ImGui::GetIO().DisplaySize;
        if (!std::isfinite(dimensions.x) || !std::isfinite(dimensions.y)
            || dimensions.x <= 0 || dimensions.y <= 0) {
            ImGui::SetCurrentContext(previous);
            return;
        }
        auto const sample = liveView(GetTickCount64());
        ImGui::SetNextWindowPos({dimensions.x-14.0f, 16.0f},
                                ImGuiCond_Always, {1.0f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.67f);
        constexpr auto flags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("TabTPS##praxis_tabtps", nullptr, flags)) {
            if (sample.status == Status::Fresh) {
                ImGui::Text("Server TPS: %.1f", sample.currentTps);
                if (sample.currentMspt)
                    ImGui::Text("Server MSPT (sim): %.2f ms", *sample.currentMspt);
                else
                    ImGui::TextUnformatted("Server MSPT (sim): --");
            } else {
                ImGui::TextUnformatted("Server TPS: --");
                ImGui::TextUnformatted("Server MSPT (sim): --");
                ImGui::TextDisabled(sample.status == Status::Stale
                    ? "Server telemetry stale" : "Server companion unavailable");
            }
        }
        ImGui::End();
    } catch (...) { }
    ImGui::SetCurrentContext(previous);
}
} // namespace praxis::companion::tabtps
