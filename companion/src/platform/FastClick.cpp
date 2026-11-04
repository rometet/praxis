#include "platform/FastClick.h"
#include "core/FastClickPolicy.h"
#include "core/FastRightItemPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>

namespace praxis::companion {
namespace {
FastClickCadence rightCadence;
FastClickCadence leftCadence;
std::uint64_t tickSequence{};
std::atomic_bool fastClickInstalled{};
ULONGLONG lastFastClickTrace{};
void traceAttempt(char const* kind,unsigned attempts) noexcept {
    auto const now=GetTickCount64();
    if(now-lastFastClickTrace<3000)return;
    lastFastClickTrace=now;
    char buf[112]{};
    std::snprintf(buf,sizeof(buf),"FAST_CLICK action=%s repeatBudget=%u (server cooldowns still apply)",
        kind,attempts);
    logGuiInfo(buf);
}
bool physicallyHeld(int vk) noexcept {
    return (GetAsyncKeyState(vk)&0x8000)!=0;
}
bool hasFocus() noexcept {
    DWORD pid{};
    auto hwnd=GetForegroundWindow();
    return hwnd && GetWindowThreadProcessId(hwnd,&pid) && pid==GetCurrentProcessId();
}
bool conflictChord(int key,unsigned modifiers) noexcept {
    if(key<=0 || key>=256 || !physicallyHeld(key))return false;
    const auto now=modifierBits(physicallyHeld(VK_CONTROL),physicallyHeld(VK_SHIFT),
        physicallyHeld(VK_MENU));
    return modifiersMatch(modifiers,now);
}
bool placementModifiersActive(Settings const& s) noexcept {
    if(s.on(Feature::FlexibleBlockPlacement)) {
        auto const& o=s.flexiblePlacement;
        if(conflictChord(o.rotationKey,o.rotationModifiers)
            || conflictChord(o.offsetKey,o.offsetModifiers)
            || conflictChord(o.adjacentKey,o.adjacentModifiers))return true;
    }
    if(s.on(Feature::AccurateBlockPlacement)) {
        auto const& o=s.accuratePlacement;
        if(conflictChord(o.intoKey,o.intoModifiers)
            || conflictChord(o.reverseKey,o.reverseModifiers))return true;
    }
    return false;
}
bool admitted(LocalPlayer const& player,ClientInstance const& client) noexcept {
    auto const screen=client.getTopScreenName();
    if(screen!="hud_screen" && screen!="in_game_play_screen")return false;
    if(!runtimeReady.load(std::memory_order_acquire)
        || !inputAllowed.load(std::memory_order_acquire)
        || guiInputCaptured() || !hasFocus()
        || !client.isInGameInputEnabled() || !player.isAlive()
        || player.isSpectator()) return false;
    return true;
}
}
void resetFastClicks() noexcept {
    rightCadence.reset();leftCadence.reset();
}
void installFastClicks() noexcept {
    tickSequence=0;
    resetFastClicks();
    fastClickInstalled.store(true,std::memory_order_release);
    availableFeatures.fetch_or(
        featureRegistryBit(unsigned(Feature::FastRightClick))
        |featureRegistryBit(unsigned(Feature::FastLeftClick)),std::memory_order_release);
}
void removeFastClicks() noexcept {
    fastClickInstalled.store(false,std::memory_order_release);
    availableFeatures.fetch_and(~(featureRegistryBit(unsigned(Feature::FastRightClick))
        |featureRegistryBit(unsigned(Feature::FastLeftClick))),std::memory_order_release);
    resetFastClicks();
}
void tickFastClicks(LocalPlayer& player) noexcept {
    ++tickSequence;
    auto const current=settings();
    auto const generation=worldGeneration.load(std::memory_order_acquire);
    if(!fastClickInstalled.load(std::memory_order_acquire) || !current) {
        resetFastClicks();return;
    }
    auto client=ll::service::getClientInstance();
    if(!client || client->getLocalPlayer()!=&player || !admitted(player,*client)
        || placementModifiersActive(*current)) {
        resetFastClicks();return;
    }
    // Only real physical mouse holds. Do not forge OS events, synthetic button
    // states, or send raw network packets. Vanilla input owns the first click.
    bool const rightHeld=current->on(Feature::FastRightClick)&&physicallyHeld(VK_RBUTTON);
    bool const leftHeld=current->on(Feature::FastLeftClick)&&physicallyHeld(VK_LBUTTON);
    if(!rightHeld && !leftHeld){resetFastClicks();return;}
    auto* mode=player.mGameMode.get();
    if(!mode){resetFastClicks();return;}
    auto const& hit=client->getLatestHitResult();
    bool const blockHit=hit.mType==HitResultType::Tile && hit.mFacing<6;
    auto& region=player.getDimensionBlockSource();
    bool const interactive=blockHit
        && region.getBlock(hit.mBlock).getBlockType().isInteractiveBlock();
    auto const selected=player.getSelectedItemSlot();
    if(selected<0 || selected>=9){resetFastClicks();return;}
    auto& held=const_cast<ItemStack&>(player.getInventory().getItem(selected));
    bool const usable=!held.isNull() && held.mCount>0 && held.mItem;
    bool const blockItem=usable && (bool(held.mBlock)
        || held.mItem->isBlockPlanterItem());
    // Long-press consumption/charge states cannot safely be replayed as clicks.
    // Exclude them rather than cancelling food, bows or other held-use actions.
    bool const longUse=usable && (held.mItem->isFood()
        || held.mItem->getMaxUseDuration(&held)>0);
    bool const creative=player.isCreative();
    // Never alter another placement mode's action. Left mining in survival
    // normally keeps the vanilla hold-to-break stream uninterrupted.
    auto gateCommon=FastClickGate{true,true,true,true,blockHit,blockItem,
        interactive,current->fastRightClick.allowInteractiveBlocks,creative,
        current->fastLeftClick.allowSurvivalBreaking,false,
        current->on(Feature::ToolProtection) && !creative};
    gateCommon.itemUsable=usable;
    gateCommon.airUseAllowed=current->fastRightClick.allowAirUse;
    gateCommon.allowBlockPlacement=current->fastRightClick.allowBlockPlacement;
    gateCommon.longUseAction=longUse;
    auto rightGate=gateCommon;
    // Experience-bottle repeat is a measured no-op: preserve vanilla input.
    // FastRightClick's optional/advanced block placement must not defeat
    // FastBlockPlacement's remote-server ghost-block safeguard.
    bool const networked=client->isMultiPlayerClient() || client->isInMultiplayerGame();
    rightGate.featureEnabled=rightHeld
        && (!usable || fastRightExtraUseSupported(held.getTypeName()))
        && (!blockItem || !networked);
    // ToolProtection concerns mining/durability: it must not globally disable
    // ordinary non-block item use such as bone meal or throwable items.
    rightGate.protectedTool=false;
    auto leftGate=gateCommon;leftGate.featureEnabled=leftHeld;
    auto const rightCount=rightCadence.due(generation,tickSequence,
        fastClickAdmitted(FastClickDecision::RepeatRight,rightGate),
        current->fastRightClick.tickInterval,current->fastRightClick.clicksPerTick);
    auto const leftCount=leftCadence.due(generation,tickSequence,
        fastClickAdmitted(FastClickDecision::RepeatLeft,leftGate),
        current->fastLeftClick.tickInterval,current->fastLeftClick.clicksPerTick);
    // On a single tick choose just one type. If both mouse buttons are held,
    // prefer attack and do not accidentally place while mining.
    if(rightCount && !leftHeld) {
        traceAttempt("right",rightCount);
        auto const firstItemId=held.getId();
        auto const firstItemAux=held.getIdAux();
        for(unsigned i=0;i<rightCount;++i) {
            // Stop immediately when a bucket changes, a stack is depleted,
            // or inventory restock/selected slot changes beneath this burst.
            if(player.getSelectedItemSlot()!=selected || held.isNull()
                || held.mCount<=0 || held.getId()!=firstItemId
                || held.getIdAux()!=firstItemAux)break;
            try {
                if(blockHit) {
                    auto const position=hit.mBlock;
                    auto const& cell=region.getBlock(position);
                    if(cell.isAir() || (cell.getBlockType().isInteractiveBlock()
                        && !current->fastRightClick.allowInteractiveBlocks))break;
                    auto const relative=Vec3{
                        hit.mPos.x-static_cast<float>(position.x),
                        hit.mPos.y-static_cast<float>(position.y),
                        hit.mPos.z-static_cast<float>(position.z)};
                    auto const result=mode->useItemOn(held,position,hit.mFacing,
                        relative,HandSlot::Mainhand,&cell,true);
                    // Projectiles and similar items often do not use the clicked
                    // block; invoke their vanilla air-use handler only on failure.
                    if(!result.mSuccess && current->fastRightClick.allowAirUse
                        && !held.isNull() && held.mCount>0
                        && held.getId()==firstItemId && held.getIdAux()==firstItemAux)
                        (void)mode->useItem(held,HandSlot::Mainhand);
                } else if(current->fastRightClick.allowAirUse) {
                    (void)mode->useItem(held,HandSlot::Mainhand);
                }
            } catch (...) {break;}
        }
    } else if(leftCount && !rightHeld) {
        traceAttempt("left",leftCount);
        auto const position=hit.mBlock;
        auto const face=hit.mFacing;
        for(unsigned i=0;i<leftCount;++i) {
            if(region.getBlock(position).isAir())break;
            bool destroyed{};
            try {
                if(creative) {
                    (void)mode->startDestroyBlock(position,face,destroyed);
                } else {
                    // Experimental opt-in only. For survival default behavior
                    // is normal uninterrupted mining, never forced restarts.
                    (void)mode->continueDestroyBlock(position,face,player.getPosition(),destroyed);
                }
            } catch (...) {break;}
            if(destroyed)break;
        }
    }
}
} // namespace praxis::companion
