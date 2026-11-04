#pragma once
#include <algorithm>
#include <cstdint>

namespace praxis::companion {
// Tweakeroo's fastRightClickCount allows 1..64 actions per tick; match the
// configurable upper bound without bypassing server/item cooldown behavior.
// FastLeftClick remains limited to 8 until its separate throughput is tested.
struct FastClickOptions {
    int clicksPerTick = 1;
    int tickInterval = 1;
    bool allowInteractiveBlocks = false;
    bool allowSurvivalBreaking = false;
    // FastBlockPlacement owns block placement. Item use is the default scope.
    bool allowBlockPlacement = false;
    bool allowAirUse = true;
    bool operator==(FastClickOptions const&) const noexcept = default;
};
inline constexpr bool validFastClickOptions(FastClickOptions const& o,int maxClicks=64) noexcept {
    return o.clicksPerTick >= 1 && o.clicksPerTick <= maxClicks
        && o.tickInterval >= 1 && o.tickInterval <= 10;
}
enum class FastClickDecision : std::uint8_t {
    NoAction, RepeatRight, RepeatLeft
};
struct FastClickGate {
    bool featureEnabled{};
    bool gameplay{};
    bool focused{};
    bool held{};
    bool blockHit{};
    bool blockItem{};
    bool interactive{};
    bool allowInteractive{};
    bool creative{};
    bool allowSurvival{};
    bool placementOwnerBusy{};
    bool protectedTool{};
    bool itemUsable{};
    bool airUseAllowed{true};
    bool allowBlockPlacement{};
    bool longUseAction{};
};
inline constexpr bool fastClickAdmitted(FastClickDecision which,FastClickGate const& g) noexcept {
    if (!g.featureEnabled || !g.gameplay || !g.focused || !g.held
        || g.placementOwnerBusy || g.protectedTool) return false;
    if (which==FastClickDecision::RepeatRight) {
        // Not a second FastBlockPlacement: non-block items are fast by default.
        if (!g.itemUsable || g.longUseAction
            || (g.blockItem && !g.allowBlockPlacement)) return false;
        if (g.blockHit && g.interactive && !g.allowInteractive) return false;
        return g.blockHit || g.airUseAllowed;
    }
    if (which==FastClickDecision::RepeatLeft)
        return g.blockHit && (g.creative || g.allowSurvival);
    return false;
}
// Throttle is shared with each physical button independently. Never carry
// timing state across world generations, button releases or disabled modules.
struct FastClickCadence {
    std::uint64_t lastWorld{};
    std::uint64_t lastTick{};
    bool heldBefore{};
    unsigned due(std::uint64_t world,std::uint64_t tick,bool held,int interval,int count) noexcept {
        if(!held){lastWorld=world;lastTick=tick;heldBefore=false;return 0;}
        if(world!=lastWorld || (heldBefore && tick<lastTick)){
            lastWorld=world;lastTick=tick;heldBefore=true;
            return 0;
        }
        if(!heldBefore) {
            heldBefore=true;lastTick=tick;
            return 0; // vanilla handles initial click
        }
        if(tick==lastTick || tick-lastTick<static_cast<std::uint64_t>(interval))return 0;
        lastTick=tick;
        return static_cast<unsigned>(std::clamp(count,1,64));
    }
    void reset() noexcept {heldBefore=false;lastTick=0;}
};
} // namespace praxis::companion
