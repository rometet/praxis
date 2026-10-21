#pragma once
// Restricted R24 test-only authorization. The intended native reload keeps
// all Level, Treatment and Base stacks intact and appends one registered user
// texture resource pack to Global in one disposable singleplayer world.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace praxis::companion::packs {
inline constexpr std::string_view R24CloneDirectory="PraxisLevelPackOfflineR17";
inline constexpr std::string_view R24CloneDisplayName="Praxis Level RP Offline Test R17";
struct R24WorldPermit {
    std::string_view directoryId;
    std::string_view displayName;
    bool multiplayer{}, globalEnabled{}, initializing{}, pending{};
    std::size_t levelPacks{};
};
constexpr bool isR24OfflineClone(R24WorldPermit const& p) noexcept {
    return p.directoryId==R24CloneDirectory
        &&p.displayName==R24CloneDisplayName
        &&!p.multiplayer&&p.globalEnabled&&!p.initializing&&!p.pending
        &&p.levelPacks==2;
}
}
