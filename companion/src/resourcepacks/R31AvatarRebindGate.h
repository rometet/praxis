#pragma once
// R31: single-shot client entity renderer refresh after native pack reload.
// No graphic resources are refreshed unless the exact disposable clone,
// native-owner token, actor source and LHolo NativeActive receipts are valid.
#include <cstdint>
namespace praxis::companion::packs {
struct R31AvatarRebindGate {
    bool offlineClone{}, vanillaSettings{}, apiReload{}, nativeLoading{},
         applicationReady{}, frameOwner{}, gameplayScreen{}, gameReady{},
         noRestack{},actorDefinitions{},peersNativeActive{},firstAttempt{};
};
constexpr std::uint32_t r31AvatarRebindMissing(R31AvatarRebindGate const& g) noexcept {
    return (std::uint32_t(!g.offlineClone)<<0)
        |(std::uint32_t(!g.vanillaSettings)<<1)
        |(std::uint32_t(!g.apiReload)<<2)
        |(std::uint32_t(!g.nativeLoading)<<3)
        |(std::uint32_t(!g.applicationReady)<<4)
        |(std::uint32_t(!g.frameOwner)<<5)
        |(std::uint32_t(!g.gameplayScreen)<<6)
        |(std::uint32_t(!g.gameReady)<<7)
        |(std::uint32_t(!g.noRestack)<<8)
        |(std::uint32_t(!g.actorDefinitions)<<9)
        |(std::uint32_t(!g.peersNativeActive)<<10)
        |(std::uint32_t(!g.firstAttempt)<<11);
}
constexpr bool r31AvatarRebindAllowed(R31AvatarRebindGate const& g) noexcept {
    return r31AvatarRebindMissing(g)==0;
}
}
