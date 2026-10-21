#include "gui/GuiBackendPolicy.h"
#include <iostream>
using namespace praxis::companion;
int main() {
    static_assert(selectGuiBackend(false, LholoBridgeStatus::Absent) == GuiBackendKind::Standalone);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Compatible) == GuiBackendKind::LholoBridge);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Incompatible) == GuiBackendKind::ConflictBlocked);
    static_assert(selectGuiBackend(true, LholoBridgeStatus::Absent) == GuiBackendKind::WaitingForProviders);
    unsigned checks{};
    unsigned failures{};
    auto check = [&](bool condition, char const* name) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
    };
    for (auto status : {LholoBridgeStatus::Absent, LholoBridgeStatus::Compatible, LholoBridgeStatus::Incompatible}) {
        check(selectGuiBackend(false, status) == GuiBackendKind::Standalone, "absent provider permits existing standalone");
        check(selectGuiBackend(true, status) != GuiBackendKind::Standalone, "present provider never permits a second host");
    }
    check(selectGuiBackend(true, LholoBridgeStatus::Absent) == GuiBackendKind::WaitingForProviders,
          "registered provider reserves ownership before enable");
    check(selectGuiBackend(true, LholoBridgeStatus::Incompatible) == GuiBackendKind::ConflictBlocked,
          "incompatible or v3-only provider blocks GUI");
    check(selectGuiBackend(true, LholoBridgeStatus::Compatible) == GuiBackendKind::LholoBridge,
          "v2-compatible provider selects shared context");
    check(finishGuiBackendStart(GuiBackendKind::Standalone, true) == GuiBackendKind::Standalone, "standalone start accepted");
    check(finishGuiBackendStart(GuiBackendKind::Standalone, false) == GuiBackendKind::Failed, "standalone start failure");
    check(finishGuiBackendStart(GuiBackendKind::LholoBridge, true) == GuiBackendKind::LholoBridge, "v2 registration accepted");
    check(finishGuiBackendStart(GuiBackendKind::LholoBridge, false) == GuiBackendKind::ConflictBlocked,
          "ABI/registration rejection never falls back");
    for (auto state : {GuiBackendKind::WaitingForProviders, GuiBackendKind::ConflictBlocked, GuiBackendKind::Failed}) {
        for (bool started : {false, true}) {
            check(finishGuiBackendStart(state, started) == state, "reserved/blocked/failed states cannot claim a host start");
        }
    }
    if (failures) return 1;
    std::cout << "GUI backend selection: " << checks << " checks PASS\n";
}
