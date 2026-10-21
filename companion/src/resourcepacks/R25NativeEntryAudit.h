#pragma once
// R25: ABI-independent audit of the resource-reload callback hand-off.
// Preserve the native origin on every failure; never treat a Global stack
// update or a visual change as proof of peer/engine completion.
#include <cstdint>

namespace praxis::companion::packs {
struct R25NativeEntryAudit {
    bool seeded{},noCycle{},tokenMatches{},attemptMatches{},ownerMatches{},
         engineOwner{},fullMatches{},peersPrepared{},
         receiptEntered{},transactionStarted{},peersBegan{};
};
constexpr std::uint32_t r25NativeEntryMissing(R25NativeEntryAudit const& a) noexcept {
    return (std::uint32_t(!a.seeded)<<0)
         | (std::uint32_t(!a.noCycle)<<1)
         | (std::uint32_t(!a.tokenMatches)<<2)
         | (std::uint32_t(!a.attemptMatches)<<3)
         | (std::uint32_t(!a.ownerMatches)<<4)
         | (std::uint32_t(!a.engineOwner)<<5)
         | (std::uint32_t(!a.fullMatches)<<6)
         | (std::uint32_t(!a.peersPrepared)<<7)
         | (std::uint32_t(!a.receiptEntered)<<8)
         | (std::uint32_t(!a.transactionStarted)<<9)
         | (std::uint32_t(!a.peersBegan)<<10);
}
constexpr bool r25NativeEntryCompleted(R25NativeEntryAudit const& a) noexcept {
    return r25NativeEntryMissing(a)==0;
}
}
