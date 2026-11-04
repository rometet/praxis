#pragma once
#include <imgui.h>

namespace praxis::companion {
// Keep the existing disabled-text color/font while making long explanations
// reachable inside the existing scrollable detail area at compact viewports.
inline void disabledTextWrapped(char const* text) {
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}
}
