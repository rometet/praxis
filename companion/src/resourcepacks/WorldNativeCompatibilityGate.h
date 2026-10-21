#pragma once
// A *single* local resource pack appended to an otherwise unchanged Global
// stack may be considered for a native compatibility experiment. This does
// NOT confer rights to change Level, Treatment, Base, behavior, script, DRM,
// entitlement, device state, pending bits, or multi-player resource stacks.
#include <cstddef>
#include <cstdint>
namespace praxis::companion::packs {
struct WorldNativeCompatibilityAdmission {
    bool installed{},ownerThread{},sameGame{},sameWorld{},sameManager{},identity{},
        initial{},pending{},singleLocalAppend{},prepared{},originalInGame{},
        gameReady{},loadReady{},notLeaving{},notMultiplayer{},levelEmpty{},noNativeTask{};
    // R24: exact disposable singleplayer clone; Level identities unchanged.
    bool offlineCloneLevelPreserved{};
    // R27: only one verified ordinary local resource pack removed from
    // the tail of Global; its entire original prefix remains unchanged.
    bool singleLocalRemove{};
    // Native Minecraft editor may insert a registered ordinary texture
    // at the front or in the middle. All original stack identities and
    // their relative order MUST be preserved for this exact clone only.
    bool singleVanillaEditorChange{};
};
// Stable ABI-free per-predicate audit. Bit N marks a missing precondition,
 // so one runtime attempt tells us *why* the original engine veto remained
 // active, without weakening or overriding any precondition.
constexpr std::uint32_t worldNativeCompatibilityFailures(WorldNativeCompatibilityAdmission const& a) noexcept {
    return (std::uint32_t(!a.installed)<<0)|(std::uint32_t(!a.ownerThread)<<1)
        |(std::uint32_t(!a.sameGame)<<2)|(std::uint32_t(!a.sameWorld)<<3)
        |(std::uint32_t(!a.sameManager)<<4)|(std::uint32_t(!a.identity)<<5)
        |(std::uint32_t(!a.initial)<<6)|(std::uint32_t(!a.pending)<<7)
        |(std::uint32_t(!(a.singleLocalAppend||a.singleLocalRemove||a.singleVanillaEditorChange))<<8)|(std::uint32_t(!a.prepared)<<9)
        |(std::uint32_t(!a.originalInGame)<<10)|(std::uint32_t(!a.gameReady)<<11)
        |(std::uint32_t(!a.loadReady)<<12)|(std::uint32_t(!a.notLeaving)<<13)
        |(std::uint32_t(!a.notMultiplayer)<<14)|(std::uint32_t(!(a.levelEmpty||a.offlineCloneLevelPreserved))<<15)
        |(std::uint32_t(!a.noNativeTask)<<16);
}
constexpr bool admitWorldNativeCompatibility(WorldNativeCompatibilityAdmission const& a) noexcept {
    return worldNativeCompatibilityFailures(a)==0;
}
}
