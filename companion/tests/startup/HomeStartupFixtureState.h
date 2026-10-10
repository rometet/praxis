#pragma once
// POD boundaries for an offline local DLL fixture. No Minecraft objects exist.
struct HomeStartupState {
    unsigned attached;
    unsigned preloadResolves;
    unsigned lateResolves;
    unsigned unknownSymbols;
    unsigned screenCalls;
    unsigned uiQueries;
    unsigned playerQueries;
    unsigned restockCalls;
};
extern HomeStartupState homeStartupState;
extern char const* fixtureScreenName;
inline constexpr char screenNameSymbol[] = "?getScreenName@ScreenView@@QEBA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";
