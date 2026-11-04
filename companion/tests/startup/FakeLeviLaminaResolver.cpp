#include "HomeStartupFixtureState.h"
#include <cstring>
#include <string>

// A normal nonvirtual member retains the SDK's x64 member-return ABI: this and
// std::string hidden return storage have the same calling convention.
class ScreenNameHost {
public:
    __declspec(noinline) std::string getScreenName() const {
        ++homeStartupState.screenCalls;
        return fixtureScreenName;
    }
};

namespace ll::memory {
void* resolveSymbol(char const* symbol) {
    if (homeStartupState.attached) ++homeStartupState.lateResolves;
    else ++homeStartupState.preloadResolves;
    if (std::strcmp(symbol, screenNameSymbol) != 0) {
        ++homeStartupState.unknownSymbols;
        return nullptr;
    }
    auto member = &ScreenNameHost::getScreenName;
    static_assert(sizeof(member) == sizeof(void*));
    void* pointer{};
    std::memcpy(&pointer, &member, sizeof(pointer));
    return pointer;
}
}
