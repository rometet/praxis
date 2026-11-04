#include "platform/FastBlockPlacement.h"
#include "platform/Native.h"
#include "platform/FlexiblePlacement.h"
#include "core/FastBlockPlacementPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/Packet.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/HitResult.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <array>
#include <string>
#include <cstdint>
namespace praxis::companion {
namespace {
std::atomic_bool installed{};
std::atomic_uint accepted{};
std::atomic<std::uint64_t> remoteSuppressionLoggedForWorld{~std::uint64_t{0}};
// Remote trial is deliberately transient. Every world transition disables it.
std::atomic_bool remoteTrialEnabled{},remoteTrialStopped{};
std::atomic<std::uint64_t> observedWorld{},simulationTick{};
std::atomic<LocalPlayer*> lastTickPlayer{};
std::mutex remoteMutex;
FastPlacementRemoteFence remoteFence{};
struct PendingTrial {
    BlockPos target{};
    std::string predictedType;
    std::uint64_t firstTick{};
};
std::array<PendingTrial,16> pending{};
std::size_t pendingCount{};
std::atomic_uint remoteNativeTrialCount{};
// Native GameMode clock changes are CLIENT-side evidence only, not BDS ACKs.
std::atomic_uint remoteNativeBuildClockMoved{};
std::atomic_uint remoteNativeDelayReset{};
// Exact outgoing sender boundary, NOT server acceptance. The old crashed
// branch hooked both directions at startup. This one is installed only
// from a live LocalPlayer tick after the user opts in while in BDS.
std::atomic_bool wireRequested{},wireHookInstalled{},wireHookFailed{};
std::atomic_uint wireAuthCount{},wireItemUseCount{},wireItemStackRequestCount{};
std::atomic_uint wireBlockActionCount{},wireOtherCount{},wireDuplicateTickCount{};
std::atomic<std::uint64_t> wireLatestClientTick{},wireLatestLocalSimTick{};
std::atomic<ULONGLONG> wireLastReportAt{};

// Same native continuation hook only: read-only, bounded 2s counters.
enum class NativeTrialStage:unsigned {
    Enter=0, Early, Settings, World, Player, Slot, Item, Hit,
    Delay, Target, TickFence, Accelerated, Count
};
constexpr unsigned kTrialStages=static_cast<unsigned>(NativeTrialStage::Count);
std::array<std::atomic_uint,kTrialStages> trialStageCounts{};
std::atomic<ULONGLONG> trialLastDumpAt{};
std::atomic_uint trialLastHitFlags{};
void sampleTrial(NativeTrialStage stage) noexcept {
    if(remoteTrialEnabled.load(std::memory_order_relaxed))
        trialStageCounts[static_cast<unsigned>(stage)].fetch_add(1,std::memory_order_relaxed);
}
void resetTrialCounters() noexcept {
    for(auto& v:trialStageCounts)v.store(0,std::memory_order_relaxed);
    trialLastDumpAt.store(0,std::memory_order_relaxed);
    trialLastHitFlags.store(0,std::memory_order_relaxed);
}
bool remoteNetwork(IClientInstance& client) noexcept {
    return client.isMultiPlayerClient() || client.isInMultiplayerGame();
}
BlockPos adjacentTarget(BlockPos const& support,uchar face) noexcept {
    switch(face) {
    case 0:return {support.x,support.y-1,support.z};
    case 1:return {support.x,support.y+1,support.z};
    case 2:return {support.x,support.y,support.z-1};
    case 3:return {support.x,support.y,support.z+1};
    case 4:return {support.x-1,support.y,support.z};
    case 5:return {support.x+1,support.y,support.z};
    default:return support;
    }
}
void clearTrialLocked() noexcept {
    remoteFence.reset();
    for(auto& item:pending){item.predictedType.clear();item.firstTick=0;}
    pendingCount=0;
}

bool held(int vk) noexcept {return (GetAsyncKeyState(vk)&0x8000)!=0;}
bool focused() noexcept {
    DWORD process{};
    HWND hwnd=GetForegroundWindow();
    return hwnd && GetWindowThreadProcessId(hwnd,&process) && process==GetCurrentProcessId();
}
bool activePlacementModifier() noexcept {
    return flexiblePlacementModifiersHeld() || lholoPlacementOperationBusy();
}
LL_TYPE_INSTANCE_HOOK(FastPlacementOutgoingWireProbe,ll::memory::HookPriority::Normal,
    LoopbackPacketSender,&LoopbackPacketSender::$sendToServer,void,Packet& packet) {
    // Do not execute user code or anything that can transmit packets here.
    // Never inspect handshake / join packets, and always forward exactly once.
    if(wireRequested.load(std::memory_order_acquire)
        && installed.load(std::memory_order_relaxed)
        && runtimeReady.load(std::memory_order_relaxed)
        && inputAllowed.load(std::memory_order_relaxed)) {
        try {
            if(packet.getId()==MinecraftPacketIds::PlayerAuthInputPacket) {
                auto const& auth=static_cast<PlayerAuthInputPacket const&>(packet);
                auto const clientTick=auth.mClientTick.get().mValue;
                if(wireAuthCount.fetch_add(1,std::memory_order_relaxed)>0
                    && clientTick==wireLatestClientTick.load(std::memory_order_relaxed))
                    wireDuplicateTickCount.fetch_add(1,std::memory_order_relaxed);
                wireLatestClientTick.store(clientTick,std::memory_order_relaxed);
                wireLatestLocalSimTick.store(
                    simulationTick.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
                if(auth.mItemUseTransaction.get())
                    wireItemUseCount.fetch_add(1,std::memory_order_relaxed);
                if(auth.mItemStackRequest.get())
                    wireItemStackRequestCount.fetch_add(1,std::memory_order_relaxed);
                // Inspect only the compact input flag, not a vector payload.
                // This avoids the old experimental receive/send probe's
                // vulnerable nested packet-container dereferences.
                if(auth.mInputData.get().contains(
                    PlayerAuthInputPacketPayload::InputData::PerformBlockActions))
                    wireBlockActionCount.fetch_add(1,std::memory_order_relaxed);
            } else wireOtherCount.fetch_add(1,std::memory_order_relaxed);
        } catch (...) {
            // Never interrupt Minecraft networking because a diagnostic failed.
            wireHookFailed.store(true,std::memory_order_release);
            wireRequested.store(false,std::memory_order_release);
        }
    }
    origin(packet);
}
void reportWireProbe() noexcept {
    if(!wireRequested.load(std::memory_order_acquire))return;
    auto const now=GetTickCount64();
    auto previous=wireLastReportAt.load(std::memory_order_relaxed);
    if(now-previous<2500 || !wireLastReportAt.compare_exchange_strong(previous,now))return;
    char line[335]{};
    std::snprintf(line,sizeof(line),
        "FAST_BLOCK_WIRE senderHook=%u authTicks=%u itemUseTransactions=%u stackRequests=%u blockActions=%u otherPackets=%u duplicateAuthTicks=%u latestAuthTick=%llu latestLocalTick=%llu nativeBuildClockUpdates=%u (NOT server ACK)",
        unsigned(wireHookInstalled.load(std::memory_order_relaxed)),
        wireAuthCount.exchange(0),wireItemUseCount.exchange(0),
        wireItemStackRequestCount.exchange(0),wireBlockActionCount.exchange(0),
        wireOtherCount.exchange(0),wireDuplicateTickCount.exchange(0),
        static_cast<unsigned long long>(wireLatestClientTick.load()),
        static_cast<unsigned long long>(wireLatestLocalSimTick.load()),
        remoteNativeBuildClockMoved.load(std::memory_order_relaxed));
    logGuiInfo(line);
}
void reportTrialGateState(LocalPlayer& player,std::uint64_t tick) noexcept {
    if(!remoteTrialEnabled.load(std::memory_order_relaxed))return;
    auto now=GetTickCount64();
    auto previous=trialLastDumpAt.load(std::memory_order_relaxed);
    if(now-previous<2000)return;
    // One initial baseline after arming; further log lines require physical
    // right click or an actual native hook call. No idle-period log flood.
    if(previous && !held(VK_RBUTTON)
        && !trialStageCounts[0].load(std::memory_order_relaxed))return;
    if(!trialLastDumpAt.compare_exchange_strong(previous,now))return;
    unsigned n[kTrialStages]{};
    for(unsigned i=0;i<kTrialStages;++i)
        n[i]=trialStageCounts[i].exchange(0,std::memory_order_relaxed);
    auto client=ll::service::getClientInstance();
    bool const active=client && remoteNetwork(*client);
    auto const& hit=client?client->getLatestHitResult():HitResult{};
    char line[475]{};
    std::snprintf(line,sizeof(line),
        "FAST_BLOCK_BDS_GATE tick=%llu remote=%u creative=%u RMB=%u hit=%d face=%u hitFlags=0x%02x hook=%u early=%u settings=%u world=%u player=%u slot=%u item=%u hitGate=%u delay=%u target=%u fence=%u accelerated=%u",
        static_cast<unsigned long long>(tick),unsigned(active),
        unsigned(player.isCreative()),unsigned(held(VK_RBUTTON)),
        int(hit.mType),unsigned(hit.mFacing),
        trialLastHitFlags.load(std::memory_order_relaxed),
        n[0],n[1],n[2],n[3],n[4],n[5],n[6],n[7],
        n[8],n[9],n[10],n[11]);
    logGuiInfo(line);
}


LL_TYPE_INSTANCE_HOOK(FastPlacementContinueHook,ll::memory::HookPriority::Normal,
    GameMode,&GameMode::$continueBuildBlock,void,
    BlockPos const& support,uchar face,HandSlot hand,float buildDelayScale) {
    NativeHookLifetime::Lease lease(nativeHookLifetime());
    sampleTrial(NativeTrialStage::Enter);
    auto vanilla=[&] {origin(support,face,hand,buildDelayScale);};
    if(!lease.admitted()||!installed.load(std::memory_order_acquire)
        || !runtimeReady.load(std::memory_order_acquire)
        || !inputAllowed.load(std::memory_order_acquire)
        || !focused() || !held(VK_RBUTTON) || held(VK_LBUTTON)
        || guiInputCaptured() || face>=6 || hand!=HandSlot::Mainhand) {
        sampleTrial(NativeTrialStage::Early);
        vanilla();return;
    }
    auto s=settings();
    auto client=ll::service::getClientInstance();
    bool const remote=client && remoteNetwork(*client);
    if(!s || !s->fastBlockPlacement
        || !client || client->getClientInstanceState()!=ClientInstanceState::Playing
        || !client->isInGameInputEnabled()
        || (client->getTopScreenName()!="hud_screen"
            && client->getTopScreenName()!="in_game_play_screen")
        || fastPlacementCompetingBlockRepeat(
            remote,s->on(Feature::FastRightClick),s->fastRightClick.allowBlockPlacement)
        || activePlacementModifier()) {
        sampleTrial(NativeTrialStage::Settings);
        vanilla();return;
    }
    // The old 50ms BDS trial generated ghost blocks. Multiplayer remains
    // vanilla unless a session-local 150ms / 3-native-tick experiment is
    // explicitly armed *after joining this world*. No packet hooks at all.
    if(remote && (!remoteTrialEnabled.load(std::memory_order_acquire)
        || remoteTrialStopped.load(std::memory_order_acquire)
        || observedWorld.load(std::memory_order_acquire)
             !=worldGeneration.load(std::memory_order_acquire))) {
        auto const gen=worldGeneration.load(std::memory_order_acquire);
        auto prev=remoteSuppressionLoggedForWorld.load(std::memory_order_relaxed);
        if(prev!=gen && remoteSuppressionLoggedForWorld.compare_exchange_strong(prev,gen))
            logGuiInfo("FAST_BLOCK_PLACEMENT remote/BDS safety: vanilla default; optional bounded trial OFF");
        sampleTrial(NativeTrialStage::World);
        vanilla();return;
    }
    auto* player=client->getLocalPlayer();
    if(!player || &mPlayer!=static_cast<Player*>(player)
        || !player->isAlive() || player->isSpectator()
        || (remote && lastTickPlayer.load(std::memory_order_acquire)!=player)) {
        sampleTrial(NativeTrialStage::Player);
        vanilla();return;
    }
    auto slot=player->getSelectedItemSlot();
    if(slot<0||slot>=9){sampleTrial(NativeTrialStage::Slot);vanilla();return;}
    auto const& stack=player->getInventory().getItem(slot);
    bool const empty=stack.isNull()||stack.mCount<=0;
    bool const hasBlock=stack.mBlock!=nullptr;
    bool const planter=!empty && bool(stack.mItem)
        && stack.mItem->isBlockPlanterItem();
    std::string const itemName=empty?std::string{}:stack.getTypeName();
    std::string const blockName=hasBlock
        ?stack.mBlock->getTypeName():std::string{};
    if(!fastPlacementEligibleHeld(
        itemName,blockName,hasBlock,planter,empty)) {
        sampleTrial(NativeTrialStage::Item);
        if(remoteTrialEnabled.load(std::memory_order_relaxed)) {
            static std::atomic<ULONGLONG> lastItemNote{};
            auto const now=GetTickCount64();
            auto earlier=lastItemNote.load(std::memory_order_relaxed);
            if(now-earlier>=2000 && lastItemNote.compare_exchange_strong(earlier,now)) {
                char text[280]{};
                std::snprintf(text,sizeof(text),
                    "FAST_BLOCK_BDS_HELD_REJECT slot=%d count=%u empty=%u mBlock=%u planter=%u item=%.72s block=%.72s (no acceleration)",
                    int(slot),unsigned(stack.mCount),unsigned(empty),
                    unsigned(hasBlock),unsigned(planter),
                    itemName.c_str(),blockName.c_str());
                logGuiInfo(text);
            }
        }
        vanilla();return;
    }
    auto const& region=player->getDimensionBlockSource();
    auto const& block=region.getBlock(support);
    auto const& hit=client->getLatestHitResult();
    bool const supportInteractive=block.getBlockType().isInteractiveBlock();
    bool const modifier=activePlacementModifier();
    if(remoteTrialEnabled.load(std::memory_order_relaxed)) {
        // bit0 creative, bit1 support present, bit2 noninteractive,
        // bit3 tile, bit4 same support position, bit5 face, bit6 no modifier.
        auto bits=unsigned(player->isCreative())
            | (unsigned(!block.isAir())<<1)
            | (unsigned(!supportInteractive)<<2)
            | (unsigned(hit.mType==HitResultType::Tile)<<3)
            | (unsigned(hit.mBlock==support)<<4)
            | (unsigned(hit.mFacing==face)<<5)
            | (unsigned(!modifier)<<6);
        trialLastHitFlags.store(bits,std::memory_order_relaxed);
    }
    auto admitted=fastPlacementAdmitted(FastPlacementAdmission{
        true,true,true,true,player->isCreative(),true,
        !block.isAir(),supportInteractive,modifier,false});
    // Only native target, never substitute a synthetic hit result.
    if(!admitted||hit.mType!=HitResultType::Tile
        || hit.mBlock!=support || hit.mFacing!=face) {
        sampleTrial(NativeTrialStage::Hit);
        vanilla();return;
    }
    // Never transmit an extra packet or call the native placement twice.
    // For the unproven multiplayer trial, reserve a maximum of one shortened
    // continuation every three simulation ticks, for a unique support face.
    auto& delay=buildingTickDelay.get();
    auto originalDelay=delay;
    auto const trialDelay=std::chrono::milliseconds(remote?150:50);
    if(originalDelay<=trialDelay) {sampleTrial(NativeTrialStage::Delay);vanilla();return;}
    auto const target=adjacentTarget(support,face);
    if(remote) {
        // A strict unsnapped target: original support/face and empty neighbor.
        // If native snapping changes target, the original vanilla path wins.
        if(!region.getBlock(target).isAir()) {sampleTrial(NativeTrialStage::Target);vanilla();return;}
        auto const gen=worldGeneration.load(std::memory_order_acquire);
        auto const tick=simulationTick.load(std::memory_order_acquire);
        bool permit{};
        {
            std::lock_guard lock(remoteMutex);
            permit=remoteTrialEnabled.load(std::memory_order_relaxed)
                && !remoteTrialStopped.load(std::memory_order_relaxed)
                && observedWorld.load(std::memory_order_relaxed)==gen
                && pendingCount<pending.size()
                && remoteFence.reserve(gen,tick,support.x,support.y,support.z,face);
        }
        // Never call a native GameMode operation while holding the state lock.
        if(!permit) {sampleTrial(NativeTrialStage::TickFence);vanilla();return;}
    }
    sampleTrial(NativeTrialStage::Accelerated);
    // Read the native GameMode state around the ORIGINAL method exactly once.
    // Recomputed delay and the client-side build clock tell us whether the
    // 150ms field is respected and whether native placement actually starts.
    auto const beforeBuildClock=mLastBuildTime.get();
    delay=trialDelay;
    try {vanilla();} catch (...) {delay=originalDelay;throw;}
    auto const nativeDelayAfter=delay;
    auto const afterBuildClock=mLastBuildTime.get();
    delay=originalDelay;
    ++accepted;
    if(remote) {
        ++remoteNativeTrialCount;
        if(afterBuildClock!=beforeBuildClock)
            ++remoteNativeBuildClockMoved;
        if(nativeDelayAfter!=trialDelay)
            ++remoteNativeDelayReset;
        // The observed local placement is *predicted*, not server-confirmed.
        // Store only exact, unsnapped full-cube targets and trip if they revert.
        auto const& predicted=region.getBlock(target);
        if(!predicted.isAir() && fastPlacementFullCube(predicted.getTypeName())) {
            std::lock_guard lock(remoteMutex);
            if(pendingCount<pending.size()) {
                pending[pendingCount++]={target,predicted.getTypeName(),
                    simulationTick.load(std::memory_order_relaxed)};
            }
        }
        if(remoteNativeTrialCount.load(std::memory_order_relaxed)%10==1) {
            char line[310]{};
            std::snprintf(line,sizeof(line),
                "FAST_BLOCK_BDS_TRIAL originalDelayMs=%lld trialDelayMs=150 nativeAfterMs=%lld uniqueNativeCalls=%u nativeBuildClockUpdates=%u nativeDelayOverwrites=%u pendingPredictions=%u (not server ACK)",
                static_cast<long long>(originalDelay.count()),
                static_cast<long long>(nativeDelayAfter.count()),
                remoteNativeTrialCount.load(std::memory_order_relaxed),
                remoteNativeBuildClockMoved.load(std::memory_order_relaxed),
                remoteNativeDelayReset.load(std::memory_order_relaxed),
                unsigned([&]{std::lock_guard lock(remoteMutex);return pendingCount;}()));
            logGuiInfo(line);
        }
    } else if(accepted.load(std::memory_order_relaxed)%100==1)
        logGuiInfo("FAST_BLOCK_PLACEMENT offline original native 50ms continuation (not server ACK)");
}
} // namespace
void installFastBlockPlacement() noexcept {
    if(installed.load(std::memory_order_acquire))return;
    remoteSuppressionLoggedForWorld.store(~std::uint64_t{0},std::memory_order_release);
    if(FastPlacementContinueHook::hook()!=0){
        logGuiInfo("FastBlockPlacement native continuation hook unavailable");
        return;
    }
    installed.store(true,std::memory_order_release);
    remoteTrialEnabled.store(false);
    remoteTrialStopped.store(false);
    observedWorld.store(0);
    simulationTick.store(0);
    lastTickPlayer.store(nullptr);
    wireRequested.store(false,std::memory_order_release);
}
void setFastBlockPlacementRemoteTrial(bool enabled) noexcept {
    std::lock_guard lock(remoteMutex);
    clearTrialLocked();
    if(!enabled) {
        remoteTrialEnabled.store(false,std::memory_order_release);
        return;
    }
    // Once rollback has been observed, no re-arming in this world.
    if(remoteTrialStopped.load(std::memory_order_relaxed))return;
    resetTrialCounters();
    remoteNativeTrialCount.store(0,std::memory_order_relaxed);
    remoteNativeBuildClockMoved.store(0,std::memory_order_relaxed);
    remoteNativeDelayReset.store(0,std::memory_order_relaxed);
    remoteTrialEnabled.store(true,std::memory_order_release);
    logGuiInfo("FAST_BLOCK_BDS_TRIAL 150ms / 3 sim tick experiment armed (no server ACK); disable immediately on ghosts");
}
bool fastBlockPlacementRemoteTrial() noexcept {
    return remoteTrialEnabled.load(std::memory_order_acquire);
}
bool fastBlockPlacementRemoteTrialTripped() noexcept {
    return remoteTrialStopped.load(std::memory_order_acquire);
}
void setFastBlockPlacementWireProbe(bool enabled) noexcept {
    if(wireHookFailed.load(std::memory_order_acquire))return;
    // The GUI may only request it. Hook installation happens later on the
    // native world tick; never while handling GUI input or joining BDS.
    wireRequested.store(enabled,std::memory_order_release);
    wireLastReportAt.store(0,std::memory_order_relaxed);
    if(enabled) {
        wireAuthCount.store(0);wireItemUseCount.store(0);
        wireItemStackRequestCount.store(0);wireBlockActionCount.store(0);
        wireOtherCount.store(0);wireDuplicateTickCount.store(0);
        logGuiInfo("FAST_BLOCK_WIRE requested read-only outgoing probe after BDS join; no packet modification");
    }
}
bool fastBlockPlacementWireProbe() noexcept {
    return wireRequested.load(std::memory_order_acquire);
}
bool fastBlockPlacementWireProbeInstalled() noexcept {
    return wireHookInstalled.load(std::memory_order_acquire);
}
bool fastBlockPlacementWireProbeFailed() noexcept {
    return wireHookFailed.load(std::memory_order_acquire);
}
void tickFastBlockPlacement(LocalPlayer& player) noexcept {
    if(!installed.load(std::memory_order_acquire))return;
    auto const gen=worldGeneration.load(std::memory_order_acquire);
    auto* prevPlayer=lastTickPlayer.exchange(&player,std::memory_order_acq_rel);
    auto const prev=observedWorld.exchange(gen,std::memory_order_acq_rel);
    if(prev!=gen || prevPlayer!=&player) {
        std::lock_guard lock(remoteMutex);
        remoteTrialEnabled.store(false,std::memory_order_release);
        remoteTrialStopped.store(false,std::memory_order_release);
        wireRequested.store(false,std::memory_order_release);
        simulationTick.store(0,std::memory_order_release);
        clearTrialLocked();
        return;
    }
    auto const tick=simulationTick.fetch_add(1,std::memory_order_acq_rel)+1;
    // The ONE outgoing hook is installed only after the player has
    // actually entered BDS and manually opted in to the wire observer.
    if(wireRequested.load(std::memory_order_acquire)) {
        auto client=ll::service::getClientInstance();
        if(!client || client->getLocalPlayer()!=&player
            || client->getClientInstanceState()!=ClientInstanceState::Playing
            || !remoteNetwork(*client) || !inputAllowed.load()) {
            wireRequested.store(false,std::memory_order_release);
        } else {
            // Wait until the GUI has been closed and ordinary HUD gameplay
            // resumes; never patch the sender during menu interaction.
            if(tick>=3 && !guiInputCaptured() && client->isInGameInputEnabled()
                && !wireHookInstalled.load(std::memory_order_acquire)
                && !wireHookFailed.load(std::memory_order_acquire)) {
                if(FastPlacementOutgoingWireProbe::hook()==0) {
                    wireHookInstalled.store(true,std::memory_order_release);
                    logGuiInfo("FAST_BLOCK_WIRE outgoing-only hook installed AFTER BDS join");
                } else {
                    wireHookFailed.store(true,std::memory_order_release);
                    wireRequested.store(false,std::memory_order_release);
                    logGuiInfo("FAST_BLOCK_WIRE hook not available; observer disabled");
                }
            }
            reportWireProbe();
        }
    }
    // The master toggle may have been disabled while the trial was armed.
    // Disarm instead of silently resuming it if the master is reenabled.
    auto s=settings();
    if(!s || !s->fastBlockPlacement) {
        if(remoteTrialEnabled.exchange(false,std::memory_order_acq_rel)) {
            std::lock_guard lock(remoteMutex);
            clearTrialLocked();
        }
        return;
    }
    if(!remoteTrialEnabled.load(std::memory_order_acquire))return;
    reportTrialGateState(player,tick);
    try {
        std::lock_guard lock(remoteMutex);
        auto& region=player.getDimensionBlockSource();
        for(std::size_t i=0;i<pendingCount;) {
            auto& check=pending[i];
            if(region.getBlock(check.target).getTypeName()!=check.predictedType) {
                remoteTrialStopped.store(true,std::memory_order_release);
                remoteTrialEnabled.store(false,std::memory_order_release);
                clearTrialLocked();
                logGuiInfo("FAST_BLOCK_BDS_TRIAL AUTO-STOP: predicted block reverted; vanilla timing restored for this world");
                return;
            }
            if(tick>check.firstTick && tick-check.firstTick>=32) {
                pending[i]=std::move(pending[--pendingCount]);
            } else ++i;
        }
    } catch(...) {
        std::lock_guard lock(remoteMutex);
        remoteTrialStopped.store(true,std::memory_order_release);
        remoteTrialEnabled.store(false,std::memory_order_release);
        clearTrialLocked();
    }
}
void removeFastBlockPlacement() noexcept {
    installed.store(false,std::memory_order_release);
    remoteTrialEnabled.store(false,std::memory_order_release);
    wireRequested.store(false,std::memory_order_release);
    lastTickPlayer.store(nullptr,std::memory_order_release);
    {
        std::lock_guard lock(remoteMutex);
        clearTrialLocked();
    }
    remoteSuppressionLoggedForWorld.store(~std::uint64_t{0},std::memory_order_release);

    FastPlacementContinueHook::unhook();
}
} // namespace praxis::companion
