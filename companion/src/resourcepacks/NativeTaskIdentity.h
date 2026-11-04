#pragma once
#include <cstdint>

namespace praxis::companion::packs {
// Exact seven-field CPU identity from ef262d EngineTransaction.h. No setter,
// provider, resource ownership or native pointer dereference belongs here.
struct CallbackBinding {
    std::uintptr_t operation{},game{},manager{};
    std::uint64_t token{},world{},cycle{},nativeRevision{};
};
inline bool callbackBindingMatches(CallbackBinding const& entry,CallbackBinding const& current) noexcept {
    return entry.operation&&entry.token&&entry.world&&entry.cycle&&entry.game&&entry.manager
        &&entry.operation==current.operation&&entry.token==current.token&&entry.world==current.world
        &&entry.cycle==current.cycle&&entry.game==current.game&&entry.manager==current.manager
        &&entry.nativeRevision==current.nativeRevision;
}
// Preserve the existing CPU tests' explicit non-authorization of native Apply.
enum class NativeSerialization { Unverified, CallbackTasksDrained };
inline constexpr NativeSerialization SupportedNativeSerialization=NativeSerialization::Unverified;
} // namespace praxis::companion::packs
