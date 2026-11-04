#pragma once
// The pinned native engine can report an identity PackType::Invalid (0) for a
// genuine registered ResourcePack. The manifest's *separate* PackType must
// still explicitly say Resources (6). UUID and exact version stay mandatory.
// This is not a general pack-type bypass, nor a substitute for repo ownership,
// native path, capabilities, dependencies and lock-state checks.
namespace praxis::companion::packs {
inline constexpr bool nativeRegisteredResourceIdentity(unsigned manifestType,unsigned identityType,
    unsigned resourcesType,unsigned invalidType,bool uuidMatches,bool versionMatches) noexcept {
    return manifestType==resourcesType&&(identityType==resourcesType||identityType==invalidType)
        &&uuidMatches&&versionMatches;
}
}
