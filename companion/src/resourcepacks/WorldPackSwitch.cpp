#include "resourcepacks/WorldPackSwitch.h"
#include "resourcepacks/WorldPackParticipant.h"
#include "resourcepacks/WorldPackNativeAdapter.h"
#include "resourcepacks/R24OfflineCloneGate.h"
#include "resourcepacks/R27LocalToggle.h"
#include "resourcepacks/NativeVanillaPackSelection.h"
#include "resourcepacks/R25NativeEntryAudit.h"
#include "resourcepacks/R31AvatarRebindGate.h"
#include "resourcepacks/WorldNativeApplicationReceipt.h"
#include "resourcepacks/WorldEngineReloadReceipt.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include "resourcepacks/WorldPackPeers.h"
#include "resourcepacks/LocalOverlayStage.h"
#include "resourcepacks/LocalTerrainAtlasProof.h"
#include "mc/client/renderer/texture/TextureAtlas.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "resourcepacks/LocalPngDecodeWin.h"
#include "resourcepacks/ServerResourcePackAudit.h"
#include "resourcepacks/WorldPackPreflightDiagnostics.h"
#include "resourcepacks/NativeResourceIdentityGate.h"
#include "resourcepacks/NativeSettingsPackScope.h"
#include "resourcepacks/NativeSettingsExitEvent.h"
#include "resourcepacks/TitlePackSwitch.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/client/gui/screens/controllers/GeneralSettingsScreenController.h"
#include "mc/client/gui/screens/controllers/ResourcePackApplyingScreenController.h"
#include "mc/client/gui/screens/controllers/SettingsScreenController.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/models/MainMenuScreenModel.h"
#include "mc/client/gui/screens/models/PackManagerContentSourceImpl.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/GameRenderer.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/screens/models/MinecraftScreenModel.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackListener.h"
#include "mc/resources/ResourceLoadManager.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/deps/core/file/Path.h"
#include "mc/client/renderer/TextureGroup.h"

#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceFileSystem.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/Pack.h"
#include "mc/resources/PackAccessStrategy.h"
#include "mc/resources/IResourcePackRepository.h"
#include "mc/scripting/modules/PackManifest.h"
#include "mc/world/level/PackInstanceId.h"
#include "mc/deps/core/threading/BackgroundTask.h"
#include "mc/deps/core/threading/BackgroundTaskBase.h"
#include "mc/deps/core/threading/TaskResult.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <new>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>

namespace praxis::companion::packs {
namespace {
auto& lifetime=*new NativeHookLifetime;
ServerAssetLayer lastObservedServerSource{ServerAssetLayer::Unknown};
std::atomic_bool attempted{},installed{},lost{};
std::atomic_bool completionHookAttempted{},completionHooksReady{},settingsBootstrapDiagnosticAttempted{};
std::atomic<std::uintptr_t> diagnosticPraxisModule{};
std::atomic<unsigned> callbacks{},nativeTasks{};
std::atomic<DWORD> ownerThread{};
std::atomic<MinecraftGame*> observedGame{};
std::atomic<std::uintptr_t> observedClient{};
std::atomic<std::uint64_t> observedWorld{};
std::atomic<std::uint64_t> revision{1},cycles{},finishProducers{},participantTokens{};
std::atomic<std::uint64_t> frameInvocations{};
std::atomic<std::uint64_t> settingsSessions{};
std::atomic<std::uint64_t> nativeReloadSerial{};
// Native original verdict remains authoritative. This experimental
// compatibility window masks exactly one virtual in-game query *inside* the
// game's own FullInvalid origin. Unlike a raw false->true return override it
// allows native resource/task teardown logic to run. No persistent game state
// bytes are written. Still not known safe until native frame/GPU verification.
std::atomic<std::uint64_t> packInvalidVotes{}, packInvalidRejects{};
thread_local unsigned worldCompatibilityMaskDepth{};
thread_local unsigned worldCompatibilityQueryHits{};
struct ScopedWorldCompatibilityQuery {
    ScopedWorldCompatibilityQuery() noexcept {++worldCompatibilityMaskDepth;}
    ~ScopedWorldCompatibilityQuery(){--worldCompatibilityMaskDepth;}
    ScopedWorldCompatibilityQuery(ScopedWorldCompatibilityQuery const&)=delete;
};
std::atomic_flag ticking=ATOMIC_FLAG_INIT;
bool installWorldNativeCompletionObservers(DWORD thread) noexcept;
std::uintptr_t address(void const* p){return reinterpret_cast<std::uintptr_t>(p);}
void anchor(){}
bool pin(){HMODULE h{};bool accepted=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&anchor),&h)!=0;
    if(accepted)diagnosticPraxisModule=address(h);return accepted;}
int praxisPeer(WorldPackReloadCommand c,WorldPackReloadRequest const* r,WorldPackReloadReply* p) noexcept {return praxisWorldParticipant().command(c,r,p);}
struct LholoBinding {std::shared_ptr<ll::mod::NativeMod> owner;HMODULE module{};WorldPackReloadParticipant entry{};};
auto& binding=*new LholoBinding;
bool resolveLholo(){
    auto mod=ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
    if(!mod)return !binding.owner;
    if(mod->getType()!=ll::mod::NativeModManagerName||!mod->isEnabled())return false;
    auto owner=std::static_pointer_cast<ll::mod::NativeMod>(mod);if(!owner->getHandle())return false;
    if(binding.owner)return binding.owner==owner&&binding.entry;
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(owner->getHandle()),&module))return false;
    auto entry=reinterpret_cast<WorldPackReloadParticipant>(GetProcAddress(module,"lholo_resource_reload_v4"));
    if(!entry)return false;binding={std::move(owner),module,entry};return true;
}
std::vector<std::string> keys(std::vector<PackInstanceId> const& values){
    std::vector<std::string> out;out.reserve(values.size());for(auto const& id:values)
        out.push_back(std::to_string(unsigned(id.mPackId->mPackType))+"/"+id.mPackId->mId->asString()+"/"+id.mPackId->mVersion->asString()+"/"+id.mSubpackName.get());return out;
}
std::vector<PackInstanceId> ids(ResourcePackStack const* stack){if(!stack)return {};if(stack->mStack->size()>512)throw std::runtime_error("stack budget");return stack->getStackAsIdentities();}
AppliedStackView viewIds(std::vector<PackInstanceId> const& values){AppliedStackView view;view.known=true;for(auto const& id:values)view.order.push_back({id.mPackId->mId->asString(),id.mPackId->mVersion->asString(),id.mSubpackName.get(),unsigned(id.mPackId->mPackType)});return view;}
bool pathEqual(std::string const& a,std::string const& b){
    auto path=[](std::string const& s){return std::filesystem::path(std::u8string_view(reinterpret_cast<char8_t const*>(s.data()),s.size())).lexically_normal().wstring();};
    auto x=path(a),y=path(b);return !x.empty()&&CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;
}
std::string packPath(ResourcePack const& pack){auto const& data=pack.mImpl.get()->mPack.get();return data&&data->mAccessStrategy?data->mAccessStrategy->getPackLocation().getFullPath().value:std::string{};}
bool ordinary(ResourcePack const& pack,Pack const& entry,unsigned* reason=nullptr){
    auto reject=[&](unsigned code){if(reason)*reason=code;return false;};
    auto const& native=pack.mImpl.get()->mPack.get();if(!native||!native->mManifest)return reject(1);
    if(!pathEqual(packPath(pack),entry.key))return reject(2);
    auto const& m=*native->mManifest;auto const& identity=m.mIdentity.get();
    auto manifestType=unsigned(m.mPackType);
    auto identityType=unsigned(identity.mPackType);
    bool uuidMatches=identity.mId->asString()==entry.uuid;
    bool versionMatches=identity.mVersion->asString()==entry.version;
    packDiagnostic("RESOURCEPACK_WORLD event=identity_gate manifest_type=%u id_type=%u resources_type=%u invalid_type=%u uuid_match=%u version_match=%u",
        manifestType,identityType,unsigned(PackType::Resources),unsigned(PackType::Invalid),unsigned(uuidMatches),unsigned(versionMatches));
    if(!nativeRegisteredResourceIdentity(manifestType,identityType,
        unsigned(PackType::Resources),unsigned(PackType::Invalid),uuidMatches,versionMatches))return reject(3);
    bool permitted=!m.mExpired&&!m.mIsTitleLocked&&!m.mIsPlatformLocked&&!m.mIsHidden&&!m.mFailsGameVersionMatch
        &&!m.mHasClientScript&&!m.mHasPlugins&&m.mPackDependencies->empty()&&m.mCapabilities->empty();
    if(!permitted)return reject(4);if(reason)*reason=0;return true;
}
struct Frame {WorldPackOwner owner;std::shared_ptr<IClientInstance> client;bool normal{};PackApplyBlock block{PackApplyBlock::Owner};};
Frame frame(MinecraftGame& game){
    Frame out;DWORD zero{};auto thread=GetCurrentThreadId();ownerThread.compare_exchange_strong(zero,thread);
    if(thread!=ownerThread.load()||!runtimeReady.load()||!game.isInGame()||!worldGeneration.load()||game.isEditorModeEnabled())return out;
    auto clients=game.getClientInstanceMap();if(clients.size()!=1)return out;out.client=clients.begin()->second;
    if(!out.client||!out.client->getLocalPlayer())return {};
    auto& manager=out.client->getResourcePackManager();
    auto screen=out.client->getScreenName();out.normal=screen=="hud_screen"||screen=="in_game_play_screen";
    out.owner={address(&game),address(&manager),address(out.client.get()),address(out.client->getLevelRenderer()),
        worldGeneration.load(),thread,true,bool(out.client->getLevelRenderer())&&game.isReadyToRender()&&!game.isShowingLoadingScreen(),manager.mPendingRestack};
    out.block=!out.normal?PackApplyBlock::Screen:out.owner.pending?PackApplyBlock::Restack:!out.owner.ready?PackApplyBlock::Loading:PackApplyBlock::None;
    return out;
}
// This authorization applies only to the explicitly named disposable test world.
bool authorizedR24Clone(std::shared_ptr<IClientInstance> const& client) noexcept {
    if(!client)return false;
    try {
        auto* level=client->getLevel();
        if(!level)return false;
        auto& rpm=client->getResourcePackManager();
        std::string const levelId=level->getLevelId();
        R24WorldPermit facts{
            levelId, level->getLevelData().mLevelName.get(),
            client->isInMultiplayerGame(),bool(rpm.mUseGlobalPackStack),
            bool(rpm.mInitializing),bool(rpm.mPendingRestack),
            rpm.mLevelStack.get()?rpm.mLevelStack->mStack->size():0
        };
        return isR24OfflineClone(facts);
    }catch(...){return false;}
}
struct Operation {
    std::mutex mutex;
    WorldPackTransaction tx;
    WorldNativeContext cycle;
    WorldNativeApplicationReceipt executed;
    WorldEngineReloadReceipt engine;
    NativeSettingsApplyOnce selectionOnce;
    std::vector<std::string> expectedFull;
    bool apiReload{},selectionCommitted{},explicitEngineRequested{},expectedFullKnown{},engineServiceLogged{},listenerSnapshotLogged{},compatibilityAuditLogged{};
    bool r24AuthorizedClone{}, r24SourceResolutionLogged{}, r26PeersNativeArmed{};
    bool r31AvatarRebindAttempted{},r31AvatarRebindReturned{};
    unsigned r33PerspectiveSampleCount{};
    std::uint64_t r33NextPerspectiveSampleAt{};
    R27LocalToggle r27ToggleKind{R27LocalToggle::Invalid};
    unsigned composeAttempts{},engineDiagnosticState{};
    std::uint64_t nextComposeServiceAt{};
    std::uint64_t nextRestackDiagnostic{};
    std::weak_ptr<AbstractScene> applicationScene;
    TitlePackRequest request;
    std::shared_ptr<IClientInstance> client;
    std::vector<PackInstanceId> oldIds,newIds;
    std::vector<std::string> level,treatment,base;
    std::vector<std::string> editorInitialGlobal;
    std::vector<std::shared_ptr<ResourcePack>> ownedPacks;
    std::unique_ptr<ResourcePackStack> candidate,backup;
    WorldPackPeers group{praxisPeer};
    std::string detail;
    bool nativeSettings{},nativeNavigation{};
    std::weak_ptr<MinecraftScreenController> settingsRoot;
    std::shared_ptr<MinecraftScreenController> pendingRoot;
    std::shared_ptr<MainMenuScreenModel> pendingModel;
    // Retain only the model needed for the later formal loading observation;
    // the Settings scene and controller are released after owned replay.
    std::shared_ptr<MainMenuScreenModel> completionModel;
    std::weak_ptr<AbstractScene> pendingScene;
    std::weak_ptr<AbstractScene> settingsScene;
    std::weak_ptr<MinecraftScreenController> settingsRetryRoot;
    std::weak_ptr<MainMenuScreenModel> settingsRetryModel;
    std::uintptr_t settingsRetrySource{};
    NativeSettingsPackContext settingsContext;
    WorldPackOwner editorOwner;
    std::unique_ptr<NativeSettingsExitEvent> pendingExit;
    std::vector<std::string> pendingSelection;
    std::uint64_t pendingRevision{};
    std::uintptr_t pendingSource{};
    unsigned settingsSceneId{};
    bool exitReplaying{},exitReplayed{};
    std::uint64_t settingsSession{};
    std::uintptr_t settingsInitRoot{};
    SettingsTabIndex pendingTab{};
    std::uint32_t settingsExitDiagnostics{};
    std::uint32_t settingsApplicationDiagnostics{};
    WorldSwitchPhase loggedPhase{WorldSwitchPhase::Idle};
    std::array<PackPeerObservation,2> loggedPeers;
    std::uint64_t nextDiagnostic{};
};
struct WorldFrameInvocation {
    std::shared_ptr<Operation> operation;
    WorldNativeApplicationReceipt::FramePermit permit;
};
thread_local WorldFrameInvocation worldFrameInvocation;
struct WorldFrameScope {
    WorldFrameInvocation previous;
    explicit WorldFrameScope(WorldFrameInvocation current):previous(std::move(worldFrameInvocation)){
        worldFrameInvocation=std::move(current);
    }
    ~WorldFrameScope(){worldFrameInvocation=std::move(previous);}
};
struct NativeSettingsPendingHolds {
    std::shared_ptr<MinecraftScreenController> root;
    std::shared_ptr<MainMenuScreenModel> model;
    std::unique_ptr<NativeSettingsExitEvent> event;
};
NativeSettingsPendingHolds takePendingSettingsExit(Operation& op) noexcept {
    NativeSettingsPendingHolds held{std::move(op.pendingRoot),std::move(op.pendingModel),std::move(op.pendingExit)};
    op.pendingScene.reset();op.pendingSelection.clear();op.pendingRevision=0;op.pendingSource=0;
    return held;
}
auto& active=*new std::atomic<std::shared_ptr<Operation>>;
auto& retained=*new std::vector<std::shared_ptr<Operation>>;
constexpr std::size_t OperationBudget=16;
void observeOperation(Operation& op,std::uint64_t now){
    if(op.loggedPhase!=op.tx.phase||now>=op.nextDiagnostic){
        op.loggedPhase=op.tx.phase;op.nextDiagnostic=now+5000;
        packDiagnostic("RESOURCEPACK_WORLD event=progress token=%llu phase=%u attempt=%llu old_epoch=%llu next_epoch=%llu started=%u returned=%u finished=%u stable=%u mask=%u",
            diagNumber(op.tx.token),unsigned(op.tx.phase),diagNumber(op.tx.attempt),diagNumber(op.tx.participant.oldEpoch),diagNumber(op.tx.participant.nextEpoch),
            unsigned(op.tx.started),unsigned(op.tx.startReturned),unsigned(op.tx.finished),op.tx.stable,op.group.mask);
        if(op.tx.phase==WorldSwitchPhase::Reacquiring||op.tx.phase==WorldSwitchPhase::Complete||op.tx.phase==WorldSwitchPhase::Restored)
            packDiagnostic("RESOURCEPACK_WORLD event=stack_check token=%llu match=%u restoring=%u expected=%zu observed=%zu visual_verified=0",
                diagNumber(op.tx.token),unsigned(op.tx.lastStable==(op.tx.restoring?op.tx.oldGlobal:op.tx.targetGlobal)),unsigned(op.tx.restoring),
                (op.tx.restoring?op.tx.oldGlobal:op.tx.targetGlobal).size(),op.tx.lastStable.size());
    }
    for(unsigned i=0;i<op.group.count;++i){auto const& p=op.group.observations[i];
        if(p.observed&&p!=op.loggedPeers[i]){
            op.loggedPeers[i]=p;
            packDiagnostic("RESOURCEPACK_WORLD event=peer token=%llu peer=%u command=%u accepted=%u epoch=%llu phase=%u error=%u readers=%u jobs=%u borrowers=%u gpu=%u receipt=%u",
                diagNumber(op.tx.token),i,unsigned(p.command),unsigned(p.accepted),diagNumber(p.epoch),unsigned(p.phase),p.error,p.readers,p.jobs,p.borrowers,unsigned(p.gpu),unsigned(p.receiptMatched));
        }
    }
}
void stopPeers(Operation& op){op.group.stop(op.tx);}
void pause(Operation& op,std::uint64_t now){
    // Every new attempt must rearm the native execution epoch explicitly.
    op.r26PeersNativeArmed=false;
    op.r31AvatarRebindAttempted=false;
    op.r31AvatarRebindReturned=false;
    op.r33PerspectiveSampleCount=0;
    op.r33NextPerspectiveSampleAt=0;
    op.group.pause(op.tx,participantTokens.fetch_add(1)+1,currentResourceEpoch(),now);
}
bool ready(Operation& op,WorldPackReloadPhase phase,std::uint64_t epoch){return op.group.ready(op.tx,phase,epoch);}
bool commandPeers(Operation& op,WorldPackReloadCommand command){return op.group.transition(op.tx,command);}
bool settingsExitCurrent(Operation const& op,WorldPackOwner const& current) noexcept;
void replaySettingsExit(std::shared_ptr<Operation> const& op) noexcept;
bool resetSettingsCancelledForRetry(Operation& op,WorldPackOwner const& current) noexcept;
void runWorldSelection(std::shared_ptr<Operation> const& op,MinecraftScreenModel& model,std::function<void()> setter) noexcept;
void serviceWorldEngineReload(MinecraftGame& game) noexcept;
// The definition is in WorldEngineReloadDriver.h (included after the
// native Settings hooks). Early ownership of the native transaction must
// happen BEFORE vanilla _setResourcePackStack invokes LHolo's cache hook.
bool armR26NativePeers(Operation& op,char const* route) noexcept;
void prepare(Operation& op,MinecraftGame&){
    auto& manager=op.client->getResourcePackManager();auto& repository=op.client->getResourcePackRepository();
    op.oldIds=ids(manager.mGlobalStack.get());op.level=keys(ids(manager.mLevelStack.get()));op.treatment=keys(ids(manager.mTreatmentStack.get()));op.base=keys(ids(manager.mBaseGameStack.get()));
    // R24 expands the full native (not atlas-only) reload trial to exactly
    // one disposable local world with two preexisting Level RP entries.
    // We never edit those entries or accept arbitrary offline worlds.
    if(!op.request.reload&&!op.nativeSettings){
        auto multi=op.client->isInMultiplayerGame();
        if(multi||(!op.level.empty()&&!op.r24AuthorizedClone)
            ||(op.r24AuthorizedClone&&op.level.size()!=2)
            ||op.request.selection.order.size()>1
            ||(!op.r24AuthorizedClone&&op.request.selection.order.empty())){
            packDiagnostic("RESOURCEPACK_WORLD event=experimental_preflight_rejected multiplayer=%u level_pack_count=%zu chosen_pack_count=%zu",
                unsigned(multi),op.level.size(),op.request.selection.order.size());
            throw std::runtime_error("R24の構成変更は、複製した指定ワールドのみ許可します。マルチプレイや異なるLevelパック構成では実行できません。");
        }
    }
    bool include=nativeSettingsMayIncludeGlobal(manager.mUseGlobalPackStack,op.client->isInMultiplayerGame(),op.level.empty(),op.r24AuthorizedClone);
    if(!include)throw std::runtime_error("このワールドのGlobal pack制限を維持するため、再入場して通常の管理機能を使用してください。");
    op.settingsContext={address(&op),op.tx.owner.client,op.tx.owner.manager,op.tx.owner.world,include,false};
    if(op.request.reload){
        // Preserve the actual active Full composition as well as every source
        // stack. Active-only reload must not opt additional Global packs in.
        {std::shared_lock lock(manager.mFullStackAccess.get());op.expectedFull=keys(ids(manager.mFullStack.get()));op.expectedFullKnown=true;}
        op.newIds=std::vector<PackInstanceId>(op.oldIds);op.tx.prepared(keys(op.oldIds),keys(op.oldIds));pause(op,titlePackNow());
        packDiagnostic("RESOURCEPACK_WORLD event=prepared token=%llu reload=1 same=1 mask=%u",diagNumber(op.tx.token),op.group.mask);return;
    }
    if(!op.request.catalog)throw std::runtime_error("一覧を読み込んでください。");auto const& catalog=*op.request.catalog;
    auto rootBytes=catalog.root.u8string();std::string root(reinterpret_cast<char const*>(rootBytes.data()),rootBytes.size());
    if(!pathEqual(root,repository.getResourcePacksPath().value)&&!pathEqual(root,repository.getDevelopmentResourcePacksPath().value))throw std::runtime_error("通常 User/Dev の登録済みフォルダを選択してください。");
    auto local=repository.getPacksByResourceLocation(PackOrigin::User);auto dev=repository.getPacksByResourceLocation(PackOrigin::Dev);
    packDiagnostic("RESOURCEPACK_WORLD event=registration_sources token=%llu user=%zu dev=%zu",diagNumber(op.tx.token),local.size(),dev.size());local.insert(local.end(),dev.begin(),dev.end());
    if(local.size()>512)throw std::runtime_error("登録済み pack の確認上限を超えました。");std::vector<PackInstanceId> selected;
    unsigned candidateOrdinal{};
    for(auto const& key:op.request.selection.order){
        auto entry=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key&&p.selectable;});
        if(entry==catalog.packs.end())throw std::runtime_error("候補が古くなりました。一覧を更新してください。");std::shared_ptr<ResourcePack> found;
        std::array<unsigned,5> reasons{};
        for(auto* pack:local){if(!pack){++reasons[1];continue;}unsigned reason{};if(!ordinary(*pack,*entry,&reason)){++reasons[reason];continue;}++reasons[0];auto const& identity=pack->mImpl.get()->mPack.get()->mManifest->mIdentity.get();
            auto owned=repository.getResourcePackSatisfiesPackId(identity,true);if(!owned||owned.get()!=pack||found)throw std::runtime_error("pack の一意な所有権を確認できません。");found=std::move(owned);}
        packDiagnostic("RESOURCEPACK_WORLD event=registration_match token=%llu candidate=%u registered=%zu eligible=%u missing=%u path_rejected=%u identity_rejected=%u protected_rejected=%u owned=%u",
            diagNumber(op.tx.token),candidateOrdinal++,local.size(),reasons[0],reasons[1],reasons[2],reasons[3],reasons[4],unsigned(bool(found)));
        if(!found)throw std::runtime_error("候補に一致する通常の登録済みパックを確認できません。現在の有効表示と診断結果を確認してください。");
        selected.emplace_back(found->mImpl.get()->mPack.get()->mManifest->mIdentity.get(),std::string{});op.ownedPacks.push_back(std::move(found));
    }
    auto merged=mergeTitleGlobal<PackInstanceId>(op.oldIds,selected,[&](auto const& old){
        auto editable=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.selectable&&old.mSubpackName->empty()
            &&(old.mPackId->mPackType==PackType::Resources
                ||(op.r24AuthorizedClone&&!op.nativeSettings
                    &&old.mPackId->mPackType==PackType::Invalid))
            &&old.mPackId->mId->asString()==p.uuid&&old.mPackId->mVersion->asString()==p.version;});
        if(editable!=catalog.packs.end()){auto owned=repository.getResourcePackSatisfiesPackId(old.mPackId.get(),true);
            if(!owned||std::find(local.begin(),local.end(),owned.get())==local.end()||!ordinary(*owned,*editable))throw std::runtime_error("現在の pack は通常の管理機能で変更してください。");op.ownedPacks.push_back(std::move(owned));}
        return editable!=catalog.packs.end();
    },[](auto const& a,auto const& b){return a.mPackId->mId->asString()==b.mPackId->mId->asString();});
    if(!merged||merged->size()>512)throw std::runtime_error("対象外の現在 pack との競合または stack 上限です。");op.newIds=std::move(*merged);
    if(op.r24AuthorizedClone&&!op.nativeSettings){
        // A verified single appended texture pack is the ONLY removable
        // resource in this clone. Never allow arbitrary Global removals,
        // swaps, reorder, or changes to protected origin/level stacks.
        auto oldKeys=keys(op.oldIds),newKeys=keys(op.newIds);
        op.r27ToggleKind=r27NativeToggleShape(
            oldKeys,newKeys,op.request.selection.order.size(),op.ownedPacks.size());
        if(op.r27ToggleKind==R27LocalToggle::Disable){
            // Explicitly validate that the dropped LAST instance is an
            // ordinary local texture pack without a subpack.
            auto const& dropped=op.oldIds.back();
            bool validTail=dropped.mSubpackName->empty()
                &&(dropped.mPackId->mPackType==PackType::Resources
                    ||dropped.mPackId->mPackType==PackType::Invalid);
            if(!validTail)op.r27ToggleKind=R27LocalToggle::Invalid;
        }
        packDiagnostic("RESOURCEPACK_R27 event=toggle_prepared token=%llu direction=%u old=%zu target=%zu local_pins=%zu",
            diagNumber(op.tx.token),unsigned(op.r27ToggleKind),
            oldKeys.size(),newKeys.size(),op.ownedPacks.size());
        if(op.r27ToggleKind!=R27LocalToggle::Enable
            &&op.r27ToggleKind!=R27LocalToggle::Disable)
            throw std::runtime_error("1件のローカルテクスチャをON/OFFする操作以外は、テストワールドで許可していません。");
    }
    auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{Bedrock::NonOwnerPointer<IResourcePackRepository const>(repository)};
    op.candidate=std::make_unique<ResourcePackStack>(op.newIds,repo,false);op.backup=std::make_unique<ResourcePackStack>(op.oldIds,repo,false);
    if(keys(ids(op.candidate.get()))!=keys(op.newIds)||keys(ids(op.backup.get()))!=keys(op.oldIds))throw std::runtime_error("通常 stack 解決が候補または旧構成と一致しません。");
    op.tx.prepared(keys(op.oldIds),keys(op.newIds));pause(op,titlePackNow());
    packDiagnostic("RESOURCEPACK_WORLD event=prepared token=%llu reload=%u old=%zu target=%zu same=%u old_epoch=%llu next_epoch=%llu mask=%u",
        diagNumber(op.tx.token),unsigned(op.request.reload),op.oldIds.size(),op.newIds.size(),unsigned(op.tx.oldGlobal==op.tx.targetGlobal),
        diagNumber(op.tx.participant.oldEpoch),diagNumber(op.tx.participant.nextEpoch),op.group.mask);
}
bool otherStacks(Operation const& op,ResourcePackManager& manager){return keys(ids(manager.mLevelStack.get()))==op.level&&keys(ids(manager.mTreatmentStack.get()))==op.treatment&&keys(ids(manager.mBaseGameStack.get()))==op.base;}
void applyCallback(std::shared_ptr<Operation> const& op,MinecraftScreenModel& model,std::uint64_t peerToken) noexcept {
    packDiagnostic("RESOURCEPACK_WORLD event=apply_callback peer_token=%llu",diagNumber(peerToken));
    if(op->apiReload){
        runWorldSelection(op,model,[&]{
            if(!op->request.reload&&!model.getResourcePackManager().setStack(std::move(op->candidate),ResourcePackStackType::Global))
                throw std::runtime_error("native Global setter rejected");
        });return;
    }
    runWorldApply(lifetime,callbacks,titlePackControl(),op->mutex,op->tx,peerToken,
        [&]{auto owner=op->tx.owner;auto& manager=model.getResourcePackManager();
            owner.present=runtimeReady.load()&&worldGeneration.load()==owner.world&&op->client->getLocalPlayer()
                &&address(op->client->getLevelRenderer())==owner.renderer&&model.mClient.get().get()==op->client.get()
                &&address(&manager)==owner.manager&&GetCurrentThreadId()==owner.thread;
            owner.pending=manager.mPendingRestack;return owner;},
        [&]{bool valid=keys(ids(model.getResourcePackManager().mGlobalStack.get()))==op->tx.expectedBefore&&otherStacks(*op,model.getResourcePackManager());
            packDiagnostic("RESOURCEPACK_WORLD event=pre_set_validation token=%llu accepted=%u",diagNumber(op->tx.token),unsigned(valid));return valid;},
        [&]{bool prepared=ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch);
            packDiagnostic("RESOURCEPACK_WORLD event=pre_set_peers token=%llu accepted=%u epoch=%llu",diagNumber(op->tx.token),unsigned(prepared),diagNumber(op->tx.participant.oldEpoch));return prepared;},
        [&]{auto& manager=model.getResourcePackManager();bool accepted=manager.setStack(op->tx.restoring?std::move(op->backup):std::move(op->candidate),ResourcePackStackType::Global);
            bool unchanged=!accepted&&keys(ids(manager.mGlobalStack.get()))==op->tx.oldGlobal;
            packDiagnostic("RESOURCEPACK_WORLD event=setter token=%llu attempt=%llu accepted=%u unchanged_old=%u",
                diagNumber(op->tx.token),diagNumber(op->tx.attempt),unsigned(accepted),unsigned(unchanged));
            return TitleSetterResult{accepted,unchanged};},titlePackNow());
    std::lock_guard lock(op->mutex);observeOperation(*op,titlePackNow());
}
void navigate(std::shared_ptr<Operation> const& op){
    auto peerToken=op->tx.participant.token;
    packDiagnostic("RESOURCEPACK_WORLD event=navigate token=%llu peer_token=%llu",diagNumber(op->tx.token),diagNumber(peerToken));
    {std::lock_guard lock(op->mutex);op->nativeNavigation=true;op->selectionOnce.token=op->tx.token;op->selectionOnce.world=op->tx.owner.world;}
    auto scene=op->client->getSceneFactory().createApplyResourcePacksScreen([op,peerToken](MinecraftScreenModel& model){applyCallback(op,model,peerToken);});
    if(!scene)throw std::runtime_error("通常の適用画面を作成できません。");op->client->getCurrentSceneStack()->pushScreen(std::move(scene),false);
}
void status(Operation const& op,TitlePackView& view){
    view.busy=op.tx.busy();view.token=op.tx.token;
    auto& observed=view.observation;observed.world=true;observed.reload=op.request.reload;
    observed.attempt=op.tx.attempt;observed.started=op.tx.started;observed.startReturned=op.tx.startReturned;observed.finished=op.tx.finished;
    observed.oldEpoch=op.tx.participant.oldEpoch;observed.nextEpoch=op.tx.participant.nextEpoch;observed.restoring=op.tx.restoring;
    observed.stackChecked=op.tx.phase==WorldSwitchPhase::Reacquiring||op.tx.phase==WorldSwitchPhase::Complete||op.tx.phase==WorldSwitchPhase::Restored;
    auto const& expected=op.tx.restoring?op.tx.oldGlobal:op.tx.targetGlobal;
    observed.stackMatches=observed.stackChecked&&op.tx.lastStable==expected;
    observed.expectedCount=expected.size();observed.observedCount=op.tx.lastStable.size();observed.peers=op.group.observations;
    switch(op.tx.phase){
    case WorldSwitchPhase::Preparing:case WorldSwitchPhase::Pausing:view.phase=TitlePackPhase::Preparing;view.status="プレビューと地図の処理が終わるのを待っています。";break;
    case WorldSwitchPhase::AwaitApply:view.phase=TitlePackPhase::AwaitApply;view.status="ゲームの通常の適用画面を待っています。";break;
    case WorldSwitchPhase::Reloading:view.phase=op.tx.restoring?TitlePackPhase::RollingBack:TitlePackPhase::Reloading;
        view.status=op.tx.restoring?"旧構成を通常の適用経路で再読込しています。":"ゲームがリソースを再読込しています。";
        if(op.apiReload&&op.selectionCommitted&&!op.tx.started&&op.composeAttempts>50)
            view.status="MinecraftのFull Stack再構成が保留中です。描画更新はまだ開始されていません。";
        break;
    case WorldSwitchPhase::Reacquiring:view.phase=TitlePackPhase::Reloading;view.status="プレビューと地図を再生成しています。";break;
    case WorldSwitchPhase::Complete:view.phase=TitlePackPhase::Complete;view.status="構成の一致とプレビュー・地図の再開を確認しました。見た目は変更対象を実際に確認してください。";break;
    case WorldSwitchPhase::Restored:view.phase=TitlePackPhase::Restored;view.status="旧構成への復帰と再開を確認しました。";break;
    case WorldSwitchPhase::Cancelled:view.phase=TitlePackPhase::Rejected;view.status=op.tx.setterRejected?"ゲームが適用を受理しませんでした。旧構成を維持しています。":"変更開始前に取消しました。";break;
    case WorldSwitchPhase::Recovery:
        view.phase=TitlePackPhase::RecoveryRequired;
        view.status="表示が変わっていても、Native再読み込みとLHoloの復帰は未確認です。操作を続けずMinecraftを終了し、バックアップから復元してください。";
        view.available=false;view.canReload=false;view.block=PackApplyBlock::Provenance;break;
    default:break;
    }
    if(!op.detail.empty())view.status=op.detail;
}
// Stage the candidate itself, NOT a sample from the *active* Full RP stack.
// This first phase deliberately does not call setStack, composeFullStack,
// TextureAtlas, BlockGraphics, or any GPU write.
void stageInactiveLocalTexture(MinecraftGame& game,std::shared_ptr<IClientInstance> const& client) noexcept {
    auto request=localOverlayStageControl().take();
    if(!request)return;
    LocalOverlayPngMetadata meta{};
    std::string imageBytes;
    LocalPngDecodedRGBA decoded;
    std::string reason="候補の確認または画像読込に失敗しました。";
    try {
        auto& repository=client->getResourcePackRepository();
        auto rootBytes=request->catalog->root.u8string();
        std::string root(reinterpret_cast<char const*>(rootBytes.data()),rootBytes.size());
        if(!pathEqual(root,repository.getResourcePacksPath().value)
            &&!pathEqual(root,repository.getDevelopmentResourcePacksPath().value))
            throw std::runtime_error("Minecraftが登録したUser/Devパック以外は対象にできません。");
        auto const& catalog=*request->catalog;
        auto selected=std::find_if(catalog.packs.begin(),catalog.packs.end(),
            [&](Pack const& item){return item.key==request->key&&item.selectable;});
        if(selected==catalog.packs.end())throw std::runtime_error("選択した候補が更新されています。");
        auto packs=repository.getPacksByResourceLocation(PackOrigin::User);
        auto dev=repository.getPacksByResourceLocation(PackOrigin::Dev);
        packs.insert(packs.end(),dev.begin(),dev.end());
        if(packs.size()>512)throw std::runtime_error("登録済みパック数が上限を超えました。");
        std::shared_ptr<ResourcePack> owned;
        for(auto* p:packs) {
            if(!p||!ordinary(*p,*selected))continue;
            auto const& identity=p->mImpl.get()->mPack.get()->mManifest->mIdentity.get();
            auto resolved=repository.getResourcePackSatisfiesPackId(identity,true);
            if(!resolved||resolved.get()!=p||owned)
                throw std::runtime_error("候補パックの所有権または一意性を確認できません。");
            owned=std::move(resolved);
        }
        if(!owned)throw std::runtime_error("Minecraft登録済みの通常ローカルパックが見つかりません。");
        auto const& identity=owned->mImpl.get()->mPack.get()->mManifest->mIdentity.get();
        auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
            Bedrock::NonOwnerPointer<IResourcePackRepository const>(repository)};
        std::vector<PackInstanceId> onlySelected;
        onlySelected.emplace_back(identity,std::string{});
        ResourcePackStack candidate(onlySelected,repo,false);
        if(candidate.mStack->size()!=1)
            throw std::runtime_error("候補を単独で読み込めません。");
        if(!candidate.mStack->front().getResource(Core::Path{"textures/blocks/powder_snow.png"},imageBytes))
            throw std::runtime_error("選択パックに粉雪テクスチャがありません。");
        meta=localOverlayPngMetadata(imageBytes);
        if(!meta.valid)throw std::runtime_error("PNG画像のサイズまたは形式が非対応です。");
        decoded=decodeLocalPngWic(imageBytes);
        if(!decoded.valid()||decoded.width!=meta.width||decoded.height!=meta.height)
            throw std::runtime_error("WindowsのPNG画像デコードに失敗しました。");
        reason="事前読込成功";
    }catch(std::exception const& e){reason=e.what();}
    catch(...){reason="画像の事前読込に失敗しました。";}
    auto success=decoded.valid()&&meta.valid;
    packDiagnostic("RESOURCEPACK_LOCAL event=inactive_candidate_stage success=%u bytes=%zu width=%u height=%u cpu_rgba_decoded=%u stack_changed=0 atlas_changed=0",
        unsigned(success),imageBytes.size(),meta.width,meta.height,unsigned(decoded.valid()));
    localOverlayStageControl().complete(request->token,std::move(imageBytes),
        meta,std::move(decoded.pixels),success,std::move(reason));
}
void tick(MinecraftGame& game) noexcept {
    if(!installed.load()||!game.isInGame()||titlePackControl().stopped()||ticking.test_and_set())return;
    struct Exit{~Exit(){ticking.clear();drainPackDiagnostics();}} exit;
    observedGame.store(&game);
    TitlePackView view;view.inWorld=true;view.canReload=true;view.revision=revision.load();view.until=titlePackNow()+1000;
    try{
        auto previous=titlePackControl().view(titlePackNow());auto op=active.load();
        if(previous.phase==TitlePackPhase::RecoveryRequired&&!op)return;
        auto current=frame(game);bool bindingReady=resolveLholo();
        if(!active.load()&&current.owner.usable()&&current.normal&&!callbacks.load()
            &&current.client&&!current.client->getResourcePackManager().mPendingRestack)
            stageInactiveLocalTexture(game,current.client);
        // Read-only native terrain atlas ownership/UV proof after the candidate
        // PNG has been decoded in the current game world. A proof is NOT an
        // instruction to write GPU memory or bypass resource-pack composition.
        if(current.owner.usable()&&current.normal&&!callbacks.load()&&current.client
            &&!current.client->getResourcePackManager().mPendingRestack){
            auto stage=localOverlayStageControl().view();
            static std::uint64_t probedToken{},probedWorld{};
            if(stage.phase==LocalOverlayStagePhase::Decoded&&stage.cpuImageValid
                &&(probedToken!=stage.token||probedWorld!=current.owner.world)){
                probedToken=stage.token;probedWorld=current.owner.world;
                LocalTerrainAtlasProof proof{};
                proof.stagedToken=stage.token;proof.world=current.owner.world;
                proof.candidateW=stage.width;proof.candidateH=stage.height;
                try {
                    auto atlasOwner=game.getTextureAtlas();
                    auto* atlas=atlasOwner.get().get();
                    auto group=game.getTextureGroup();
                    auto terrain=BlockGraphics::mTerrainTextureAtlas().lock();
                    proof.gameAtlas=bool(atlas);
                    proof.textureGroup=bool(group);
                    proof.terrainManager=bool(terrain);
                    proof.sameAtlasManager=bool(atlas)&&bool(terrain)
                        &&atlas->mAtlasItemManager.get()==terrain.get();
                    auto const& uv=BlockGraphics::getTextureUVCoordinateSet("powder_snow",0,0);
                    proof.u0=uv._u0;proof.v0=uv._v0;proof.u1=uv._u1;proof.v1=uv._v1;
                    proof.uvSourceW=uv._sourceImageWidth;proof.uvSourceH=uv._sourceImageHeight;
                    proof.uvTexW=uv._texSizeW;proof.uvTexH=uv._texSizeH;
                    auto location=uv.sourceFileLocation->getFullPath().value;
                    proof.sourcePathPowder=location.find("powder_snow")!=std::string::npos;
                    proof.uvFinite=std::isfinite(proof.u0)&&std::isfinite(proof.v0)
                        &&std::isfinite(proof.u1)&&std::isfinite(proof.v1);
                    proof.uvInsideAtlas=proof.u0>=0.f&&proof.v0>=0.f
                        &&proof.u1<=1.f&&proof.v1<=1.f;
                    proof.uvAreaNonzero=proof.u0<proof.u1&&proof.v0<proof.v1;
                    proof.sourceDimensionsMatch=proof.uvSourceW==stage.width
                        &&proof.uvSourceH==stage.height;
                }catch(...){ }
                localTerrainAtlasProofControl().publish(proof);
                packDiagnostic("RESOURCEPACK_LOCAL event=terrain_atlas_owner_probe token=%llu atlas=%u terrain_manager=%u same_manager=%u texture_group=%u source_powder=%u uv_finite=%u uv_range=%u source_dimensions_match=%u uv_nonzero=%u candidate=%ux%u uv_source=%ux%u uv_tex=%ux%u",
                    diagNumber(proof.stagedToken),unsigned(proof.gameAtlas),
                    unsigned(proof.terrainManager),unsigned(proof.sameAtlasManager),
                    unsigned(proof.textureGroup),unsigned(proof.sourcePathPowder),
                    unsigned(proof.uvFinite),unsigned(proof.uvInsideAtlas),
                    unsigned(proof.sourceDimensionsMatch),unsigned(proof.uvAreaNonzero),
                    proof.candidateW,proof.candidateH,proof.uvSourceW,proof.uvSourceH,
                    proof.uvTexW,proof.uvTexH);
            }
        }
        observedClient.store(address(current.client.get()));observedWorld.store(current.owner.world);
        // Same admission predicates as before; diagnostics do not authorize work.
        bool imageReady=resourcePackImageVerified();
        if(imageReady&&current.owner.present&&current.owner.thread==GetCurrentThreadId()
            &&!completionHookAttempted.load())installWorldNativeCompletionObservers(current.owner.thread);
        auto callbackCount=callbacks.load();bool provenanceLost=lost.load();
        view.available=false;
        view.block=!imageReady?PackApplyBlock::Version:!current.owner.usable()?(current.block==PackApplyBlock::None?PackApplyBlock::Owner:current.block):!current.normal?PackApplyBlock::Screen:
            callbackCount?PackApplyBlock::Callback:provenanceLost?PackApplyBlock::Provenance:!bindingReady?PackApplyBlock::Peer:
            retained.size()>=OperationBudget?PackApplyBlock::History:PackApplyBlock::None;
        view.available=view.block==PackApplyBlock::None;
        static PackApplyBlock loggedBlock{PackApplyBlock::Unknown};
        if(loggedBlock!=view.block){loggedBlock=view.block;
            packDiagnostic("RESOURCEPACK_WORLD event=admission reason=%u available=%u callbacks=%u pending=%u normal=%u ready=%u",
                unsigned(view.block),unsigned(view.available),callbackCount,unsigned(current.owner.pending),unsigned(current.normal),unsigned(current.owner.ready));
        }
        if(current.owner.usable()&&current.normal&&!callbacks.load()){
            auto& manager=current.client->getResourcePackManager();view.current=viewIds(ids(manager.mGlobalStack.get()));auto& repo=current.client->getResourcePackRepository();
            view.folders={repo.getResourcePacksPath().value,repo.getDevelopmentResourcePacksPath().value};
            ServerResourcePackSnapshot facts{
                current.client->isInMultiplayerGame(),bool(manager.mUseGlobalPackStack),
                bool(manager.mPendingRestack),
                manager.mLevelStack.get()?manager.mLevelStack->mStack->size():0,
                manager.mGlobalStack.get()?manager.mGlobalStack->mStack->size():0,
                manager.mTreatmentStack.get()?manager.mTreatmentStack->mStack->size():0,
                manager.mBaseGameStack.get()?manager.mBaseGameStack->mStack->size():0,0};
            {
                std::shared_lock lock(manager.mFullStackAccess.get());
                facts.full=manager.mFullStack.get()?manager.mFullStack->mStack->size():0;
            }
            view.serverPackContext=facts.serverOrWorldResourceContext();
            view.r24AuthorizedClone=authorizedR24Clone(current.client);
            static std::uint64_t r24LastWorldReported{};
            if(r24LastWorldReported!=current.owner.world){
                r24LastWorldReported=current.owner.world;
                bool idMatch{},nameMatch{};
                try {
                    if(auto* level=current.client->getLevel()){
                        idMatch=level->getLevelId()==R24CloneDirectory;
                        nameMatch=level->getLevelData().mLevelName.get()==R24CloneDisplayName;
                    }
                }catch(...){}
                packDiagnostic("RESOURCEPACK_R24 event=clone_gate id_match=%u name_match=%u global=%u level=%zu multiplayer=%u allowed=%u",
                    unsigned(idMatch),unsigned(nameMatch),unsigned(facts.useGlobal),
                    facts.level,unsigned(facts.multiplayer),unsigned(view.r24AuthorizedClone));
            }
            view.serverMultiplayer=facts.multiplayer;
            view.nativeGlobalIncluded=facts.useGlobal;
            view.nativeLevelPackCount=facts.level;
            view.nativeFullPackCount=facts.full;
            // A resolver lookup is sampled once per world+stack shape, never
            // on each frame or on a pending restack. It does not modify the
            // global/server packs, GPU, native listener, or resource lifetime.
            static std::uint64_t lastProbeWorld{};
            static std::array<std::size_t,6> lastShape{};
            auto shape=std::array<std::size_t,6>{facts.level,facts.global,facts.treatment,
                facts.base,facts.full,std::size_t(facts.useGlobal)};
            if(view.serverPackContext&&!facts.pending&&facts.full
                &&(lastProbeWorld!=current.owner.world||lastShape!=shape)){
                lastProbeWorld=current.owner.world;lastShape=shape;
                ServerAssetLayer sample=ServerAssetLayer::Unknown;
                int sampleIndex=-1;
                try {
                    auto level=keys(ids(manager.mLevelStack.get()));
                    auto global=keys(ids(manager.mGlobalStack.get()));
                    auto treatment=keys(ids(manager.mTreatmentStack.get()));
                    auto base=keys(ids(manager.mBaseGameStack.get()));
                    std::vector<std::string> full;
                    {std::shared_lock lock(manager.mFullStackAccess.get());full=keys(ids(manager.mFullStack.get()));}
                    std::array<std::string,1> extensions{"png"};
                    ResourceLocation location(Core::PathView{"textures/blocks/powder_snow"},ResourceFileSystem::UserPackage);
                    sampleIndex=manager.getPackStackIndexOfResource(location,
                        gsl::span<std::string const>(extensions.data(),extensions.size())).first;
                    sample=serverResourceAssetLayer(full,level,global,treatment,base,sampleIndex);
                }catch(...){sampleIndex=-2;sample=ServerAssetLayer::Unknown;}
                packDiagnostic("RESOURCEPACK_SERVER event=resource_origin version=1 multiplayer=%u level=%zu global=%zu treatment=%zu base=%zu full=%zu use_global=%u pending=%u sample_found=%u sample_layer=%u",
                    unsigned(facts.multiplayer),facts.level,facts.global,facts.treatment,
                    facts.base,facts.full,unsigned(facts.useGlobal),unsigned(facts.pending),
                    unsigned(sampleIndex>=0),unsigned(sample));
                // Store only an anonymous enum; never a server path, domain,
                // private pack identifier or texture data.
                lastObservedServerSource=sample;
            }
            view.powderSnowSource=lastObservedServerSource;
            // One-shot baseline from the actual gameplay camera, not the
            // applying screen. The user can switch F5 before entering the
            // vanilla resource editor. No actor/render pipeline is modified.
            static std::uint64_t r33LastBaselineWorld{},r33BaselineNextAt{};
            static unsigned r33BaselineCount{};
            if(view.r24AuthorizedClone&&!op){
                if(r33LastBaselineWorld!=current.owner.world){
                    r33LastBaselineWorld=current.owner.world;
                    r33BaselineCount=0;
                    r33BaselineNextAt=0;
                }
                auto now=titlePackNow();
                if(r33BaselineCount<8&&now>=r33BaselineNextAt){
                    r33BaselineNextAt=now+650;
                    ++r33BaselineCount;
                    try {
                        packDiagnostic("RESOURCEPACK_R33 event=player_render_baseline seq=%u perspective=%d model=%u skin_initialized=%u platform_skin_valid=%u",
                            r33BaselineCount,current.client->getOptions().getPlayerViewPerspective(),
                            unsigned(current.client->getRenderPlayerModel()),
                            unsigned(current.client->isSelectedSkinInitialized()),
                            unsigned(observedGame.load()&&observedGame.load()->hasAllValidCrossPlatformSkin()));
                    }catch(...){packDiagnostic("RESOURCEPACK_R33 event=player_render_baseline unavailable=1");}
                }
            }
        }
        view.status=bindingReady?"ローカルtextureの候補を選んで適用できます。現在の有効構成だけを再読込する操作も使用できます。":"LHoloの再読込bridgeを確認できません。";
        if(view.serverPackContext && !view.r24AuthorizedClone){
            view.available=false;
            view.canReload=false;
            view.block=PackApplyBlock::ServerPack;
            view.status="サーバー／ワールド側の専用リソースパックを保持しています。構成変更は指定の複製テストワールド以外では禁止です。";
        }else if(view.serverPackContext){
            view.status="複製ワールドでのみ、Global RPとFull Stackの再構成を試験できます。";
        }
        // Preserve the test-mode UI while the native applying screen is on
        // top. This is presentation only; actual apply admission is checked
        // afresh by the engine owner for every request.
        if(op&&op->r24AuthorizedClone&&op->client==current.client
            &&current.owner.world==op->tx.owner.world)
            view.r24AuthorizedClone=true;
        if(op){
            // After the *real* Full Stack reload, invalidate old section meshes
            // exactly once. Atlas texture completion alone does not retessellate
            // blocks that may carry different UV/material state.
            bool rebuildR24{};
            {
                std::lock_guard lock(op->mutex);
                if(op->r24AuthorizedClone&&op->apiReload
                    &&op->tx.phase==WorldSwitchPhase::Reloading
                    &&op->engine.applicationReady()
                    &&!op->tx.geometryRequested
                    &&current.owner.usable()&&current.normal
                    &&current.owner.game==op->tx.owner.game
                    &&current.owner.world==op->tx.owner.world
                    &&current.owner.manager==op->tx.owner.manager){
                    op->tx.geometryRequested=true;
                    rebuildR24=true;
                }
            }
            if(rebuildR24) {
                try {
                    // Full Stack/UI/texture reload is complete, but R30 did
                    // not notify the client entity renderer to rebuild its
                    // player/avatar graphics. A restart repairs those caches.
                    // Use the SDK's public actor renderer API exactly ONCE,
                    // only while both Praxis and LHolo still hold NativeActive
                    // and before peer epoch change/reacquisition. This never
                    // modifies world entities, skins, pack metadata or
                    // protected server RP policy.
                    R31AvatarRebindGate avatar{};
                    auto* game=observedGame.load();
                    {
                        std::lock_guard lock(op->mutex);
                        avatar.offlineClone=op->r24AuthorizedClone;
                        avatar.vanillaSettings=op->nativeSettings;
                        avatar.apiReload=op->apiReload;
                        avatar.nativeLoading=op->tx.phase==WorldSwitchPhase::Reloading;
                        avatar.applicationReady=op->engine.applicationReady();
                        avatar.frameOwner=game&&current.owner.usable()
                            &&op->tx.owner.world==current.owner.world
                            &&op->tx.owner.client==current.owner.client
                            &&op->tx.owner.manager==current.owner.manager
                            &&op->tx.owner.game==address(game)
                            &&GetCurrentThreadId()==ownerThread.load();
                        avatar.gameplayScreen=current.normal;
                        avatar.gameReady=game&&game->isReadyToRender()
                            &&!game->isShowingLoadingScreen();
                        avatar.noRestack=!current.client->getResourcePackManager().mPendingRestack;
                        avatar.actorDefinitions=game&&game->hasActorResourceDefinitionGroup();
                        avatar.firstAttempt=!op->r31AvatarRebindAttempted;
                        // Query the actual participant ABI; no optimistic
                        // "prepared" flag or bypass of a GPU owner receipt.
                        if(avatar.offlineClone&&avatar.vanillaSettings
                            &&avatar.apiReload&&avatar.nativeLoading
                            &&avatar.applicationReady&&avatar.firstAttempt)
                            avatar.peersNativeActive=op->group.nativeActive(op->tx);
                        op->r31AvatarRebindAttempted=true;
                    }
                    auto denied=r31AvatarRebindMissing(avatar);
                    bool returned{},resourceNotificationReturned{},geometryReloadReturned{};
                    bool skinBefore{},skinAfter{},renderModelBefore{},renderModelAfter{},skinCrossPlatform{};
                    int perspectiveBefore=-1,perspectiveAfter=-1;
                    if(!denied){
                        skinBefore=current.client->isSelectedSkinInitialized();
                        renderModelBefore=current.client->getRenderPlayerModel();
                        skinCrossPlatform=game->hasAllValidCrossPlatformSkin();
                        perspectiveBefore=current.client->getOptions().getPlayerViewPerspective();
                        // R33: a complete pack reload reconstructs terrain
                        // but not necessarily the local player's animation and
                        // geometry group that the active skin renderer uses.
                        // The game SDK exposes a dedicated synchronous reload
                        // of animation/geometry definitions. It must run
                        // inside the same verified NativeActive peer scope,
                        // before the per-client actor resource callback.
                        game->reloadAnimationAndGeometryData(true);
                        geometryReloadReturned=true;
                        // R31 only rebuilt an actor renderer; this skips the
                        // ClientInstance resource-definition refresh normally
                        // associated with pack changes. R32 notifies the
                        // existing local client FIRST and rebuilds renderers
                        // only AFTER the actor-definition callback returned.
                        // Both methods are SDK-backed virtual functions; no
                        // patching, mutable player/skin fields or offsets.
                        current.client->onActiveResourcePacksChanged(
                            game->getActorResourceDefinitionGroupMutable());
                        resourceNotificationReturned=true;
                        current.client->reloadEntityRenderers(
                            game->getActorResourceDefinitionGroupMutable());
                        skinAfter=current.client->isSelectedSkinInitialized();
                        renderModelAfter=current.client->getRenderPlayerModel();
                        perspectiveAfter=current.client->getOptions().getPlayerViewPerspective();
                        returned=true;
                        std::lock_guard lock(op->mutex);
                        op->r31AvatarRebindReturned=true;
                    }
                    packDiagnostic("RESOURCEPACK_R33 event=avatar_geometry_reload token=%llu missing_mask=%u geometry_reloaded=%u perspective_before=%d perspective_after=%d notified=%u rebuilt=%u visual_verified=0",
                        diagNumber(op->tx.token),denied,
                        unsigned(geometryReloadReturned),perspectiveBefore,perspectiveAfter,
                        unsigned(resourceNotificationReturned),unsigned(returned));
                    packDiagnostic("RESOURCEPACK_R32 event=client_pack_resources token=%llu missing_mask=%u notified=%u rebuilt=%u skin_valid_before=%u skin_valid_after=%u platform_skins=%u render_model_before=%u render_model_after=%u visual_verified=0",
                        diagNumber(op->tx.token),denied,
                        unsigned(resourceNotificationReturned),unsigned(returned),
                        unsigned(skinBefore),unsigned(skinAfter),
                        unsigned(skinCrossPlatform),
                        unsigned(renderModelBefore),unsigned(renderModelAfter));
                    packDiagnostic("RESOURCEPACK_R31 event=player_entity_rebind token=%llu missing_mask=%u native_returned=%u skin_before=%u skin_after=%u model_before=%u model_after=%u visual_verified=0",
                        diagNumber(op->tx.token),denied,unsigned(returned),
                        unsigned(skinBefore),unsigned(skinAfter),
                        unsigned(renderModelBefore),unsigned(renderModelAfter));
                    current.client->getLevelRenderer()->rebuildAllRenderChunkGeometry();
                    packDiagnostic("RESOURCEPACK_R24 event=full_reload_chunk_rebuild_requested native_application=1");
                }catch(...){
                    std::lock_guard lock(op->mutex);
                    op->engine.seal();
                    op->tx.seal();
                    packDiagnostic("RESOURCEPACK_R31 event=player_entity_rebind_failed token=%llu recovery=1",
                        diagNumber(op->tx.token));
                }
            }
            // Observe the real perspective and render-player flag after the
            // Settings screen has closed and both participants released their
            // old resource epochs. This is an observation, NOT proof of pixels.
            bool r33Sample{};
            unsigned r33SampleSeq{};
            std::uint64_t r33SampleToken{};
            {
                std::lock_guard lock(op->mutex);
                auto now=titlePackNow();
                if(op->r24AuthorizedClone&&op->nativeSettings
                    &&op->tx.phase==WorldSwitchPhase::Complete
                    &&current.owner.usable()&&current.normal
                    &&op->r33PerspectiveSampleCount<16
                    &&now>=op->r33NextPerspectiveSampleAt){
                    r33SampleSeq=++op->r33PerspectiveSampleCount;
                    r33SampleToken=op->tx.token;
                    op->r33NextPerspectiveSampleAt=now+500;
                    r33Sample=true;
                }
            }
            if(r33Sample){
                try {
                    packDiagnostic("RESOURCEPACK_R33 event=player_render_post token=%llu seq=%u perspective=%d model=%u skin_initialized=%u platform_skin_valid=%u screen_normal=%u",
                        diagNumber(r33SampleToken),r33SampleSeq,
                        current.client->getOptions().getPlayerViewPerspective(),
                        unsigned(current.client->getRenderPlayerModel()),
                        unsigned(current.client->isSelectedSkinInitialized()),
                        unsigned(observedGame.load()&&observedGame.load()->hasAllValidCrossPlatformSkin()),
                        unsigned(current.normal));
                }catch(...){packDiagnostic("RESOURCEPACK_R33 event=player_render_post failure=1");}
            }
            bool navigateNow=false,replayNow=false;
            NativeSettingsPendingHolds staleExit;
            std::shared_ptr<MainMenuScreenModel> staleModel;
            {std::lock_guard lock(op->mutex);
                if(lost.load()||!bindingReady)op->tx.seal();
                if(op->nativeSettings&&op->pendingExit){
                    if(!op->tx.beforeNative())staleExit=takePendingSettingsExit(*op);
                    else if(!settingsExitCurrent(*op,current.owner)){
                        staleExit=takePendingSettingsExit(*op);op->tx.cancel();
                    }
                }
                if(!op->nativeSettings&&titlePackControl().cancelled(op->tx.token))op->tx.cancel();
                // A queued native callback still owns its selection. Timeout
                // cannot resume old borrowers while that callback may run.
                if(op->nativeSettings&&op->nativeNavigation&&op->tx.phase==WorldSwitchPhase::AwaitApply&&titlePackNow()>op->tx.deadline)op->tx.seal();
                if(op->nativeSettings&&op->exitReplayed&&!op->nativeNavigation
                    &&op->tx.phase==WorldSwitchPhase::AwaitApply&&!op->tx.awaitingSelection
                    &&titlePackNow()>op->tx.deadline)op->tx.seal();
                // The API route requires actual engine finish, UI reload and
                // a later current-renderer world draw before peer reacquisition.
                if(op->apiReload&&(op->tx.phase==WorldSwitchPhase::Reloading||op->tx.phase==WorldSwitchPhase::Reacquiring)){
                    auto const& old=op->tx.owner;
                    if(op->tx.phase==WorldSwitchPhase::Reloading)op->engine.requireCurrentFrame(current.owner.renderer);
                    if(titlePackNow()>op->tx.deadline||current.owner.game!=old.game||current.owner.manager!=old.manager
                        ||current.owner.client!=old.client||current.owner.world!=old.world||current.owner.thread!=old.thread)op->tx.seal();
                    else if(op->tx.phase==WorldSwitchPhase::Reloading&&op->engine.frameReady()&&current.owner.usable()){
                        auto priorRenderer=op->tx.owner.renderer;
                        if(op->tx.settleExecuted(current.owner,true,otherStacks(*op,current.client->getResourcePackManager()),
                            keys(ids(current.client->getResourcePackManager().mGlobalStack.get())),titlePackNow()))
                            packDiagnostic("RESOURCEPACK_WORLD event=native_peer_retarget route=api_apply token=%llu attempt=%llu old_renderer=%llu current_renderer=%llu",
                                diagNumber(op->tx.token),diagNumber(op->tx.attempt),diagNumber(priorRenderer),diagNumber(current.owner.renderer));
                    }
                }else if(op->tx.busy())op->tx.poll(current.owner,!callbacks.load()&&!nativeTasks.load(),current.normal,
                    current.owner.usable()?otherStacks(*op,current.client->getResourcePackManager()):true,
                    current.owner.usable()?keys(ids(current.client->getResourcePackManager().mGlobalStack.get())):std::vector<std::string>{},titlePackNow());
                if(!op->apiReload&&op->tx.phase==WorldSwitchPhase::Reacquiring&&!op->tx.geometryRequested)
                    requestWorldGeometryRebuild(op->tx,current.owner,current.normal,[&]{
                        current.client->getLevelRenderer()->rebuildAllRenderChunkGeometry();
                    });
                if(op->nativeSettings&&op->tx.phase==WorldSwitchPhase::Reacquiring&&op->tx.needRollback)op->tx.seal();
                auto action=op->group.step(op->tx,titlePackNow(),worldPeerStepPolicy(op->apiReload,op->nativeSettings));
                if(action==WorldPeerAction::Navigate&&!op->nativeSettings)navigateNow=true;
                if(action==WorldPeerAction::Navigate&&op->nativeSettings&&op->pendingExit)replayNow=true;
                if(action==WorldPeerAction::Rollback){op->tx.rollback(keys(ids(current.client->getResourcePackManager().mGlobalStack.get())),titlePackNow());pause(*op,titlePackNow());}
                if(op->nativeSettings&&op->tx.phase==WorldSwitchPhase::Cancelled){
                    // poll() may cancel after the pending-exit lifetime check.
                    // Release its owned references before verifying the live editor.
                    if(op->pendingExit)staleExit=takePendingSettingsExit(*op);
                    resetSettingsCancelledForRetry(*op,current.owner);
                }
                if(op->apiReload&&!op->tx.busy()&&op->tx.phase!=WorldSwitchPhase::Idle&&op->tx.phase!=WorldSwitchPhase::Recovery)staleModel=std::move(op->completionModel);
                if(op->tx.busy()||op->group.mask||op->loggedPhase!=op->tx.phase)observeOperation(*op,titlePackNow());

            }
            if(navigateNow)navigate(op);
            if(replayNow)replaySettingsExit(op);
        }
        bool rejectedRequest=false;
        if(auto request=titlePackControl().take()){
            packDiagnostic("RESOURCEPACK_WORLD event=request_taken token=%llu reload=%u revision_match=%u available=%u count=%zu",
                diagNumber(request->token),unsigned(request->reload),unsigned(request->revision==revision.load()),unsigned(view.available),request->selection.order.size());
            bool canBegin=!op; if(op){std::lock_guard lock(op->mutex);canBegin=!op->tx.busy()&&op->tx.phase!=WorldSwitchPhase::Recovery&&!op->group.mask;}
            if(canBegin&&view.available&&request->revision==revision.load()){
                op=std::make_shared<Operation>();op->apiReload=true;op->request=std::move(*request);op->client=current.client;
                op->r24AuthorizedClone=authorizedR24Clone(current.client);
                packDiagnostic("RESOURCEPACK_R24 event=transaction_world_authorized clone=%u level_count=%zu",
                    unsigned(op->r24AuthorizedClone),ids(current.client->getResourcePackManager().mLevelStack.get()).size());
                if(binding.owner){op->group.peers[1]=binding.entry;op->group.count=2;}
                op->tx.begin(op->request.token,current.owner,titlePackNow());active.store(op);retained.push_back(op);
                try{prepare(*op,game);}catch(std::exception const& error){op->tx.cancel();op->detail=error.what();
                    packDiagnostic("RESOURCEPACK_WORLD event=prepare_rejected token=%llu phase=%u",diagNumber(op->tx.token),unsigned(op->tx.phase));}
                observeOperation(*op,titlePackNow());
            }else{rejectedRequest=true;view.token=request->token;view.observation={};view.phase=TitlePackPhase::Rejected;view.status="現在状態が変わったため、適用を開始していません。";
                packDiagnostic("RESOURCEPACK_WORLD event=request_rejected token=%llu reason=%u can_begin=%u revision_match=%u",
                    diagNumber(request->token),unsigned(view.block),unsigned(canBegin),unsigned(request->revision==revision.load()));}
        }
        if(op&&!rejectedRequest){std::lock_guard lock(op->mutex);status(*op,view);if(op->group.mask&&op->tx.phase==WorldSwitchPhase::Cancelled)view.busy=true;}
        if(retained.size()>=OperationBudget){view.available=false;view.block=PackApplyBlock::History;view.status="この起動中の適用履歴の上限（16回）です。再起動後に適用してください。";}
        if(view.busy)view.available=false;titlePackControl().publish(std::move(view));
    }catch(...){if(auto op=active.load()){std::lock_guard lock(op->mutex);op->tx.seal();stopPeers(*op);}view.available=false;view.block=PackApplyBlock::Provenance;view.current={};view.status="現在状態の確認に失敗しました。旧状態を保持して停止しています。";packDiagnostic("RESOURCEPACK_WORLD event=observation_exception retained=1");titlePackControl().publish(std::move(view));}
}
#include "resourcepacks/NativeSettingsPackHooks.h"
#include "resourcepacks/WorldEngineReloadDriver.h"

// Observe the game's real terrain-atlas completion callback. Calling the
// game's exported queue routine is not proof that any pixels reached the GPU;
// only the completion hook + subsequent game frame may advance the UI state.
// Only during a formally owned local texture-pack transaction's native
// listener call, present a not-in-world answer to the specific Game virtual
// query that otherwise refuses to enter its own resource/task invalidation.
LL_TYPE_INSTANCE_HOOK(WorldNativeIsInGameCompatibilityHook,ll::memory::HookPriority::Normal,
    MinecraftGame,&MinecraftGame::$isInGame,bool){
    if(worldCompatibilityMaskDepth && observedGame.load()==this
        &&GetCurrentThreadId()==ownerThread.load()) {
        ++worldCompatibilityQueryHits;
        return false;
    }
    return origin();
}
LL_TYPE_INSTANCE_HOOK(WorldFullStackInvalidHook,ll::memory::HookPriority::Normal,
    MinecraftGame,&MinecraftGame::$onFullPackStackInvalid,bool){
    NativeHookLifetime::Lease lease(lifetime);
    auto* game=observedGame.load();
    auto op=active.load();
    bool inspect=lease.admitted()&&game&&GetCurrentThreadId()==ownerThread.load();
    int loadFlag=-1;
    unsigned inGame=2,leaveDone=2;
    bool limited=false;
    if(inspect)try{
        auto manager=game->getResourceLoadManager();
        loadFlag=int(*reinterpret_cast<unsigned char const*>(&manager->mUnk8953d0));
        inGame=unsigned(game->isInGame());
        leaveDone=unsigned(game->isLeaveGameDone());
        if(op && op->apiReload && !op->request.reload && (!op->nativeSettings || op->r24AuthorizedClone)) {
            std::lock_guard lock(op->mutex);
            auto& rpm=op->client->getResourcePackManager();
            auto inherited=worldNativeContext;
            WorldNativeCompatibilityAdmission test{};
            test.installed=installed.load();
            test.ownerThread=GetCurrentThreadId()==op->tx.owner.thread;
            test.sameGame=op->tx.owner.game==address(game);
            test.sameWorld=op->tx.owner.world==worldGeneration.load();
            test.sameManager=op->tx.owner.manager==address(&rpm);
            test.identity=inherited.seed()&&!inherited.cycle&&inherited.token==op->tx.token
                &&inherited.attempt==op->tx.attempt&&inherited.owner.same(op->tx.owner);
            test.initial=op->tx.phase==WorldSwitchPhase::Reloading&&op->selectionCommitted
                &&!op->tx.started&&op->expectedFullKnown;
            test.pending=bool(rpm.mPendingRestack)&&!rpm.mInitializing;
            test.singleLocalAppend=op->ownedPacks.size()==1
                &&op->r27ToggleKind==R27LocalToggle::Enable
                &&op->tx.targetGlobal.size()==op->tx.oldGlobal.size()+1
                &&std::equal(op->tx.oldGlobal.begin(),op->tx.oldGlobal.end(),
                    op->tx.targetGlobal.begin());
            test.singleLocalRemove=op->r24AuthorizedClone
                &&op->r27ToggleKind==R27LocalToggle::Disable
                &&op->ownedPacks.size()==1
                &&op->tx.oldGlobal.size()==op->tx.targetGlobal.size()+1
                &&std::equal(op->tx.targetGlobal.begin(),op->tx.targetGlobal.end(),
                    op->tx.oldGlobal.begin());
            auto vanillaChange=nativeVanillaPackChange(
                op->tx.oldGlobal,op->tx.targetGlobal);
            test.singleVanillaEditorChange=op->nativeSettings&&op->r24AuthorizedClone
                &&op->ownedPacks.size()==1
                &&((vanillaChange.delta==NativeVanillaPackDelta::EnableOne
                    &&op->r27ToggleKind==R27LocalToggle::Enable)
                    ||(vanillaChange.delta==NativeVanillaPackDelta::DisableOne
                    &&op->r27ToggleKind==R27LocalToggle::Disable))
                &&keys(ids(rpm.mGlobalStack.get()))==op->tx.targetGlobal;
            test.prepared=op->group.mask==3&&op->group.count==2;
            test.originalInGame=inGame==1;
            test.gameReady=game->isReadyToRender()&&!game->isShowingLoadingScreen();
            test.loadReady=loadFlag==1;
            test.notLeaving=leaveDone==0;
            test.notMultiplayer=!op->client->isInMultiplayerGame();
            test.levelEmpty=op->level.empty();
            test.offlineCloneLevelPreserved=op->r24AuthorizedClone
                &&!test.levelEmpty&&op->level.size()==2
                &&keys(ids(rpm.mLevelStack.get()))==op->level
                &&bool(rpm.mUseGlobalPackStack)
                &&!op->client->isInMultiplayerGame();
            test.noNativeTask=nativeTasks.load()==0;
            limited=admitWorldNativeCompatibility(test);
            if(!op->compatibilityAuditLogged){
                op->compatibilityAuditLogged=true;
                auto failures=worldNativeCompatibilityFailures(test);
                packDiagnostic("RESOURCEPACK_WORLD event=android_compatibility_admission failed_mask=%u installed=%u thread=%u game=%u world=%u manager=%u identity=%u initial=%u pending=%u append=%u remove=%u native_editor=%u prepared=%u in_game=%u game_ready=%u load_ready=%u not_leaving=%u not_multiplayer=%u level_empty=%u level_clone_preserved=%u task_free=%u",
                    failures,unsigned(test.installed),unsigned(test.ownerThread),unsigned(test.sameGame),unsigned(test.sameWorld),
                    unsigned(test.sameManager),unsigned(test.identity),unsigned(test.initial),unsigned(test.pending),
                    unsigned(test.singleLocalAppend),unsigned(test.singleLocalRemove),unsigned(test.singleVanillaEditorChange),
                    unsigned(test.prepared),unsigned(test.originalInGame),
                    unsigned(test.gameReady),unsigned(test.loadReady),unsigned(test.notLeaving),
                    unsigned(test.notMultiplayer),unsigned(test.levelEmpty),
                    unsigned(test.offlineCloneLevelPreserved),unsigned(test.noNativeTask));
            }
        }
    }catch(...){loadFlag=-2;limited=false;}
    bool permitted{};
    unsigned maskHits{};
    if(limited){
        // Within this single synchronous native origin, no game fields,
        // listener return value, Full pending bit or GPU owner are modified.
        auto old=worldCompatibilityQueryHits;
        {ScopedWorldCompatibilityQuery mask;permitted=origin();}
        maskHits=worldCompatibilityQueryHits-old;
        packDiagnostic("RESOURCEPACK_WORLD event=android_compatibility_native_gate eligible=1 permitted=%u in_game_query_hits=%u load_flag=%d",
            unsigned(permitted),maskHits,loadFlag);
    }else permitted=origin();
    if(inspect){
        auto total=packInvalidVotes.fetch_add(1)+1;
        if(!permitted)packInvalidRejects.fetch_add(1);
        if(total<=15||(!permitted&&total%50==0))
            packDiagnostic("RESOURCEPACK_WORLD event=full_invalid_gate permitted=%u compatibility_armed=%u game_query_hits=%u resource_state=%d leave_done=%u in_game=%u votes=%llu rejects=%llu",
                unsigned(permitted),unsigned(limited),maskHits,loadFlag,leaveDone,inGame,
                diagNumber(total),diagNumber(packInvalidRejects.load()));
    }
    return permitted;
}
LL_TYPE_INSTANCE_HOOK(WorldUpdateHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::$update,void){NativeHookLifetime::Lease lease(lifetime);if(lease.admitted())serviceWorldEngineReload(*this);origin();if(lease.admitted())tick(*this);}
LL_TYPE_INSTANCE_HOOK(WorldActiveHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_onActiveResourcePacksChanged,void,ResourcePackManager& manager,bool full){
    revision.fetch_add(1);
    TitleCallbackLease lease(lifetime,callbacks);auto op=lease.admitted()?active.load():nullptr;
    auto serial=nativeReloadSerial.fetch_add(1)+1;
    if(op&&op->apiReload){
        auto inherited=worldNativeContext;WorldNativeContext context;
        try{
            std::lock_guard lock(op->mutex);
            if(op->tx.busy()&&!worldNativeStaleAttempt(op->tx,inherited)){
                auto actual=op->tx.owner;actual.game=address(this);actual.manager=address(&manager);
                actual.thread=GetCurrentThreadId();actual.present=engineOwnerCurrent(*op,*this);
                auto expected=expectedEngineFull(*op,manager);
                bool fullMatches=engineFullMatches(*op,manager,expected);
                // R25: isolate and record every admission predicate. R24's
                // visual result did not prove native peer re-acquisition; the
                // previous short-circuit condition hid the first failed gate.
                R25NativeEntryAudit gate{};
                gate.seeded=inherited.seed();
                gate.noCycle=!inherited.cycle;
                gate.tokenMatches=inherited.token==op->tx.token;
                gate.attemptMatches=inherited.attempt==op->tx.attempt;
                gate.ownerMatches=inherited.owner.same(op->tx.owner);
                gate.engineOwner=actual.present;
                gate.fullMatches=fullMatches;
                if(gate.seeded&&gate.noCycle&&gate.tokenMatches&&
                    gate.attemptMatches&&gate.ownerMatches&&gate.engineOwner&&gate.fullMatches){
                    if(op->r26PeersNativeArmed){
                        // Prepared was verified before entering native
                        // composition. Active is the correct phase while
                        // LHolo's owned mutation lease is still in scope.
                        gate.peersPrepared=op->group.nativeActive(op->tx);
                    }else if(!op->r24AuthorizedClone){
                        // Preserve the original non-clone Settings route.
                        gate.peersPrepared=ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch);
                    }
                }
                if(gate.peersPrepared)
                    gate.receiptEntered=op->engine.engineEntered(op->engine.identity(),serial,true,true);
                if(gate.receiptEntered)
                    gate.transactionStarted=op->tx.nativeStart(actual,op->tx.attempt,full);
                if(gate.transactionStarted)
                    gate.peersBegan=op->r26PeersNativeArmed
                        ||commandPeers(*op,WorldPackReloadCommand::BeginNative);
                if(!gate.peersPrepared){
                    for(unsigned i=0;i<op->group.count;++i){
                        auto const& peer=op->group.observations[i];
                        packDiagnostic("RESOURCEPACK_R26 event=native_peer_rejected peer=%u phase=%u error=%u readers=%u jobs=%u borrowers=%u epoch=%llu",
                            i,unsigned(peer.phase),peer.error,peer.readers,peer.jobs,
                            peer.borrowers,diagNumber(peer.epoch));
                    }
                }
                if(r25NativeEntryCompleted(gate)){
                    context={op->tx.token,op->tx.attempt,serial,op->tx.owner,full};context.producer=serial;
                    op->cycle=context;op->expectedFull=std::move(expected);op->expectedFullKnown=true;
                }else {op->engine.seal();op->tx.seal();}
                packDiagnostic("RESOURCEPACK_R25 event=native_entry_audit token=%llu attempt=%llu cycle=%llu missing_mask=%u seed=%u owner=%u prepared=%u receipt=%u started=%u began=%u",
                    diagNumber(op->tx.token),diagNumber(op->tx.attempt),diagNumber(serial),
                    r25NativeEntryMissing(gate),unsigned(gate.seeded),unsigned(gate.engineOwner),
                    unsigned(gate.peersPrepared),unsigned(gate.receiptEntered),
                    unsigned(gate.transactionStarted),unsigned(gate.peersBegan));
                packDiagnostic("RESOURCEPACK_R26 event=native_entry token=%llu prearmed=%u native_active_match=%u admitted=%u missing_mask=%u",
                    diagNumber(op->tx.token),unsigned(op->r26PeersNativeArmed),
                    unsigned(gate.peersPrepared),unsigned(context.receipt()),
                    r25NativeEntryMissing(gate));
                packDiagnostic("RESOURCEPACK_WORLD event=native_begin route=engine_api token=%llu attempt=%llu cycle=%llu admitted=%u full=%u full_match=%u",
                    diagNumber(op->tx.token),diagNumber(op->tx.attempt),diagNumber(serial),unsigned(context.receipt()),unsigned(full),unsigned(fullMatches));
            }
        }catch(...){std::lock_guard lock(op->mutex);op->engine.seal();op->tx.seal();}
        WorldNativeScope scope(context.receipt()?context:inherited);
        try{origin(manager,full);}catch(...){if(context.receipt()){std::lock_guard lock(op->mutex);op->engine.seal();op->tx.seal();}throw;}
        if(context.receipt()){
            std::lock_guard lock(op->mutex);
            if(nativeReloadSerial.load()!=serial||!op->engine.engineReturned(serial,true)){op->engine.seal();op->tx.seal();}
            else op->tx.startResult(context.attempt,true);
        }
        return;
    }
    auto inherited=worldNativeContext;WorldNativeActiveInvocation invocation(inherited);
    if(op){std::lock_guard lock(op->mutex);
        invocation=prepareWorldActiveInvocation(op->tx,inherited,[&]{
            auto actual=op->tx.owner;actual.game=address(this);actual.manager=address(&manager);actual.thread=GetCurrentThreadId();actual.present=worldGeneration.load()==actual.world&&runtimeReady.load();
            auto context=beginWorldCycle(op->tx,inherited,actual,full,cycles.fetch_add(1)+1);
            if(context.receipt()&&commandPeers(*op,WorldPackReloadCommand::BeginNative))op->cycle=context;
            else {op->tx.seal();context={};}
            packDiagnostic("RESOURCEPACK_WORLD event=native_begin token=%llu attempt=%llu cycle=%llu admitted=%u full=%u phase=%u",
                diagNumber(op->tx.token),diagNumber(op->tx.attempt),diagNumber(context.cycle),unsigned(context.receipt()),unsigned(full),unsigned(op->tx.phase));
            return context;
        });
    }
    auto const& context=invocation.observed;
    try{runWorldActiveOrigin(invocation,[&]{origin(manager,full);});}catch(...){if(op&&context.receipt()){std::lock_guard lock(op->mutex);op->tx.startResult(context.attempt,false);}throw;}
    if(op&&context.receipt()){std::lock_guard lock(op->mutex);op->tx.startResult(context.attempt,true);
        packDiagnostic("RESOURCEPACK_WORLD event=native_start_return token=%llu attempt=%llu normal=1 phase=%u",
            diagNumber(op->tx.token),diagNumber(context.attempt),unsigned(op->tx.phase));}
}
LL_TYPE_INSTANCE_HOOK(WorldFinishHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_finishReloadingResources,void,ResourcePackManager& manager,bool full){
    TitleCallbackLease lease(lifetime,callbacks);auto op=lease.admitted()?active.load():nullptr;auto inherited=worldNativeContext;
    if(!op){origin(manager,full);return;}
    if(op->apiReload){
        bool matches{};
        {
            std::lock_guard lock(op->mutex);
            matches=worldFinishMatches(op->tx,op->cycle,inherited,address(this),address(&manager),worldGeneration.load(),full)
                &&nativeReloadSerial.load()==inherited.cycle;
            if(!matches&&op->tx.busy()&&!worldNativeStaleAttempt(op->tx,inherited)){op->engine.seal();op->tx.seal();}
        }
        try{origin(manager,full);}catch(...){if(matches){std::lock_guard lock(op->mutex);op->engine.seal();op->tx.seal();}throw;}
        if(matches){std::lock_guard lock(op->mutex);
            if(nativeReloadSerial.load()!=inherited.cycle||!worldFinishMatches(op->tx,op->cycle,inherited,address(this),address(&manager),worldGeneration.load(),full)
                ||!op->engine.finishReturned(inherited.cycle,true)){op->engine.seal();op->tx.seal();}
            else op->tx.finish(inherited.attempt,true);
            packDiagnostic("RESOURCEPACK_WORLD event=native_finish route=engine_api token=%llu attempt=%llu cycle=%llu observed=%u full=%u",
                diagNumber(op->tx.token),diagNumber(inherited.attempt),diagNumber(inherited.cycle),unsigned(op->tx.finished),unsigned(full));
        }
        return;
    }
    auto producing=inherited;
    observeWorldFinish(op->mutex,op->tx,op->cycle,inherited,address(this),address(&manager),worldGeneration.load(),full,[&]{
        WorldNativeScope scope(producing);origin(manager,full);
    });
    std::lock_guard lock(op->mutex);
    packDiagnostic("RESOURCEPACK_WORLD event=native_finish token=%llu attempt=%llu observed=%u phase=%u full=%u",
        diagNumber(op->tx.token),diagNumber(inherited.attempt),unsigned(op->tx.finished),unsigned(op->tx.phase),unsigned(full));
}
LL_TYPE_INSTANCE_HOOK(WorldGameFrameHook,ll::memory::HookPriority::Normal,GameRenderer,&GameRenderer::renderCurrentFrame,void,float delta){
    NativeHookLifetime::Lease lease(lifetime);auto op=lease.admitted()?active.load():nullptr;
    std::shared_ptr<IClientInstance> client;WorldNativeApplicationReceipt::FramePermit permit;
    WorldPackOwner expected;
    if(op&&op->apiReload){
        {std::lock_guard lock(op->mutex);client=op->client;expected=op->tx.owner;}
        std::uintptr_t renderer{};bool currentGame{};
        try{currentGame=client&&worldGeneration.load()==expected.world
                &&address(&client->getGameRenderer())==address(this);
            if(currentGame)renderer=address(client->getLevelRenderer());}catch(...){currentGame=false;}
        if(currentGame&&renderer){
            std::lock_guard lock(op->mutex);
            if(active.load()==op&&op->tx.phase==WorldSwitchPhase::Reloading&&op->tx.finished&&op->tx.startReturned)
                permit=op->engine.beginFrame(expected.game,address(client.get()),worldGeneration.load(),
                    renderer,frameInvocations.fetch_add(1)+1);
        }
    }
    WorldFrameScope frame({op,permit});
    try{origin(delta);}catch(...){
        if(permit){std::lock_guard lock(op->mutex);op->engine.frameReturned(permit,0,0,0,0,false);op->tx.seal();}throw;
    }
    if(permit){
        std::uintptr_t game{},currentClient{},renderer{};std::uint64_t world{};
        try{if(client&&active.load()==op&&worldGeneration.load()==permit.world
            &&address(&client->getGameRenderer())==address(this)){
            game=expected.game;currentClient=address(client.get());world=worldGeneration.load();renderer=address(client->getLevelRenderer());
        }}catch(...){game=currentClient=renderer=0;world=0;}
        std::lock_guard lock(op->mutex);
        bool completed=op->engine.frameReturned(permit,game,currentClient,world,renderer,true);
        if(!completed&&op->engine.sealed())op->tx.seal();
        else if(completed)packDiagnostic("RESOURCEPACK_WORLD event=native_frame route=api_apply token=%llu attempt=%llu frame=%llu current_renderer=%llu normal=1 level_returned=1",
            diagNumber(permit.identity.token),diagNumber(permit.identity.attempt),diagNumber(permit.frame),diagNumber(renderer));
    }
}
LL_TYPE_INSTANCE_HOOK(WorldLevelFrameHook,ll::memory::HookPriority::Normal,LevelRenderer,&LevelRenderer::renderLevel,void,
    ScreenContext& screen,FrameRenderObject const& render){
    NativeHookLifetime::Lease lease(lifetime);
    auto frame=worldFrameInvocation;
    bool candidate{};
    try{candidate=lease.admitted()&&frame.operation&&frame.permit&&active.load()==frame.operation
        &&worldGeneration.load()==frame.permit.world&&frame.operation->client
        &&address(frame.operation->client.get())==frame.permit.client
        &&address(frame.operation->client->getLevelRenderer())==address(this);}
    catch(...){}
    try{origin(screen,render);}catch(...){if(candidate){std::lock_guard lock(frame.operation->mutex);
        frame.operation->engine.seal();frame.operation->tx.seal();}throw;}
    if(candidate){bool stillCurrent{};try{stillCurrent=active.load()==frame.operation
            &&worldGeneration.load()==frame.permit.world
            &&frame.operation->client->getLevelRenderer()==this;}catch(...){}
        std::lock_guard lock(frame.operation->mutex);
        if(!stillCurrent){frame.operation->engine.seal();frame.operation->tx.seal();}
        else if(!frame.operation->engine.levelRendered(frame.permit,address(this),true)
            &&frame.operation->engine.sealed())frame.operation->tx.seal();
    }
}
using WorldWorkerTask=BackgroundTask<TaskResult,void>;
using WorldCallbackTask=BackgroundTask<void,void>;
template<class Result,class Factory>
auto worldFactory(brstd::move_only_function<Result()> task,Factory&& factory){
    bool failed{};
    try{
        auto result=wrapWorldCallable<Result>(worldNativeContext,lifetime,callbacks,std::move(task),
            [&](auto body){if(failed)lost=true;return factory(std::move(body));},failed,TitleNativeAllocation{},&nativeTasks);
        if(failed)lost=true;return result;
    }catch(...){if(failed)lost=true;throw;}
}
LL_TYPE_STATIC_HOOK(WorldWorkerFactoryHook,ll::memory::HookPriority::Normal,WorldWorkerTask,&WorldWorkerTask::create,std::shared_ptr<WorldWorkerTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<TaskResult()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return worldFactory<TaskResult>(std::move(task),[&](auto body){return origin(owner,info,std::move(predecessor),async,std::move(body));});
}
LL_TYPE_STATIC_HOOK(WorldCallbackFactoryHook,ll::memory::HookPriority::Normal,WorldCallbackTask,&WorldCallbackTask::create,std::shared_ptr<WorldCallbackTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<void()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return worldFactory<void>(std::move(task),[&](auto body){return origin(owner,info,std::move(predecessor),async,std::move(body));});
}
bool installWorldNativeCompletionObservers(DWORD thread) noexcept {
    if(completionHookAttempted.exchange(true))return completionHooksReady.load();
    unsigned failure{};
    auto check=[&](unsigned stage,bool pass){
        packDiagnostic("RESOURCEPACK_WORLD event=application_adapter stage=%u pass=%u sdk=26.51.5",stage,unsigned(pass));
        if(!pass&&!failure)failure=stage;return pass;
    };
    auto hook=[&](unsigned stage,int code){
        packDiagnostic("RESOURCEPACK_WORLD event=application_adapter stage=%u status=%d sdk=26.51.5",stage,code);
        if(code&&!failure)failure=stage;return code==0;
    };
    bool accepted=registerWorldApplicationAdapter([&](unsigned stage){
        return check(stage,stage==1?resourcePackImageVerified():stage==2?thread&&thread==ownerThread.load():installed.load());
    },[&](unsigned stage){
        switch(stage){case 20:return hook(stage,WorldApplyingTickHook::hook());
        case 21:return hook(stage,WorldApplyingLoadingHook::hook());
        case 22:return hook(stage,WorldLevelFrameHook::hook());
        case 23:return hook(stage,WorldGameFrameHook::hook());default:return false;}
    });
    if(accepted)completionHooksReady=true;else lost=true;
    packDiagnostic("RESOURCEPACK_WORLD event=application_adapter stage=199 first_fail=%u ready=%u lost=%u",
        failure,unsigned(completionHooksReady.load()),unsigned(lost.load()));
    return accepted;
}

}
bool installWorldPackSwitch() noexcept {
    if(attempted.exchange(true))return installed.load();try{
        if(!lifetime.prepare(pin)){packDiagnostic("RESOURCEPACK_WORLD event=install accepted=0 reason=1");return false;}lifetime.registrationStarted();
        if(WorldUpdateHook::hook()!=0||WorldNativeIsInGameCompatibilityHook::hook()!=0||WorldFullStackInvalidHook::hook()!=0||WorldActiveHook::hook()!=0||WorldFinishHook::hook()!=0
            ||WorldWorkerFactoryHook::hook()!=0||WorldCallbackFactoryHook::hook()!=0
            ||WorldSettingsCreatedHook::hook()!=0||WorldSettingsExitDispatcherHook::hook()!=0||WorldSettingsCanExitHook::hook()!=0||WorldSettingsStackHook::hook()!=0
            ||WorldSettingsClientPreGameHook::hook()!=0||WorldSettingsGlobalHook::hook()!=0||WorldSettingsApplyHook::hook()!=0
            ||WorldSettingsComposeHook::hook()!=0||WorldSettingsRootInitHook::hook()!=0||WorldSettingsRootOpenedHook::hook()!=0||!lifetime.open()){lifetime.close();packDiagnostic("RESOURCEPACK_WORLD event=install accepted=0 reason=2");return false;}
        installed=true;packDiagnostic("RESOURCEPACK_WORLD event=install accepted=1 reason=0");return true;
    }catch(...){lifetime.close();packDiagnostic("RESOURCEPACK_WORLD event=install accepted=0 reason=3");return false;}
}
void invalidateWorldPackOperation() noexcept {if(auto op=active.load()){
    NativeSettingsPendingHolds staleExit;std::shared_ptr<MainMenuScreenModel> staleModel;
    {std::lock_guard lock(op->mutex);
        if(op->pendingExit)staleExit=takePendingSettingsExit(*op);
        staleModel=std::move(op->completionModel);
        if(op->tx.busy()||op->group.mask){packDiagnostic("RESOURCEPACK_WORLD event=invalidate token=%llu phase=%u mask=%u started=%u finished=%u",
            diagNumber(op->tx.token),unsigned(op->tx.phase),op->group.mask,unsigned(op->tx.started),unsigned(op->tx.finished));op->tx.seal();stopPeers(*op);}
    }
}}
bool stopWorldPackSwitch() noexcept {installed=false;lifetime.close();invalidateWorldPackOperation();praxisWorldParticipant().stop();return lifetime.readers()==0&&callbacks.load()==0&&nativeTasks.load()==0&&praxisWorldParticipant().quiescent();}
}
