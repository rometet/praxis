#include "gui/GuiBackendPolicy.h"
#include <iostream>
using namespace praxis::companion;
int main() {
    static_assert(selectGuiBackend(false, LholoBridgeStatus::Absent) == GuiBackendKind::Standalone);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Compatible) == GuiBackendKind::LholoBridge);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Incompatible) == GuiBackendKind::ConflictBlocked);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Absent) == GuiBackendKind::ConflictBlocked);
    static_assert(finishGuiBackendStart(GuiBackendKind::Standalone, false) == GuiBackendKind::Failed);
    static_assert(finishGuiBackendStart(GuiBackendKind::LholoBridge, false) == GuiBackendKind::Failed);
    std::cout << "GUI backend selection: 6 checks PASS\n";
}
