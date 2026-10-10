#include "HomeStartupFixtureState.h"
#include <Windows.h>
#include "mc/client/gui/screens/ScreenView.h"

HomeStartupState homeStartupState{};
char const* fixtureScreenName = "start_screen";

struct FixturePlayer {};
struct FixtureClient {
    FixturePlayer* getLocalPlayer() {
        ++homeStartupState.playerQueries;
        return nullptr;
    }
};
struct FixtureUi { FixtureClient mClient; };
namespace ll::event {
struct AfterUIRenderEvent {
    // The object is an opaque host-boundary token. Only the imported member's
    // ABI is exercised; the fake resolver never reads SDK object storage.
    ScreenView& screenView() { return *reinterpret_cast<ScreenView*>(&opaqueToken); }
    FixtureUi& uiRenderContext() { ++homeStartupState.uiQueries; return ui; }
    alignas(ScreenView) unsigned char opaqueToken[sizeof(ScreenView)]{};
    FixtureUi ui;
};
}
void tickHandRestock(FixturePlayer&) { ++homeStartupState.restockCalls; }

extern "C" __declspec(dllexport) HomeStartupState const* GetHomeStartupState() {
    return &homeStartupState;
}
extern "C" __declspec(dllexport) unsigned RunHomeStartupCallback(unsigned hud) {
    fixtureScreenName = hud ? "hud_screen" : "start_screen";
    ll::event::AfterUIRenderEvent event;
    // This include is the whole current product HandRestock UI listener lambda,
    // extracted with braces intact, not an independently implemented callback.
    auto callback =
#include "ActualHandRestockHomeCallback.inc"
    ;
    callback(event);
    return homeStartupState.screenCalls;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) homeStartupState.attached = 1;
    return TRUE;
}
