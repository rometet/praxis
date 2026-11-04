#include "resourcepacks/WorldPackSwitch.h"
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/WorldPackParticipant.h"
#include "resourcepacks/WorldPackReloadApi.h"
#include "resourcepacks/World2660SelectionPlan.h"
#include "resourcepacks/Title2660Receipt.h"
#include "mc/resources/ResourcePackSelectionPolicy.h"
#include "core/Settings.h"
#include "core/NativeHookLifetime.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/Pack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/PackAccessStrategy.h"
#include "mc/scripting/modules/PackManifest.h"
#include "mc/world/level/PackInstanceId.h"
#include "mc/resources/IResourcePackRepository.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#if defined(PRAXIS_2660_TEST_WORLD_CANARY)
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/world/level/storage/LevelData.h"
#endif
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <algorithm>
#include <string>
#include <filesystem>
#include <vector>
#include <optional>
#include <memory>
#include <cctype>
#include <cwctype>
#include <array>

namespace praxis::companion::packs {
namespace {
auto& lifetime = *new NativeHookLifetime;
std::atomic_bool attempted{}, installed{}, lost{};
std::atomic<std::uint64_t> worldUpdateSerial{}, worldActiveSerial{}, worldFinishSerial{};
std::atomic<std::uintptr_t> worldManager{}, worldClient{}, worldRenderer{};
std::atomic<std::uint64_t> lastWorldFrame{}, lastWorldEpoch{};
std::atomic_flag worldTickGuard=ATOMIC_FLAG_INIT;
unsigned stableWorldFrames{};
std::vector<AppliedPack> lastWorldGlobal;
std::vector<AppliedPack> lastWorldLevel;
std::vector<AppliedPack> lastWorldBase;
std::vector<AppliedPack> lastWorldTreatment;
std::vector<AppliedPack> lastWorldFull;
void anchor(){}
bool pin() noexcept {
    HMODULE h{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&anchor),&h)!=0;
}
std::optional<std::string> subpack(ResourcePack const& pack,int index) {
    if(!pack.mImpl.get() || !pack.mImpl->mPack.get())return std::nullopt;
    auto const& children=pack.mImpl->mSubResourcePacks.get();
    auto const& subsidiary=pack.mImpl->mSubPacks.get();
    if(children.empty()&&subsidiary.empty()) {
        if(index==-1||index==0)return std::string{};
        return std::nullopt;
    }
    if(index<0)return std::string{};
    if(static_cast<std::size_t>(index)>=children.size())return std::nullopt;
    auto const& child=children[static_cast<std::size_t>(index)];
    if(!child || !child->mImpl.get() || !child->mImpl->mPack.get() ||
       !child->mImpl->mPack->mAccessStrategy)return std::nullopt;
    auto convert=[](std::string const& v) {
        return std::filesystem::path(std::u8string_view(
            reinterpret_cast<char8_t const*>(v.data()),v.size())).lexically_normal();
    };
    auto* strategy=child->mImpl->mPack->mAccessStrategy.get();
    auto relative=convert(strategy->getSubPath().value);
    if(relative.empty()||relative=="."){
        if(!pack.mImpl->mPack->mAccessStrategy)return std::nullopt;
        auto const& root=pack.mImpl->mPack->mAccessStrategy->getPackLocation().mPath.get().value;
        auto const& native=strategy->getPackLocation().mPath.get().value;
        if(root.empty()||native.empty())return std::nullopt;
        auto p=convert(root),q=convert(native);
        if(p==q)return std::nullopt;
        relative=q.lexically_relative(p);
    }
    if(relative.filename().empty())relative=relative.parent_path();
    if(relative.empty()||relative=="."||relative.is_absolute())return std::nullopt;
    std::vector<std::string> components;
    for(auto const& part:relative) {
        auto value=part.generic_string();
        if(value.empty()||value=="."||value=="..")return std::nullopt;
        components.emplace_back(std::move(value));
    }
    if(components.size()!=1 &&
       !(components.size()==2&&components[0]=="subpacks"))return std::nullopt;
    auto name=components.back();
    if(name.empty()||name.size()>64)return std::nullopt;
    for(unsigned char c:name)if(!std::isalnum(c)&&c!='-'&&c!='_')return std::nullopt;
    return name;
}
std::optional<AppliedPack> identity(PackInstance const& instance,bool protectedLayer=false) {
    auto pack=instance.mPack.get();
    if(!pack||!pack->mImpl.get()||!pack->mImpl->mPack.get()||
       !pack->mImpl->mPack->mManifest)return std::nullopt;
    auto name=subpack(*pack,instance.mSubpackIndex);
    if(!name){
        // Level/server/base stacks are never changed by this feature.
        // Their indexed subpacks can be identified opaquely for identity
        // comparison, without fabricating a name for the Global setter.
        if(!protectedLayer)return std::nullopt;
        name=std::string("__native_index_")+std::to_string(instance.mSubpackIndex);
    }
    auto const& manifest=pack->mImpl->mPack->mManifest->mIdentity.get();
    return AppliedPack{manifest.mId->asString(),manifest.mVersion->asString(),
                       *name,static_cast<unsigned>(manifest.mPackType)};
}
AppliedStackView snapshot(ResourcePackStack const* stack,std::uint32_t& reason,bool protectedLayer=false){
    AppliedStackView out;
    if(!stack){reason=protectedLayer?0:1;out.known=protectedLayer;return out;}
    if(stack->mStack->size()>512){reason=2;return out;}
    for(auto const& item:*stack->mStack){
        auto id=identity(item,protectedLayer);
        if(!id){reason=3;return {};}
        out.order.push_back(std::move(*id));
    }
    out.known=true;reason=0;return out;
}
AppliedStackView fullSnapshot(ResourcePackManager const& manager,std::uint32_t& reason){
    AppliedStackView out;
    manager.iteratePacks([&](PackInstance const& instance){
        if(reason||out.order.size()>=512){reason=4;return;}
        auto id=identity(instance,true);
        if(!id){reason=5;return;}
        out.order.push_back(std::move(*id));
    });
    out.known=!reason;return out;
}
bool same(std::vector<AppliedPack> const& a,std::vector<AppliedPack> const& b){
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){
        auto const& x=a[i];auto const& y=b[i];
        if(x.uuid!=y.uuid||x.version!=y.version||
           x.subpack!=y.subpack||x.packType!=y.packType)return false;
    }
    return true;
}
struct World2660Frame {
    std::shared_ptr<IClientInstance> client;
    ResourcePackManager* manager{};
    std::uintptr_t renderer{};
    std::uint64_t generation{};
    bool multiplayer{}, normal{}, renderReady{}, globalIncluded{};
};
std::optional<World2660Frame> frame(MinecraftGame& game) {
    if(!runtimeReady.load(std::memory_order_acquire)||
       !worldGeneration.load(std::memory_order_acquire)||
       !game.isInGame()||game.isEditorModeEnabled())return std::nullopt;
    auto clients=game.getClientInstanceMap();
    if(clients.size()!=1||!clients.begin()->second)return std::nullopt;
    auto client=clients.begin()->second;
    if(!client->getLocalPlayer()||!client->getLevelRenderer())return std::nullopt;
    auto& manager=client->getResourcePackManager();
    auto screen=client->getScreenName();
    World2660Frame result;
    result.client=std::move(client);
    result.manager=&manager;
    result.renderer=reinterpret_cast<std::uintptr_t>(result.client->getLevelRenderer());
    result.generation=worldGeneration.load(std::memory_order_acquire);
    result.multiplayer=result.client->isInMultiplayerGame();
    result.normal=screen=="hud_screen"||screen=="in_game_play_screen";
    result.renderReady=game.isReadyToRender()&&!game.isShowingLoadingScreen();
    result.globalIncluded=manager.mUseGlobalPackStack;
    return result;
}
#include "resourcepacks/World2660ApplyEngine.inl"
void publish(MinecraftGame& game) noexcept {
    if(!installed.load(std::memory_order_acquire)||
       worldTickGuard.test_and_set(std::memory_order_acquire))return;
    struct Scope {~Scope(){worldTickGuard.clear(std::memory_order_release);}} scope;
    try {
        auto now=titlePackNow();
        // Loading screens can make frame() or Global/Level snapshots
        // temporarily unavailable. Still enforce the native operation's
        // deadline, otherwise paused Praxis/LHolo peers may never resume.
        auto const active=worldOperation.step==World2660Step::Pausing ||
            worldOperation.step==World2660Step::WaitingNative ||
            worldOperation.step==World2660Step::Settling ||
            worldOperation.step==World2660Step::Reacquiring;
        if(active && now>worldOperation.deadline){
            if(!worldOperation.setterAttempted)world2660ClosePeers(false);
            world2660Finish(World2660Step::Failed,
                "再読み込みの完了を確認できません。Minecraftを再起動してください。",
                worldOperation.setterAttempted);
        }
        if(now-lastWorldFrame.load(std::memory_order_relaxed)<300)return;
        lastWorldFrame.store(now,std::memory_order_relaxed);
        auto owner=frame(game);
        if(!owner){
            worldManager.store(0,std::memory_order_release);
            worldClient.store(0,std::memory_order_release);
            stableWorldFrames=0;
            return; // TitlePack2660ReadOnly owns non-world presentation.
        }
        TitlePackView view;
        view.inWorld=true;
        view.until=now+1500;
        view.revision=worldUpdateSerial.load(std::memory_order_acquire);
        view.serverMultiplayer=owner->multiplayer;
        view.nativeGlobalIncluded=owner->globalIncluded;
        view.available=false;view.canReload=false;
        view.status="26.60 ワールド内: パック構成と再読み込み所有者を検証中です。";
        auto& manager=*owner->manager;
        if(!owner->renderReady||!owner->normal||manager.mInitializing||
            manager.mPendingRestack||!owner->globalIncluded){
            stableWorldFrames=0;
            view.block=PackApplyBlock::Loading;
            titlePackControl().publish(std::move(view));
            return;
        }
        std::uint32_t reason{};
        auto global=snapshot(manager.mGlobalStack.get(),reason);
        if(!global.known){view.status="26.60 ワールド内: Globalパックの識別情報が不足しています。";
            titlePackControl().publish(std::move(view));return;}
        auto level=snapshot(manager.mLevelStack.get(),reason,true);
        if(!level.known){view.status="26.60 ワールド内: Levelパックを確認できません。";
            titlePackControl().publish(std::move(view));return;}
        auto base=snapshot(manager.mBaseGameStack.get(),reason,true);
        if(!base.known){view.status="26.60 ワールド内: Baseパックを確認できません。";
            titlePackControl().publish(std::move(view));return;}
        auto treatment=snapshot(manager.mTreatmentStack.get(),reason,true);
        if(!treatment.known){view.status="26.60 ワールド内: Treatmentパックを確認できません。";
            titlePackControl().publish(std::move(view));return;}
        auto full=fullSnapshot(manager,reason);
        if(!full.known){view.status="26.60 ワールド内: 有効なFull Stackを確認できません。";
            titlePackControl().publish(std::move(view));return;}

        auto const unchanged=worldManager.load()==
                 reinterpret_cast<std::uintptr_t>(owner->manager) &&
             worldClient.load()==reinterpret_cast<std::uintptr_t>(owner->client.get()) &&
             worldRenderer.load()==owner->renderer &&
             lastWorldEpoch.load()==owner->generation &&
             same(lastWorldGlobal,global.order)&&same(lastWorldLevel,level.order)&&
             same(lastWorldBase,base.order)&&same(lastWorldTreatment,treatment.order)&&
             same(lastWorldFull,full.order);
        if(unchanged)stableWorldFrames=std::min(6u,stableWorldFrames+1);
        else {
            stableWorldFrames=1;
            lastWorldGlobal=global.order;
            lastWorldLevel=level.order;lastWorldBase=base.order;
            lastWorldTreatment=treatment.order;lastWorldFull=full.order;
            worldManager.store(reinterpret_cast<std::uintptr_t>(owner->manager));
            worldClient.store(reinterpret_cast<std::uintptr_t>(owner->client.get()));
            worldRenderer.store(owner->renderer);
            lastWorldEpoch.store(owner->generation);
            char message[250]{};
            std::snprintf(message,sizeof(message),
                "RESOURCEPACK_WORLD2660 native_context=1 global=%zu level=%zu treatment=%zu base=%zu full=%zu multiplayer=%u global_included=%u",
                global.order.size(),level.order.size(),treatment.order.size(),
                base.order.size(),full.order.size(),unsigned(owner->multiplayer),
                unsigned(owner->globalIncluded));
            logGuiInfo(message);
        }
        bool const stable=stableWorldFrames>=5;
        if(stable || (worldOperation.step!=World2660Step::Idle &&
                      worldOperation.step!=World2660Step::Finished &&
                      worldOperation.step!=World2660Step::Failed)){
            world2660Advance(game,*owner,global,now);
        }
        auto currentStep=worldOperation.step;
        if(stable && (currentStep==World2660Step::Idle ||
                      currentStep==World2660Step::Finished ||
                      currentStep==World2660Step::Failed) &&
                      !worldPermanentRecovery.load(std::memory_order_acquire)){
            // The UI is the only producer of an apply request. Native
            // admission is repeated immediately before any setter.
            if(auto request=titlePackControl().take()){
                bool started=false;
                try {started=world2660Begin(*owner,*request,
                           global,level,base,treatment,full,now);}
                catch(...){started=false;}
                if(!started){
                    view.token=request->token;
                    view.phase=TitlePackPhase::Rejected;
                    view.status="26.60: 適用前の検証で拒否しました。ゲームのパック構成は変更していません。";
                }else {
                    view.token=request->token;
                    view.phase=TitlePackPhase::Preparing;
                }
            }
        }
        currentStep=worldOperation.step;
        bool const busy=currentStep==World2660Step::Pausing ||
                        currentStep==World2660Step::WaitingNative ||
                        currentStep==World2660Step::Settling ||
                        currentStep==World2660Step::Reacquiring;
        view.canReload=stable && !busy && !worldPermanentRecovery.load(std::memory_order_acquire);
        view.available=stable && !busy &&
            !worldPermanentRecovery.load(std::memory_order_acquire);
        view.busy=busy;
        view.current=std::move(global);
        view.nativeLevelPackCount=level.order.size();
        view.nativeFullPackCount=full.order.size();
        view.nativeGlobalIncluded=true;
        view.serverPackContext=owner->multiplayer || !level.order.empty();
        view.block=view.available?PackApplyBlock::None:PackApplyBlock::Loading;
        view.folders={
            owner->client->getResourcePackRepository().getResourcePacksPath().value,
            owner->client->getResourcePackRepository().getDevelopmentResourcePacksPath().value};
        if(busy){
            view.token=worldOperation.token;
            view.phase=currentStep==World2660Step::Pausing?
                TitlePackPhase::Preparing:TitlePackPhase::Reloading;
            view.status=worldOperation.status;
        }else if(currentStep==World2660Step::Finished){
            view.token=worldOperation.token;
            view.phase=worldOperation.rollback?
                TitlePackPhase::Restored:TitlePackPhase::Complete;
            view.status=worldOperation.status;
        }else if(currentStep==World2660Step::Failed){
            view.token=worldOperation.token;
            view.phase=worldPermanentRecovery.load()?
                TitlePackPhase::RecoveryRequired:TitlePackPhase::Rejected;
            view.status=worldOperation.status;
        }else if(view.phase!=TitlePackPhase::Rejected){
            view.status=stable
                ? "26.60: ワールド内GlobalパックのON/OFF・優先順位変更を準備できました。Level・サーバーパックは変更しません。"
                : "26.60: 同一の構成が安定するまで照合しています。";
        }
        titlePackControl().publish(std::move(view));
#if defined(PRAXIS_2660_TEST_WORLD_CANARY)
        // Automated no-op native reload, on one disposable test world ONLY.
        // Never used in the packaged end-user build. It exercises the
        // actual world peer + reload transaction without changing pack order.
        static bool canaryRequested{};
        if(!canaryRequested && stable && !busy &&
           owner->client->getLevel() &&
           owner->client->getLevel()->getLevelData().mLevelName.get() ==
                "Praxis 26.60 HotSwap TEST (disposable)") {
            canaryRequested=true;
            auto result=titlePackControl().requestResult(
                nullptr,Selection{},now,true);
            logGuiInfo(result.accepted
                ? "RESOURCEPACK_WORLD2660 canary_same_stack_request=1"
                : "RESOURCEPACK_WORLD2660 canary_same_stack_request=0");
        }
#endif
    }catch(...){
        TitlePackView view;
        view.inWorld=true;view.available=false;view.canReload=false;
        view.until=titlePackNow()+500;
        view.status="26.60 ワールド内: 構成の取得に失敗しました。";
        titlePackControl().publish(std::move(view));
    }
}
LL_TYPE_INSTANCE_HOOK(World2660NativeUpdateHook,ll::memory::HookPriority::Normal,
    MinecraftGame,&MinecraftGame::$update,void){
    NativeHookLifetime::Lease lease(lifetime);
    origin();
    if(lease.admitted()){
        worldUpdateSerial.fetch_add(1,std::memory_order_acq_rel);
        publish(*this);
    }
}
LL_TYPE_INSTANCE_HOOK(World2660NativeActiveHook,ll::memory::HookPriority::Normal,
    MinecraftGame,&MinecraftGame::_onActiveResourcePacksChanged,
    void,ResourcePackManager& manager,bool vanilla){
    NativeHookLifetime::Lease lease(lifetime);
    bool own=lease.admitted() && worldNativeThread.load(std::memory_order_acquire)==GetCurrentThreadId()
        && worldOperation.owner==reinterpret_cast<std::uintptr_t>(&manager)
        && worldOperation.receipt.isInNativeCall();
    if(own) worldOperation.receipt.onActive(static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&manager)));
    origin(manager,vanilla);
    if(lease.admitted() && worldManager.load()==reinterpret_cast<std::uintptr_t>(&manager))
        worldActiveSerial.fetch_add(1,std::memory_order_acq_rel);
}
LL_TYPE_INSTANCE_HOOK(World2660NativeFinishHook,ll::memory::HookPriority::Normal,
    MinecraftGame,&MinecraftGame::_finishReloadingResources,
    void,ResourcePackManager& manager,bool vanilla){
    NativeHookLifetime::Lease lease(lifetime);
    origin(manager,vanilla);
    bool own=lease.admitted() && worldNativeThread.load(std::memory_order_acquire)==GetCurrentThreadId()
        && worldOperation.owner==reinterpret_cast<std::uintptr_t>(&manager)
        && worldOperation.receipt.isInNativeCall();
    if(own) worldOperation.receipt.onFinish(static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&manager)));
    if(lease.admitted() && worldManager.load()==reinterpret_cast<std::uintptr_t>(&manager))
        worldFinishSerial.fetch_add(1,std::memory_order_acq_rel);
}
} // namespace
bool installWorldPackSwitch() noexcept {
    if(attempted.exchange(true))return installed.load();
    try {
        if(!lifetime.prepare(pin))return false;
        lifetime.registrationStarted();
        if(World2660NativeUpdateHook::hook()!=0 ||
           World2660NativeActiveHook::hook()!=0 ||
           World2660NativeFinishHook::hook()!=0 || !lifetime.open()){
            lifetime.close();return false;
        }
        installed.store(true,std::memory_order_release);
        return true;
    }catch(...){lifetime.close();return false;}
}
bool stopWorldPackSwitch() noexcept {
    installed.store(false,std::memory_order_release);
    invalidateWorldPackOperation();
    lifetime.close();
    return lifetime.readers()==0;
}
void invalidateWorldPackOperation() noexcept {
    lost.store(true,std::memory_order_release);
    if(worldOperation.step==World2660Step::Pausing ||
       worldOperation.step==World2660Step::WaitingNative ||
       worldOperation.step==World2660Step::Settling ||
       worldOperation.step==World2660Step::Reacquiring){
        world2660Finish(World2660Step::Failed,
            "ワールドの変更で再読み込みが中断しました。再起動して状態を確認してください。",
            worldOperation.setterAttempted);
        if(!worldOperation.setterAttempted)world2660ClosePeers(false);
    }
    worldManager.store(0,std::memory_order_release);
    stableWorldFrames=0;
}
} // namespace praxis::companion::packs
