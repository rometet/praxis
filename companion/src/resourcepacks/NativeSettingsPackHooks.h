#include "resourcepacks/NativeVirtualSlot2660.h"
// Private implementation included inside WorldPackSwitch's owner namespace.
// 26.60 does not export the private _areAnyPackImportsPending helper.
// The settings controller owns the async work group which performs its
// pending imports; a nonempty group is a conservative native readiness gate.
bool nativeSettingsImportsPending(GeneralSettingsScreenController const& general) {
    auto const& tasks=general.mTaskGroup.get();
    return tasks && !tasks->isEmpty();
}
WorldPackOwner settingsOwner(MinecraftScreenModel& model,std::shared_ptr<IClientInstance>* retained=nullptr) {
    auto* game=observedGame.load();
    auto world=worldGeneration.load();auto thread=GetCurrentThreadId();
    if(!game||!world||observedWorld.load()!=world||!runtimeReady.load()||thread!=ownerThread.load())return {};
    auto held=nativeSettingsClient(model,*game,observedClient.load());auto* client=held.get();
    if(!client||!game->isInGame()||!client->getLocalPlayer()||!client->getLevelRenderer())return {};
    auto& manager=model.getResourcePackManager();
    if(retained)*retained=std::move(held);
    // Use the same actual-world readiness boundary as the update owner. A menu
    // model's UI classification/loading state is not the current world's state.
    return {address(game),address(&manager),address(client),address(client->getLevelRenderer()),
        world,thread,true,game->isReadyToRender()&&!game->isShowingLoadingScreen(),manager.mPendingRestack};
}
struct WorldSettingsRootInitInvocation {
    // Borrowed Root is used only while the engine's _init is on this thread.
    // Model/client ownership survives General construction without relying on
    // constructor-time shared_from_this or an as-yet unpublished scene.
    SettingsScreenController* root{};
    std::shared_ptr<MainMenuScreenModel> model;
    std::shared_ptr<IClientInstance> client;
    WorldPackOwner owner;
    std::shared_ptr<Operation> operation;
    bool eligible{};
};
thread_local WorldSettingsRootInitInvocation worldSettingsRootInitInvocation;
struct WorldSettingsRootInitScope {
    WorldSettingsRootInitInvocation previous;
    explicit WorldSettingsRootInitScope(WorldSettingsRootInitInvocation current)
        :previous(std::move(worldSettingsRootInitInvocation)){worldSettingsRootInitInvocation=std::move(current);}
    ~WorldSettingsRootInitScope(){worldSettingsRootInitInvocation=std::move(previous);}
};
std::shared_ptr<Operation> settingsOperation(std::uintptr_t controller) {
    auto op=active.load();return op&&op->nativeSettings&&op->settingsContext.controller==controller?op:nullptr;
}
bool beginSettings(GeneralSettingsScreenController* controller,MainMenuScreenModel& model,unsigned& reason) noexcept {
    auto reject=[&](unsigned code){reason=code;return false;};
    try{
        auto& initialization=worldSettingsRootInitInvocation;
        if(!initialization.eligible||!initialization.root||initialization.model.get()!=&model
            ||initialization.operation)return reject(9);
        WorldSettingsBootstrapDiagnostics bootstrap;
        if(!bootstrap.ownerThread(GetCurrentThreadId(),ownerThread.load())
            ||!bootstrap.flag(2,installed.load())
            ||!bootstrap.flag(3,resourcePackImageVerified())
            ||!bootstrap.flag(4,completionHooksReady.load()&&legacySettingsHooksReady.load())
            ||!bootstrap.flag(5,lost.load(),false)
            ||!bootstrap.zero(6,callbacks.load())
            ||!bootstrap.zero(7,nativeTasks.load())
            ||!bootstrap.flag(8,resolveLholo())){
            bootstrap.publishFirst(packDiagnosticQueue(),settingsBootstrapDiagnosticAttempted);return reject(1);
        }
        std::shared_ptr<IClientInstance> client;auto owner=settingsOwner(model,&client);if(!owner.usable())return reject(2);
        if(client!=initialization.client||!initialization.owner.same(owner))return reject(10);
        auto previous=active.load();if(previous){std::lock_guard lock(previous->mutex);if(previous->pendingExit||previous->tx.busy()||previous->group.mask||previous->tx.phase==WorldSwitchPhase::Recovery)return reject(3);}
        auto& manager=model.getResourcePackManager();
        auto level=ids(manager.mLevelStack.get());
        bool clone=authorizedR24Clone(client);
        bool scopedClone=vanillaScopedCloneGrant(
            clone,!isVerifiedIntegratedWorld(*client),level.size()==2,
            level.size(),bool(manager.mUseGlobalPackStack),
            bool(manager.mInitializing),bool(manager.mPendingRestack));
        bool include=nativeSettingsMayIncludeGlobal(
            manager.mUseGlobalPackStack,!isVerifiedIntegratedWorld(*client),
            level.empty(),scopedClone);
        if(!include)return reject(4);
        // Do not grant the native editor a live-in-world compatibility
        // exception to any other world or to any multiplayer stack.
        if(!scopedClone)return reject(11);
        auto op=std::make_shared<Operation>();op->nativeSettings=true;op->apiReload=true;
        op->r24AuthorizedClone=scopedClone;
        op->settingsSession=settingsSessions.fetch_add(1)+1;
        op->settingsContext={address(controller),owner.client,owner.manager,owner.world,include};
        op->editorOwner=owner;op->client=std::move(client);
        op->level=keys(level);op->treatment=keys(ids(manager.mTreatmentStack.get()));op->base=keys(ids(manager.mBaseGameStack.get()));
        op->oldIds=ids(manager.mGlobalStack.get());
        op->editorInitialGlobal=keys(op->oldIds);
        packDiagnostic("RESOURCEPACK_R29 event=vanilla_editor_armed clone=1 world_rp=%zu global=%zu native_policy=%u",
            level.size(),op->editorInitialGlobal.size(),unsigned(include));
        if(binding.owner){op->group.peers[1]=binding.entry;op->group.count=2;}
        active.store(op);initialization.operation=op;reason=0;return true;
    }catch(...){return reject(7);}
}
// Narrow, owner-thread 26.60 Settings UI query for the real integrated
// singleplayer Global Resources tab. No synthetic Settings controller, fake
// runtime pack or alternate user interface. The original preGame verdict is
// extended ONLY while the native Settings UI itself owns the active tab.
// Reacquire every pointer from the engine-owned CURRENT top scene on each call.
bool verifiedNativeGlobalPage(IClientInstance* client,bool requireEnabledFlags) noexcept {
    // The 26.60 MinecraftGame::isInGame() getter can call the client's
    // virtual isPreGame(). This very function is queried from that hook:
    // isPreGame -> native editor check -> isInGame -> isPreGame was the
    // verified C00000FD infinite recursion in both 14:08 minidumps.
    // Accessors below may also indirectly query isPreGame; never evaluate
    // this UI grant recursively. The inner query uses the game's original
    // isPreGame verdict while the outer query finishes.
    static thread_local bool inNativeGlobalProbe{};
    if(inNativeGlobalProbe)return false;
    inNativeGlobalProbe=true;
    struct ResetProbe {
        bool& active;
        ~ResetProbe() noexcept {active=false;}
    } reset{inNativeGlobalProbe};
    try {
        if(!client||GetCurrentThreadId()!=ownerThread.load()||!runtimeReady.load()
           ||observedClient.load()!=address(client)||!isVerifiedIntegratedWorld(*client)
           ||!observedGame.load()
           ||!worldGeneration.load()||observedWorld.load()!=worldGeneration.load())
            return false;
        auto scene=client->getCurrentSceneStack()->getTopSceneShared();
        if(!scene||scene->getSceneType()!=ui::SceneType::SettingsScene
           ||scene->getRawScreenName().find("settings")==std::string::npos)
            return false;
        auto* ui=static_cast<UIScene*>(scene.get());
        auto* view=ui->mScreenView.get();
        auto* controller=view?view->mController.get():nullptr;
        if(!controller||controller->getSceneType()!=ui::SceneType::SettingsScene
           ||controller->getSceneId()!=scene->mSceneId)return false;
        auto& root=*static_cast<SettingsScreenController*>(controller);
        if(root.mSettingsMode!=SettingsScreenMode::Settings
           ||root.mCurrentTab!=SettingsTabIndex::GlobalResourcesTab
           ||root.mLegacySyncOnly||!root.mMainMenuScreenModel
           ||!root.mGeneralSettingsScreenController)return false;
        auto& general=*root.mGeneralSettingsScreenController;
        if(!general.mResourceContentSource)return false;
        auto modelClient=nativeSettingsClient(*root.mMainMenuScreenModel,
            *observedGame.load(),observedClient.load());
        if(modelClient.get()!=client)return false;
        auto& manager=root.mMainMenuScreenModel->getResourcePackManager();
        if(!manager.mUseGlobalPackStack||manager.mInitializing
           ||manager.mPendingRestack||!ids(manager.mLevelStack.get()).empty())
            return false;
        if(requireEnabledFlags && (!root.mIsGlobalSettingsScreen
           ||!root.mReloadTexturePacksOnExit||!general.mIsGlobalSettings
           ||!general.mReloadTexturePacksOnExit))return false;
        return true;
    }catch(...) {return false;}
}
bool verifiedNativeGlobalPagePreGame(bool original,IClientInstance* client) noexcept {
    if(original)return true;
    bool granted=verifiedNativeGlobalPage(client,true);
    static std::atomic_bool logged{};
    if(granted&&!logged.exchange(true))
        packDiagnostic("RESOURCEPACK_WORLD event=vanilla_global_pregame_granted exact_native_owner=1");
    return granted;
}
// Hold the native shared controller while reading it. Never persist or inspect
// a raw Settings pointer after its owning scene was deleted.
bool settingsEditorPreGame(bool original,IClientInstance* client) noexcept {
    if(original)return true;
    if(nativeSettingsEditorSuppressed)return false;
    static thread_local bool probing{};if(probing)return false;
    struct Probe {bool& active;explicit Probe(bool& v):active(v){active=true;}~Probe(){active=false;}} probe(probing);
    try{
        auto op=active.load();if(!op||!op->nativeSettings||!client)return false;
        NativeSettingsPackContext context;NativeSettingsEditorView view;WorldPackOwner expected;
        std::weak_ptr<MinecraftScreenController> weak;
        {
            std::lock_guard lock(op->mutex);context=op->settingsContext;weak=op->settingsRoot;expected=op->editorOwner;
            view.client=address(client);view.world=worldGeneration.load();
            view.ownerThread=GetCurrentThreadId()==op->editorOwner.thread&&GetCurrentThreadId()==ownerThread.load();
            view.sessionOpen=op->tx.phase==WorldSwitchPhase::Idle&&!op->pendingExit&&!op->exitReplaying&&!op->exitReplayed;
        }
        if(!view.ownerThread||!view.sessionOpen||view.client!=context.client
            ||view.world!=context.world||!runtimeReady.load()||observedWorld.load()!=view.world)return false;
        auto held=weak.lock();if(!held)return false;
        auto& root=*static_cast<SettingsScreenController*>(held.get());
        auto model=root.mMainMenuScreenModel;if(!model)return false;
        std::shared_ptr<IClientInstance> currentClient;auto current=settingsOwner(*model,&currentClient);
        if(currentClient.get()!=client)return false;
        view.ownerCurrent=current.usable()&&expected.same(current);
        view.rootAlive=true;
        auto general=root.mGeneralSettingsScreenController;
        view.sameController=general&&address(general.get())==context.controller&&general->mIsGlobalSettings
            &&general->mReloadTexturePacksOnExit&&general->mResourceContentSource;
        view.globalTab=root.mCurrentTab==SettingsTabIndex::GlobalResourcesTab;
        view.settingsScreen=root.mSettingsMode==SettingsScreenMode::Settings;
        // The grant belongs to this retained client's current top scene and
        // the live Root's scene ID, not a generated model-context field offset.
        auto stack=currentClient->getCurrentSceneStack();
        auto scene=stack->getTopSceneShared();
        view.sameTopScene=scene&&scene->mSceneId==root.getSceneId();
        return nativeSettingsEditorPreGame(original,context,view);
    }catch(...){return original;}
}
bool settingsPrepared(std::shared_ptr<Operation> const& op,MinecraftScreenModel& model) {
    if(!op)return false;auto owner=settingsOwner(model);std::lock_guard lock(op->mutex);
    return !op->nativeNavigation&&op->tx.phase==WorldSwitchPhase::AwaitApply&&op->tx.owner.same(owner)&&owner.usable()
        &&otherStacks(*op,model.getResourcePackManager())&&ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch);
}
// The native settings UI produces PackInstanceId identities. All global
// changes must still pass the same registered User/Dev catalog+manifest gates
// used by the existing R27 toggle, never by a UI's success notification.
bool r29ValidateNativeSelection(
    Operation& op,std::vector<PackInstanceId> const& selected,
    NativeVanillaPackDelta direction) {
    if(!op.r24AuthorizedClone || !op.nativeSettings
       || (direction!=NativeVanillaPackDelta::EnableOne
           && direction!=NativeVanillaPackDelta::DisableOne))
        return false;
    if(keys(op.oldIds)!=op.editorInitialGlobal || op.ownedPacks.size()!=0)
        return false;
    auto& repository=op.client->getResourcePackRepository();
    auto change=nativeVanillaPackChange(op.editorInitialGlobal,keys(selected));
    if(change.delta!=direction)return false;
    PackInstanceId const& changed=(direction==NativeVanillaPackDelta::EnableOne)
        ?selected.at(change.index):op.oldIds.at(change.index);
    if(!changed.mSubpackName->empty())return false;
    if(changed.mPackId->mPackType!=PackType::Resources
        && changed.mPackId->mPackType!=PackType::Invalid)
        return false;
    auto owned=repository.getResourcePackSatisfiesPackId(changed.mPackId.get(),true,ResourcePackSelectionPolicy::Default);
    if(!owned)return false;
    bool matchesSource=false;
    auto user=repository.getPacksByResourceLocation(PackOrigin::User);
    auto dev=repository.getPacksByResourceLocation(PackOrigin::Dev);
    if(user.size()+dev.size()>512)return false;
    for(auto* item:user)if(item==owned.get())matchesSource=true;
    for(auto* item:dev)if(item==owned.get())matchesSource=true;
    if(!matchesSource)return false;
    auto const localPath=packPath(*owned);
    std::atomic_bool cancel{};
    bool recognized=false;
    for(auto const& folder:{repository.getResourcePacksPath().value,
                            repository.getDevelopmentResourcePacksPath().value}){
        // Read only bounded local manifests, never game-owned assets or an
        // arbitrary selected archive. All successful catalogue rows must
        // still pass the live repository's actual manifest/owner check.
        std::filesystem::path const root(std::u8string_view(
            reinterpret_cast<char8_t const*>(folder.data()),folder.size()));
        auto catalog=scanCatalog(root,cancel);
        if(!catalog.complete)continue;
        for(auto const& pack:catalog.packs){
            if(pack.selectable&&pathEqual(localPath,pack.key)
                &&ordinary(*owned,pack)) {
                recognized=true;
                break;
            }
        }
        if(recognized)break;
    }
    if(!recognized)return false;
    op.r27ToggleKind=direction==NativeVanillaPackDelta::EnableOne
        ?R27LocalToggle::Enable:R27LocalToggle::Disable;
    op.ownedPacks.push_back(std::move(owned));
    return true;
}
// Called on the owner update after peer ResumeCancelled replies were checked.
// The captured scene is weak: a closed Settings scene cannot be resurrected
// merely because an old controller or model still has a shared reference.
bool resetSettingsCancelledForRetry(Operation& op,WorldPackOwner const& current) noexcept {
    if(op.tx.phase!=WorldSwitchPhase::Cancelled||!op.group.cancelVerified)return false;
    try{
        NativeSettingsCancelledRetryView v;
        v.nativeSettings=op.nativeSettings;v.peersResumed=op.group.cancelVerified;v.peersOpen=!op.group.mask;
        v.pendingReleased=!op.pendingExit&&!op.pendingRoot&&!op.pendingModel;
        v.notReplayed=!op.exitReplaying&&!op.exitReplayed;v.notNavigated=!op.nativeNavigation;
        v.noCompletion=!op.completionModel&&!op.cycle.receipt()&&!op.executed.identity();
        v.activeOperation=active.load().get()==&op;
        v.ownerCurrent=op.editorOwner.same(current)&&current.usable();
        v.ownerThread=current.thread==GetCurrentThreadId()&&current.thread==ownerThread.load();
        v.callbacksIdle=!callbacks.load()&&!nativeTasks.load()&&!lost.load();
        auto rootHeld=op.settingsRoot.lock();auto scene=op.settingsScene.lock();
        if(!rootHeld||!scene||!op.client)return false;
        auto& root=*static_cast<SettingsScreenController*>(rootHeld.get());
        auto model=root.mMainMenuScreenModel;auto general=root.mGeneralSettingsScreenController;
        v.sameRoot=op.settingsRetryRoot.lock().get()==rootHeld.get();
        v.sameModel=model&&op.settingsRetryModel.lock().get()==model.get();
        v.sameController=general&&address(general.get())==op.settingsContext.controller;
        v.sameSource=general&&general->mResourceContentSource
            &&address(static_cast<PackManagerContentSourceImpl*>(general->mResourceContentSource))==op.settingsRetrySource;
        v.sameTopScene=root.getSceneId()==op.settingsSceneId&&scene->mSceneId==op.settingsSceneId
            &&op.client->getCurrentSceneStack()->getTopSceneShared().get()==scene.get();
        v.settingsScreen=root.mSettingsMode==SettingsScreenMode::Settings
            &&root.mReloadTexturePacksOnExit;
        v.globalPolicy=general&&general->mIsGlobalSettings&&general->mReloadTexturePacksOnExit;
        if(!model)return false;
        std::shared_ptr<IClientInstance> client;auto owner=settingsOwner(*model,&client);
        v.ownerCurrent=v.ownerCurrent&&client.get()==op.client.get()&&owner.usable()&&op.editorOwner.same(owner);
        auto& manager=model->getResourcePackManager();
        v.oldGlobalUnchanged=keys(ids(manager.mGlobalStack.get()))==op.editorInitialGlobal&&op.tx.oldGlobal==op.editorInitialGlobal;
        v.otherStacksUnchanged=otherStacks(op,manager);
        if(!nativeSettingsCancelledRetry(op.tx,v))return false;
        // Selection/pack ownership belonged to the aborted attempt.
        // A fresh change must acquire the registered native pack again.
        op.ownedPacks.clear();
        op.r27ToggleKind=R27LocalToggle::Invalid;
        op.group.cancelVerified=false;op.group.pauseComplete=false;
        op.settingsExitDiagnostics=op.settingsApplicationDiagnostics=0;
        op.settingsScene.reset();op.settingsRetryRoot.reset();op.settingsRetryModel.reset();op.settingsRetrySource=0;
        return true;
    }catch(...){return false;}
}
// The SDK's 32-bit UI identifier algorithm, rather than a version-pinned numeric ID.
inline constexpr StringHash SettingsMenuExitButton{"button.menu_exit"};
struct SettingsReplayPermit {
    Operation* operation{};ScreenController* root{};ScreenEvent* event{};
    MainMenuScreenModel* model{};IClientInstance* client{};AbstractScene* scene{};
    WorldPackOwner owner;std::uint64_t token{};bool used{};
};
thread_local SettingsReplayPermit settingsReplayPermit;
bool settingsExitCurrent(Operation const& op,WorldPackOwner const& current) noexcept {
    try{
        if(!op.pendingExit||!op.pendingExit->alive()||op.pendingRevision!=revision.load()
            ||!op.editorOwner.same(current)||!current.usable()||current.thread!=GetCurrentThreadId())return false;
        auto held=op.pendingRoot;auto model=op.pendingModel;
        if(!held||!model||op.settingsRoot.lock().get()!=held.get()||active.load().get()!=&op)return false;
        auto& root=*static_cast<SettingsScreenController*>(held.get());
        auto general=root.mGeneralSettingsScreenController;
        if(!general||address(general.get())!=op.settingsContext.controller
            ||address(static_cast<PackManagerContentSourceImpl*>(general->mResourceContentSource))!=op.pendingSource
            ||!general->mIsGlobalSettings||!general->mReloadTexturePacksOnExit
            ||nativeSettingsImportsPending(*general)
            ||root.mMainMenuScreenModel.get()!=model.get()
            ||root.mCurrentTab!=op.pendingTab
            ||root.mSettingsMode!=SettingsScreenMode::Settings
            ||!root.mReloadTexturePacksOnExit
            ||root.getSceneId()!=op.settingsSceneId)return false;
        auto client=op.client;
        if(!client||address(client.get())!=current.client)return false;
        auto scene=client->getCurrentSceneStack()->getTopSceneShared();
        if(!scene||scene->mSceneId!=op.settingsSceneId||op.pendingScene.lock().get()!=scene.get())return false;
        auto owner=settingsOwner(*model);
        if(!op.editorOwner.same(owner)||keys(ids(model->getResourcePackManager().mGlobalStack.get()))!=op.editorInitialGlobal
            ||!otherStacks(op,model->getResourcePackManager()))return false;
        std::vector<PackInstanceId> selected;
        auto* game=observedGame.load();if(!game)return false;
        auto observed=nativeSelectedGlobalPacks(*general->mResourceContentSource,op.oldIds);
        if(!observed)return false;
        return keys(*observed)==op.pendingSelection;
    }catch(...){return false;}
}
void replaySettingsExit(std::shared_ptr<Operation> const& op) noexcept {
    std::shared_ptr<MinecraftScreenController> held;
    std::shared_ptr<MainMenuScreenModel> model;
    std::shared_ptr<IClientInstance> client;
    std::shared_ptr<AbstractScene> scene;
    std::unique_ptr<NativeSettingsExitEvent> event;
    std::uint64_t token{};
    NativeSettingsPendingHolds stale;
    // The native root callback may have become unable to leave while peers
    // drained. Do not replay a stale exit in that case.
    std::shared_ptr<MinecraftScreenController> liveRoot;
    {
        std::lock_guard lock(op->mutex);
        // A duplicate owner-service call or a service call before Prepared
        // must not turn a valid pending exit into a cancellation.
        if(!op->pendingExit||op->exitReplaying||op->exitReplayed)return;
        if(op->tx.phase!=WorldSwitchPhase::AwaitApply){
            if(!op->tx.beforeNative())stale=takePendingSettingsExit(*op);
            return;
        }
        liveRoot=op->pendingRoot;
    }
    bool canExit=false;
    try{canExit=liveRoot&&liveRoot->canExit()
        &&static_cast<SettingsScreenController*>(liveRoot.get())->mGeneralSettingsScreenController
        &&!nativeSettingsImportsPending(*static_cast<SettingsScreenController*>(liveRoot.get())->mGeneralSettingsScreenController);}
    catch(...){canExit=false;}
    {
        std::lock_guard lock(op->mutex);
        if(!op->pendingExit||op->exitReplaying||op->exitReplayed)return;
        if(op->tx.phase!=WorldSwitchPhase::AwaitApply){
            if(!op->tx.beforeNative())stale=takePendingSettingsExit(*op);
            return;
        }
        if(!canExit||!settingsExitCurrent(*op,op->tx.owner)
            ||!ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch)){
            stale=takePendingSettingsExit(*op);op->tx.cancel();return;
        }
        held=op->pendingRoot;model=op->pendingModel;client=op->client;
        if(!held||!model||!client){stale=takePendingSettingsExit(*op);op->tx.cancel();return;}
        try{scene=client->getCurrentSceneStack()->getTopSceneShared();}
        catch(...){stale=takePendingSettingsExit(*op);op->tx.cancel();return;}
        if(!scene||scene->mSceneId!=op->settingsSceneId||op->pendingScene.lock().get()!=scene.get()){
            stale=takePendingSettingsExit(*op);op->tx.cancel();return;
        }
        stale=takePendingSettingsExit(*op);
        held=std::move(stale.root);model=std::move(stale.model);event=std::move(stale.event);
        op->exitReplaying=op->exitReplayed=true;
        token=op->tx.token;
    }
    auto previous=settingsReplayPermit;
    settingsReplayPermit={op.get(),held.get(),&event->event(),model.get(),client.get(),scene.get(),op->tx.owner,token,false};
    try{held->handleEvent(event->event());}catch(...){std::lock_guard lock(op->mutex);op->tx.seal();}
    bool dispatched=settingsReplayPermit.used;
    settingsReplayPermit=previous;
    {
        std::lock_guard lock(op->mutex);
        op->exitReplaying=false;
        if(dispatched&&op->tx.busy()&&!op->executed.identity())op->completionModel=std::move(model);
        if(!dispatched&&op->tx.beforeNative())op->tx.cancel();
        // The native root may queue apply work after the dispatcher returns.
        // Bound that interval, but never infer "no native action" from a
        // missing synchronous navigate call and reopen old borrowers.
        if(dispatched&&op->tx.phase==WorldSwitchPhase::AwaitApply&&!op->nativeNavigation
            &&op->tx.awaitingSelection)op->tx.queueCallback(titlePackNow());
    }
}
// One record per decision class per live editor/attempt; unrelated controls
// produce at most one mismatch record. Values are booleans/counts, not pack IDs.
void settingsExitDiagnostic(Operation& op,NativeSettingsRootExitReason reason,NativeSettingsRootExitView const& v,
    unsigned eventType,bool interacted,bool idMatch,bool fields,bool rootGlobal,bool globalTab,
    bool selectedKnown,std::size_t selectedCount) noexcept {
    auto bit=std::uint32_t{1}<<unsigned(reason);if(op.settingsExitDiagnostics&bit)return;
    op.settingsExitDiagnostics|=bit;
    packDiagnostic("RESOURCEPACK_WORLD event=settings_exit session=%llu token=%llu phase=%u reason=%u event_type=%u interacted=%u id_match=%u fields=%u root_global=%u root_reload=%u child_global=%u child_reload=%u can_exit=%u imports_idle=%u event_alive=%u model=%u general=%u source=%u global_tab=%u settings_mode=%u owner=%u scene=%u global_unchanged=%u other_stacks=%u selected_known=%u old_count=%zu selected_count=%zu unchanged=%u",
        diagNumber(op.settingsSession),diagNumber(op.tx.token),unsigned(op.tx.phase),unsigned(reason),eventType,unsigned(interacted),unsigned(idMatch),unsigned(fields),
        unsigned(rootGlobal),unsigned(v.rootReload),unsigned(v.childGlobal),unsigned(v.childReload),unsigned(v.canExit),unsigned(v.importsIdle),unsigned(v.eventAlive),
        unsigned(v.modelPresent),unsigned(v.sameGeneral),unsigned(v.sourcePresent),unsigned(globalTab),unsigned(v.settingsMode),unsigned(v.ownerCurrent),unsigned(v.sameScene),
        unsigned(v.globalUnchanged),unsigned(v.otherStacksUnchanged),unsigned(selectedKnown),op.editorInitialGlobal.size(),selectedCount,unsigned(v.selectionUnchanged));
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsExitDispatcherHook,ll::memory::HookPriority::Normal,ScreenController,
    &ScreenController::$handleEvent,ui::ViewRequest,ScreenEvent& event){
    auto permit=settingsReplayPermit;
    if(permit.root==this&&permit.event==&event&&permit.operation&&active.load().get()==permit.operation
        &&permit.operation->tx.token==permit.token&&!permit.used){
        bool ownerCurrent=false;
        try{
            auto now=settingsOwner(*permit.model);
            auto top=permit.client->getCurrentSceneStack()->getTopSceneShared();
            ownerCurrent=permit.operation->exitReplaying&&permit.operation->tx.phase==WorldSwitchPhase::AwaitApply
                &&permit.owner.same(now)&&now.usable()&&top.get()==permit.scene
                &&static_cast<SettingsScreenController*>(permit.root)->getSceneId()==top->mSceneId;
        }catch(...){ownerCurrent=false;}
        {std::lock_guard lock(permit.operation->mutex);NativeSettingsRootExitView view;
            settingsExitDiagnostic(*permit.operation,ownerCurrent?NativeSettingsRootExitReason::Replay:NativeSettingsRootExitReason::ReplayOwnerChanged,
                view,unsigned(event.type),true,true,false,false,false,false,0);}
        if(nativeSettingsExitRoute(true,true,ownerCurrent,false,false)!=NativeSettingsExitRoute::OriginReplay)
            return ui::ViewRequest::ConsumeEvent;
        // Owner-verified replay: prearm both peers before the actual vanilla
        // UI event executes its private resource-pack setter and compose path.
        auto current=active.load();
        if(!current||current.get()!=permit.operation||
           !armR26NativePeers(*current,"vanilla_settings_before_replayed_event")){
            if(current){std::lock_guard lock(current->mutex);current->tx.seal();}
            return ui::ViewRequest::ConsumeEvent;
        }
        settingsReplayPermit.used=true;
        NativeSettingsEditorPause editing;
        NativeSettingsPackScope scope(current->settingsContext);
        return origin(event);
    }
    NativeHookLifetime::Lease lease(lifetime);
    auto op=lease.admitted()?active.load():nullptr;
    auto rootHeld=op&&op->nativeSettings?op->settingsRoot.lock():nullptr;
    if(!rootHeld||static_cast<ScreenController*>(rootHeld.get())!=this)return origin(event);
    NativeSettingsRootExitView view;
    bool button=event.type==ScreenEventType::ButtonEvent;
    bool interacted=button&&event.data->button->isInteracted,idMatch=button&&event.data->button->id==SettingsMenuExitButton;
    view.exitEvent=button&&interacted&&idMatch;
    view.ownerThread=GetCurrentThreadId()==ownerThread.load();
    auto diagnostic=[&](NativeSettingsRootExitReason reason,bool fields=false,bool rootGlobal=false,bool globalTab=false,
        bool selectedKnown=false,std::size_t selectedCount=0){settingsExitDiagnostic(*op,reason,view,unsigned(event.type),interacted,idMatch,fields,rootGlobal,globalTab,selectedKnown,selectedCount);};
    if(!view.exitEvent||!view.ownerThread){
        {std::lock_guard lock(op->mutex);diagnostic(nativeSettingsRootExit(view).reason);}
        return view.exitEvent?ui::ViewRequest::ConsumeEvent:origin(event);
    }
    {
        std::lock_guard lock(op->mutex);
        view.pending=bool(op->pendingExit)||op->exitReplaying||op->exitReplayed
            ||op->tx.phase!=WorldSwitchPhase::Idle;
        if(view.pending){diagnostic(NativeSettingsRootExitReason::Pending);return ui::ViewRequest::ConsumeEvent;}
    }
    // canExit can enter a child hook; never query it under the operation lock.
    auto& currentRoot=*static_cast<SettingsScreenController*>(rootHeld.get());
    std::shared_ptr<GeneralSettingsScreenController> general=currentRoot.mGeneralSettingsScreenController;
    std::shared_ptr<MainMenuScreenModel> model=currentRoot.mMainMenuScreenModel;
    view.rootReload=currentRoot.mReloadTexturePacksOnExit;view.generalPresent=bool(general);
    bool predicateException{};
    try{view.canExit=currentRoot.canExit();view.importsIdle=general&&!nativeSettingsImportsPending(*general);}
    catch(...){predicateException=true;}
    view.eventAlive=!(event.data->button->properties&&event.lifetimeCheck.expired());
    view.modelPresent=bool(model);view.sameGeneral=general&&address(general.get())==op->settingsContext.controller;
    view.sourcePresent=general&&general->mResourceContentSource;view.childGlobal=general&&general->mIsGlobalSettings;
    view.childReload=general&&general->mReloadTexturePacksOnExit;
    view.settingsMode=currentRoot.mSettingsMode==SettingsScreenMode::Settings;
    bool rootGlobal=currentRoot.mIsGlobalSettingsScreen,globalTab=currentRoot.mCurrentTab==SettingsTabIndex::GlobalResourcesTab;
    NativeSettingsPendingHolds stale;
    std::unique_lock lock(op->mutex);
    view.pending=bool(op->pendingExit)||op->exitReplaying||op->exitReplayed
        ||op->tx.phase!=WorldSwitchPhase::Idle;
    bool selectedKnown{};std::size_t selectedCount{};
    try{
        auto owner=model?settingsOwner(*model):WorldPackOwner{};
        view.ownerCurrent=active.load()==op&&op->editorOwner.same(owner)&&owner.usable();view.revisionValid=revision.load()!=0;
        auto scene=view.ownerCurrent&&op->client?op->client->getCurrentSceneStack()->getTopSceneShared():nullptr;
        view.sameScene=scene&&scene->mSceneId==op->settingsSceneId&&currentRoot.getSceneId()==op->settingsSceneId;
        if(view.ownerCurrent&&view.sameScene){auto& manager=model->getResourcePackManager();
            view.globalUnchanged=keys(ids(manager.mGlobalStack.get()))==op->editorInitialGlobal;view.otherStacksUnchanged=otherStacks(*op,manager);}
        auto decision=nativeSettingsRootExit(view);
        std::vector<std::string> target;
        std::vector<PackInstanceId> selected;
        if(decision.route==NativeSettingsExitRoute::Inspect&&!predicateException){
            auto observed=nativeSelectedGlobalPacks(*general->mResourceContentSource,op->oldIds);
            if(!observed)throw std::runtime_error("native selected resource-pack order cannot be verified");
            selected=std::move(*observed);
            target=keys(selected);selectedKnown=true;selectedCount=target.size();view.selectionUnchanged=target==op->editorInitialGlobal;
            decision=nativeSettingsRootExit(view);
        }
        if(predicateException)decision={NativeSettingsRootExitReason::Exception,NativeSettingsExitRoute::Consume};
        diagnostic(decision.reason,true,rootGlobal,globalTab,selectedKnown,selectedCount);
        if(decision.route==NativeSettingsExitRoute::OriginNormal){lock.unlock();return origin(event);}
        if(decision.route==NativeSettingsExitRoute::Consume)return ui::ViewRequest::ConsumeEvent;
        auto change=nativeVanillaPackChange(op->editorInitialGlobal,target);
        auto delta=change.delta;
        bool verified=r29ValidateNativeSelection(*op,selected,delta);
        packDiagnostic("RESOURCEPACK_R29 event=vanilla_selection delta=%u index=%zu original=%zu selected=%zu clone=%u verified=%u owned=%zu",
            unsigned(delta),change.index,op->editorInitialGlobal.size(),target.size(),
            unsigned(op->r24AuthorizedClone),unsigned(verified),op->ownedPacks.size());
        if(!verified)return ui::ViewRequest::ConsumeEvent;
        auto owned=std::make_unique<NativeSettingsExitEvent>(event,SettingsMenuExitButton);
        if(!owned->alive()){
            op->ownedPacks.clear();
            op->r27ToggleKind=R27LocalToggle::Invalid;
            diagnostic(NativeSettingsRootExitReason::CopyExpired,true,rootGlobal,globalTab,true,selectedCount);
            return ui::ViewRequest::ConsumeEvent;
        }
        if(!op->tx.begin(titlePackControl().reserveWorldOperationToken(),owner,titlePackNow())){
            op->ownedPacks.clear();
            op->r27ToggleKind=R27LocalToggle::Invalid;
            diagnostic(NativeSettingsRootExitReason::BeginRejected,true,rootGlobal,globalTab,true,selectedCount);return ui::ViewRequest::ConsumeEvent;}
        op->tx.prepared(op->editorInitialGlobal,target);
        op->settingsApplicationDiagnostics=0;
        op->pendingExit=std::move(owned);op->pendingSelection=std::move(target);
        op->pendingRevision=revision.load();op->pendingRoot=rootHeld;op->pendingModel=model;op->pendingTab=currentRoot.mCurrentTab;
        op->pendingScene=scene;op->settingsScene=scene;op->settingsRetryRoot=rootHeld;op->settingsRetryModel=model;
        op->pendingSource=address(static_cast<PackManagerContentSourceImpl*>(general->mResourceContentSource));
        op->settingsRetrySource=op->pendingSource;
        pause(*op,titlePackNow());
        if(op->tx.phase!=WorldSwitchPhase::Pausing){stale=takePendingSettingsExit(*op);op->tx.cancel();}
        return ui::ViewRequest::ConsumeEvent;
    }catch(...){if(!lock.owns_lock())throw;diagnostic(NativeSettingsRootExitReason::Exception,true,rootGlobal,globalTab,selectedKnown,selectedCount);
        stale=takePendingSettingsExit(*op);op->tx.cancel();return ui::ViewRequest::ConsumeEvent;}
}
// Both callbacks remain 26.60 virtual functions even though their $-named
// thunks are no longer exported. Resolve the exact vtable entry only from
// the native settings controllers constructed by the vanilla engine.
struct WorldSettingsRootOpenedHook : SettingsScreenController {
    static inline ll::memory::FuncPtr nativeTarget{};
    static inline void (*nativeOrigin)(SettingsScreenController*){};
    void origin() { nativeOrigin(this); }
    static int hook(SettingsScreenController& root) {
        if (nativeOrigin) return 0;
        auto slot=native2660::zeroAdjustVirtualSlot(&SettingsScreenController::onOpen);
        if (!slot) return -11;
        if (*slot>1024) return -12;
        auto table=*reinterpret_cast<void* const* const*>(&root);
        if (!table || !table[*slot]) return -13;
        nativeTarget=table[*slot];
        if(ll::memory::hookEx(nativeTarget,ll::memory::toFuncPtr(&WorldSettingsRootOpenedHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),ll::memory::HookPriority::Normal,
            ll::memory::SaveNone,true)!=0) {
            nativeTarget=nullptr;
            return -14;
        }
        return 0;
    }
    void detour() {
        NativeHookLifetime::Lease lease(lifetime);
        origin();
        if(!lease.admitted()||GetCurrentThreadId()!=ownerThread.load())return;
        auto general=mGeneralSettingsScreenController.get();
        auto op=general?settingsOperation(address(general)):nullptr;
        if(!op||op->settingsInitRoot!=address(this))return;
        auto weak=static_cast<MinecraftScreenController*>(this)->weak_from_this();
        {std::lock_guard lock(op->mutex);op->settingsRoot=weak;op->settingsSceneId=getSceneId();}
        packDiagnostic("RESOURCEPACK_WORLD event=settings_editor session=%llu bound=%u global_tab=%u fullscreen=%u root_global=%u root_reload=%u",
            diagNumber(op->settingsSession),unsigned(!weak.expired()),unsigned(mCurrentTab==SettingsTabIndex::GlobalResourcesTab),
            unsigned(mFullscreen),unsigned(mIsGlobalSettingsScreen),unsigned(mReloadTexturePacksOnExit));
    }
};
struct WorldSettingsCanExitHook : GeneralSettingsScreenController {
    static inline ll::memory::FuncPtr nativeTarget{};
    static inline bool (*nativeOrigin)(GeneralSettingsScreenController*){};
    bool origin() { return nativeOrigin(this); }
    static int hook(GeneralSettingsScreenController& general) {
        if (nativeOrigin) return 0;
        auto slot=native2660::zeroAdjustVirtualSlot(&GeneralSettingsScreenController::canExit);
        if (!slot) return -11;
        if (*slot>1024) return -12;
        auto table=*reinterpret_cast<void* const* const*>(&general);
        if (!table || !table[*slot]) return -13;
        nativeTarget=table[*slot];
        if(ll::memory::hookEx(nativeTarget,ll::memory::toFuncPtr(&WorldSettingsCanExitHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),ll::memory::HookPriority::Normal,
            ll::memory::SaveNone,true)!=0) {
            nativeTarget=nullptr;
            return -14;
        }
        return 0;
    }
    bool detour() {
        NativeHookLifetime::Lease lease(lifetime);
        bool result=origin();
        if(!lease.admitted()||!result)return result;
        auto op=settingsOperation(address(this));
        if(!op)return result;
        std::lock_guard lock(op->mutex);
        return op->tx.phase!=WorldSwitchPhase::Pausing&&op->tx.phase!=WorldSwitchPhase::Preparing;
    }
};
// 26.60 does not export the constructor or private _init thunks used by
// 26.51. Inspect a REAL Settings scene/controller from the native stack
// instead of constructing a replacement UI or replaying private _init.
// Never adopt a scene whose preconstructed native content source does not
// show exactly the captured original Global pack identities.
bool observeNativeSettingsScene(AbstractScene& scene,SettingsScreenController& root) noexcept {
    if(!installed.load()||GetCurrentThreadId()!=ownerThread.load()
       ||root.mSettingsMode!=SettingsScreenMode::Settings
       ||root.mLegacySyncOnly || !root.mMainMenuScreenModel
       ||!root.mGeneralSettingsScreenController
       ||root.getSceneId()!=scene.mSceneId)
        return false;
    auto general=root.mGeneralSettingsScreenController;
    if(!general->mResourceContentSource)return false;
    auto previous=active.load();
    if(previous&&previous->nativeSettings
       &&previous->settingsInitRoot==address(&root))return true;
    std::shared_ptr<IClientInstance> client;
    WorldPackOwner owner;
    try{owner=settingsOwner(*root.mMainMenuScreenModel,&client);}
    catch(...){return false;}
    if(!owner.usable()||!client)return false;
    WorldSettingsRootInitInvocation initialization{
        &root,root.mMainMenuScreenModel,client,owner,{},true
    };
    WorldSettingsRootInitScope initializationScope(std::move(initialization));
    unsigned reason{};
    if(!beginSettings(general.get(),*root.mMainMenuScreenModel,reason))
        return false;
    auto op=worldSettingsRootInitInvocation.operation;
    if(!op)return false;
    bool parentGlobal=root.mIsGlobalSettingsScreen;
    bool parentReload=root.mReloadTexturePacksOnExit;
    bool childGlobal=general->mIsGlobalSettings;
    bool childReload=general->mReloadTexturePacksOnExit;
    auto cancel=[&](char const* failure) noexcept {
        root.mIsGlobalSettingsScreen=parentGlobal;
        root.mReloadTexturePacksOnExit=parentReload;
        general->mIsGlobalSettings=childGlobal;
        general->mReloadTexturePacksOnExit=childReload;
        {std::lock_guard lock(op->mutex);op->tx.cancel();}
        if(active.load()==op)active.store(previous);
        packDiagnostic("RESOURCEPACK_WORLD event=settings_scene_observe denied=%s",failure);
    };
    try {
        auto selected=nativeSelectedGlobalPacks(*general->mResourceContentSource,op->oldIds);
        if(!selected || keys(*selected)!=op->editorInitialGlobal){
            cancel("source_is_not_native_global_stack");return false;
        }
        // The native UI already owns this Resource content source. Alter
        // only its Global/Reload admission flags, never construct a new
        // controller, replace its source, or invoke a private initializer.
        root.mIsGlobalSettingsScreen=true;
        root.mReloadTexturePacksOnExit=true;
        general->mIsGlobalSettings=true;
        general->mReloadTexturePacksOnExit=true;
        NativeSettingsRootReloadView view{};
        view.initializationOwned=true;
        view.sameModel=root.mMainMenuScreenModel==worldSettingsRootInitInvocation.model;
        view.sameGeneral=address(general.get())==op->settingsContext.controller;
        view.ownerCurrent=op->editorOwner.same(owner)
            &&client==op->client&&worldGeneration.load()==owner.world;
        view.settingsMode=root.mSettingsMode==SettingsScreenMode::Settings
            &&!root.mLegacySyncOnly;
        auto& manager=root.mMainMenuScreenModel->getResourcePackManager();
        view.globalPolicy=nativeSettingsMayIncludeGlobal(
            manager.mUseGlobalPackStack,!isVerifiedIntegratedWorld(*client),
            ids(manager.mLevelStack.get()).empty(),op->r24AuthorizedClone);
        view.childGlobal=general->mIsGlobalSettings;
        view.childReload=general->mReloadTexturePacksOnExit;
        view.sourcePresent=bool(general->mResourceContentSource);
        if(!nativeSettingsRootReload(false,view)){
            cancel("root_policy");return false;
        }
        if(WorldSettingsRootOpenedHook::hook(root)!=0
            ||WorldSettingsCanExitHook::hook(*general)!=0){
            cancel("native_virtual_hook");return false;
        }
        auto weak=static_cast<MinecraftScreenController*>(&root)->weak_from_this();
        if(weak.expired()){cancel("controller_not_shared");return false;}
        {std::lock_guard lock(op->mutex);
            op->settingsRoot=weak;
            op->settingsInitRoot=address(&root);
            op->settingsSceneId=root.getSceneId();
        }
        packDiagnostic("RESOURCEPACK_WORLD event=settings_scene_observe bound=1 session=%llu original_global=%u original_reload=%u",
            diagNumber(op->settingsSession),unsigned(parentGlobal),unsigned(parentReload));
        return true;
    }catch(...){cancel("scene_exception");return false;}
}
enum class SettingsApplicationStage {StackEnter,StackReturn,StackThrow,GlobalEnter,GlobalReturn,GlobalThrow,
    NavigationEnter,NavigationReturn,NavigationThrow,NavigationDenied,CallbackAdmission,CallbackPrepared,
    CallbackReturn,CallbackThrow,CallbackStale,CallbackDuplicate};
void settingsApplicationDiagnostic(std::shared_ptr<Operation> const& op,SettingsApplicationStage stage,
    bool scoped,unsigned reason=0,std::size_t count=0) noexcept {
    if(!op)return;std::lock_guard lock(op->mutex);auto bit=std::uint32_t{1}<<unsigned(stage);
    if(op->settingsApplicationDiagnostics&bit)return;op->settingsApplicationDiagnostics|=bit;
    packDiagnostic("RESOURCEPACK_WORLD event=settings_application session=%llu token=%llu attempt=%llu phase=%u stage=%u scoped=%u reason=%u count=%zu navigation=%u",
        diagNumber(op->settingsSession),diagNumber(op->tx.token),diagNumber(op->tx.attempt),unsigned(op->tx.phase),unsigned(stage),unsigned(scoped),reason,count,unsigned(op->nativeNavigation));
}
// 26.60 exposes isPreGame as a virtual method, but not the legacy
// $isPreGame thunk. Install against the actual ClientInstance vtable.
struct WorldSettingsClientPreGameHook : ClientInstance {
    static inline bool (*nativeOrigin)(ClientInstance const*){};
    static inline ll::memory::FuncPtr nativeTarget{};
    bool origin() const { return nativeOrigin(this); }
    static int hook(ClientInstance& client) {
        if (nativeOrigin) return 0;
        auto slot=native2660::zeroAdjustVirtualSlot(&ClientInstance::isPreGame);
        if (!slot) return -11;
        if (*slot>1024) return -12;
        auto table=*reinterpret_cast<void* const* const*>(&client);
        if (!table || !table[*slot]) return -13;
        nativeTarget=table[*slot];
        if (ll::memory::hook(nativeTarget,ll::memory::toFuncPtr(&WorldSettingsClientPreGameHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),ll::memory::HookPriority::Normal)!=0) {
            nativeTarget=nullptr;
            return -14;
        }
        return 0;
    }
    bool detour() const {
    bool result=origin();NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return result;
    auto* client=static_cast<IClientInstance*>(const_cast<WorldSettingsClientPreGameHook*>(this));
    return nativeSettingsPreGame(result,address(client),worldGeneration.load())
        ||settingsEditorPreGame(result,client)||verifiedNativeGlobalPagePreGame(result,client);
    }
};
// Both private GeneralSettings global stack hooks are not exported in 26.60.
// The owner-verified native exit replay arms both peers before the vanilla
// controller executes any setters. Guard the native stack mutation boundary.
LL_TYPE_INSTANCE_HOOK(WorldManagerStackHook,ll::memory::HookPriority::Normal,
    ResourcePackManager,&ResourcePackManager::setStack,bool,
    std::unique_ptr<ResourcePackStack> stack,ResourcePackStackType type) {
    NativeHookLifetime::Lease lease(lifetime);
    auto op=lease.admitted()?active.load():nullptr;
    if(!op||!op->nativeSettings||type!=ResourcePackStackType::Global
       ||address(this)!=op->settingsContext.manager)
        return origin(std::move(stack),type);
    bool allowed=false;
    bool exact=false;
    try {
        auto permit=settingsReplayPermit;
        auto context=nativeSettingsPackContext;
        bool fromReplay=permit.operation==op.get()&&permit.used
            &&permit.token==op->tx.token
            &&permit.client==op->client.get()
            &&permit.model&&permit.owner.same(op->tx.owner);
        bool fromOwnedApply=context.valid()
            &&context.controller==op->settingsContext.controller
            &&context.client==op->settingsContext.client
            &&context.manager==op->settingsContext.manager
            &&context.world==op->settingsContext.world;
        std::lock_guard lock(op->mutex);
        allowed=op->r24AuthorizedClone && op->r26PeersNativeArmed
            &&op->tx.busy() && GetCurrentThreadId()==op->editorOwner.thread
            &&(fromReplay||fromOwnedApply);
        if(allowed && stack)
            exact=keys(ids(stack.get()))==op->tx.targetGlobal;
    }catch(...){allowed=exact=false;}
    settingsApplicationDiagnostic(op,SettingsApplicationStage::StackEnter,allowed && exact);
    if(!allowed||!exact) {
        std::lock_guard lock(op->mutex);
        op->tx.seal();
        packDiagnostic("RESOURCEPACK_WORLD event=manager_global_stack_denied admission=%u exact=%u",
            unsigned(allowed),unsigned(exact));
        return false;
    }
    try {
        NativeSettingsPackScope scope(op->settingsContext);
        auto result=origin(std::move(stack),type);
        settingsApplicationDiagnostic(op,SettingsApplicationStage::StackReturn,true,unsigned(result));
        if(!result){std::lock_guard lock(op->mutex);op->tx.seal();}
        return result;
    }catch(...){
        settingsApplicationDiagnostic(op,SettingsApplicationStage::StackThrow,true);
        std::lock_guard lock(op->mutex);op->tx.seal();
        throw;
    }
}
struct WorldApplicationTickInvocation {
    std::shared_ptr<Operation> operation;
    std::shared_ptr<MainMenuScreenModel> model;
    std::shared_ptr<IClientInstance> client;
    std::shared_ptr<AbstractScene> scene;
    std::uintptr_t controller{};
    std::uint64_t tick{};
    WorldNativeApplicationReceipt::TickPermit permit;
};
thread_local WorldApplicationTickInvocation worldApplicationTickInvocation;
std::atomic<std::uint64_t> applicationTicks{};
struct WorldApplicationTickScope {
    WorldApplicationTickInvocation previous;
    explicit WorldApplicationTickScope(WorldApplicationTickInvocation value):previous(std::move(worldApplicationTickInvocation)){
        worldApplicationTickInvocation=std::move(value);
    }
    ~WorldApplicationTickScope(){worldApplicationTickInvocation=std::move(previous);}
};
bool applicationOwnerCurrent(WorldApplicationTickInvocation const& invocation) noexcept {
    auto const& id=invocation.permit.identity;
    if(!invocation.operation||!invocation.model||!invocation.client||!id
        ||GetCurrentThreadId()!=id.thread||worldGeneration.load()!=id.world||!runtimeReady.load()
        ||address(observedGame.load())!=id.game||active.load()!=invocation.operation)return false;
    try{
        auto client=nativeSettingsClient(*invocation.model,*observedGame.load(),id.client);
        return client.get()==invocation.client.get()&&address(client.get())==id.client&&client->getLocalPlayer()
            &&address(&invocation.model->getResourcePackManager())==invocation.operation->settingsContext.manager;
    }catch(...){return false;}
}
// This exact name has no 26.60 prelink entry yet. Resolve from the
// engine's trusted symbol table only; never hook a fabricated local stub.
// The full native world UI transaction stays blocked when absent.
struct WorldApplyingLoadingHook : MinecraftScreenModel {
    static inline ll::memory::FuncPtr nativeTarget{};
    static inline bool (*nativeOrigin)(MinecraftScreenModel const*){};
    static int hook() {
        if(nativeOrigin)return 0;
        nativeTarget=reinterpret_cast<ll::memory::FuncPtr>(native2660::resolveLoadingGetter());
        if(!nativeTarget)return -1;
        if(ll::memory::hookEx(nativeTarget,
            ll::memory::toFuncPtr(&WorldApplyingLoadingHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),
            ll::memory::HookPriority::Normal,ll::memory::SaveNone,true)!=0) {
            nativeTarget=nullptr;
            return -1;
        }
        return 0;
    }
    bool detour() const {
        NativeHookLifetime::Lease lease(lifetime);
        bool result=nativeOrigin(this);
        auto invocation=worldApplicationTickInvocation;
        if(lease.admitted()&&invocation.permit&&applicationOwnerCurrent(invocation)) {
            auto op=invocation.operation;std::lock_guard lock(op->mutex);
            if(op->tx.phase==WorldSwitchPhase::Reloading&&op->tx.token==invocation.permit.identity.token
                &&op->tx.attempt==invocation.permit.identity.attempt)
                op->executed.loadingQueried(invocation.permit,address(this),result);
        }
        return result;
    }
};
// 26.60 no longer exports ResourcePackApplyingScreenController::$tick.
// This virtual member can be hooked from an engine-owned *actual* applying
// controller when it is published to its native scene stack. Registering
// observers does not fabricate a completion receipt.
struct WorldApplyingTickHook : ResourcePackApplyingScreenController {
    static inline ll::memory::FuncPtr nativeTarget{};
    static inline ui::DirtyFlag (*nativeOrigin)(ResourcePackApplyingScreenController*){};
    static inline std::weak_ptr<IClientInstance> registeredClient{};
    static inline unsigned char callbackToken{};
    ui::DirtyFlag origin() { return nativeOrigin(this); }
    static int hookController(ResourcePackApplyingScreenController& controller) {
        if(nativeOrigin)return 0;
        auto slot=native2660::zeroAdjustVirtualSlot(&ResourcePackApplyingScreenController::tick);
        if(!slot)return -11;
        if(*slot>1024)return -12;
        auto table=*reinterpret_cast<void* const* const*>(&controller);
        if(!table||!table[*slot])return -13;
        nativeTarget=table[*slot];
        if(ll::memory::hookEx(nativeTarget,ll::memory::toFuncPtr(&WorldApplyingTickHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),ll::memory::HookPriority::Normal,
            ll::memory::SaveNone,true)!=0) {
            nativeTarget=nullptr;
            return -14;
        }
        return 0;
    }
    static void inspectScene(AbstractScene& scene) noexcept {
        if(!installed.load()||GetCurrentThreadId()!=ownerThread.load())return;
        // The copied SDK's cross-module RTTI currently throws bad_typeid
        // for live 26.60 scene stack entries. Record the real native
        // scene category before ANY controller cast. No pack flags,
        // scene lifetimes or renderer state are modified by this probe.
        try {
            // SceneType::SettingsScene is NOT guaranteed for current OreUI
            // routes. Inspect the first 16 distinct screen pushes, regardless
            // of their scene category, without reading or logging user data.
            // In 26.60 the C++ dynamic_cast on cross-module scenes throws
            // bad_typeid; this probe uses only the native virtual interface.
            static std::array<unsigned,16> seen{};
            static unsigned count{};
            auto id=unsigned(scene.mSceneId);
            if(count<seen.size() && std::find(seen.begin(),seen.begin()+count,id)==seen.begin()+count) {
                seen[count++]=id;
                auto type=unsigned(scene.getSceneType());
                auto raw=scene.getRawScreenName();
                auto route=scene.getRoute();
                packDiagnostic("RESOURCEPACK_WORLD event=native_scene_seen scene=%u type=%u settings_bit=%u oreui_bit=%u name_options=%u name_settings=%u name_packs=%u route_settings=%u",
                    id,type,unsigned(bool(type&unsigned(ui::SceneType::SettingsScene))),
                    unsigned(bool(type&unsigned(ui::SceneType::OreUi))),
                    unsigned(raw.find("options")!=std::string::npos),
                    unsigned(raw.find("settings")!=std::string::npos),
                    unsigned(raw.find("pack")!=std::string::npos),
                    unsigned(route.find("settings")!=std::string::npos));
            }
        } catch(...) {packDiagnostic("RESOURCEPACK_WORLD event=native_scene_probe_unavailable");}
        try {
            auto* ui=dynamic_cast<UIScene*>(&scene);
            if(!ui||!ui->mScreenView.get())return;
            auto controller=ui->mScreenView->mController;
            if(auto* settings=controller?dynamic_cast<SettingsScreenController*>(controller.get()):nullptr)
                observeNativeSettingsScene(scene,*settings);
            auto* applying=controller?dynamic_cast<ResourcePackApplyingScreenController*>(controller.get()):nullptr;
            if(!applying)return;
            auto code=hookController(*applying);
            packDiagnostic("RESOURCEPACK_WORLD event=applying_tick_virtual_bind code=%d scene=%u",
                code,unsigned(scene.mSceneId));
            if(code)lost=true;
        } catch(std::bad_typeid const&) {
            packDiagnostic("RESOURCEPACK_WORLD event=applying_tick_virtual_no_rtti");
        } catch(...) {
            packDiagnostic("RESOURCEPACK_WORLD event=applying_tick_virtual_exception");
        }
    }
    static int hook() {
        try {
            auto* game=observedGame.load();
            auto requested=observedClient.load();
            if(!game||!requested||GetCurrentThreadId()!=ownerThread.load())return -1;
            std::shared_ptr<IClientInstance> client;
            for(auto const& [sub,owned]:game->getClientInstanceMap())
                if(owned && address(owned.get())==requested) {client=owned;break;}
            if(!client)return -1;
            if(auto previous=registeredClient.lock();previous) {
                if(previous.get()==client.get())return 0;
                // Another screen-stack owner would make the retained callback
                // token stale: refuse rather than overwrite its lifecycle.
                return -1;
            }
            auto stack=client->getCurrentSceneStack();
            stack->registerPrePushSceneCallback(&callbackToken,[](AbstractScene& scene){inspectScene(scene);});
            stack->registerPushSceneCallback(&callbackToken,
                [](std::shared_ptr<AbstractScene> scene,std::optional<OreUI::RouteAction>) {
                    if(scene)inspectScene(*scene);
                });
            registeredClient=client;
            if(auto scene=stack->getTopSceneShared())inspectScene(*scene);
            return 0;
        }catch(...){return -1;}
    }
    static void unsubscribe() noexcept {
        if(auto client=registeredClient.lock())try {
            auto stack=client->getCurrentSceneStack();
            stack->unregisterPrePushSceneCallback(&callbackToken);
            stack->unregisterPushSceneCallback(&callbackToken);
        }catch(...){}
        registeredClient.reset();
    }
    ui::DirtyFlag detour() {
TitleCallbackLease lease(lifetime,callbacks);WorldApplicationTickInvocation invocation;
    // Copy shared ownership and scene identity before origin. Native tick can
    // leave/destroy its screen; no naked controller access is made afterwards.
    std::shared_ptr<MinecraftScreenController> heldController;
    if(lease.admitted()&&GetCurrentThreadId()==ownerThread.load())try{
        auto op=active.load();std::shared_ptr<MainMenuScreenModel> model=mMainMenuScreenModel;
        auto held=weak_from_this().lock();
        if(op&&op->apiReload&&model&&held.get()==static_cast<MinecraftScreenController*>(this)){
            WorldPackOwner expected;bool eligible{};
            {std::lock_guard lock(op->mutex);expected=op->tx.owner;
                eligible=op->nativeNavigation&&(op->tx.phase==WorldSwitchPhase::AwaitApply||op->tx.phase==WorldSwitchPhase::Reloading);}
            auto* game=observedGame.load();
            if(eligible&&game&&address(game)==expected.game&&runtimeReady.load()&&worldGeneration.load()==expected.world){
                auto client=nativeSettingsClient(*model,*game,expected.client);
                auto scene=client?client->getCurrentSceneStack()->getTopSceneShared():nullptr;
                if(client&&client.get()==op->client.get()&&scene&&scene->mSceneId==getSceneId()
                    &&address(&model->getResourcePackManager())==expected.manager){
                    invocation={op,model,client,scene,address(this),applicationTicks.fetch_add(1)+1,{}};
                    auto baseModel=address(static_cast<MinecraftScreenModel*>(model.get()));
                    {std::lock_guard lock(op->mutex);auto id=op->executed.identity();
                        if(id&&id.model==baseModel&&op->applicationScene.lock().get()==scene.get())
                            invocation.permit=op->executed.enterTick(id,address(this),address(scene.get()),invocation.tick);}
                    heldController=std::move(held);
                }
            }
        }
    }catch(...){invocation={};}
    WorldApplicationTickScope scope(std::move(invocation));
    ui::DirtyFlag result{};
    try{result=origin();}catch(...){
        auto current=worldApplicationTickInvocation;
        if(current.permit&&active.load()==current.operation){std::lock_guard lock(current.operation->mutex);
            current.operation->executed.tickReturned(current.permit,false,false,false);current.operation->tx.seal();}
        throw;
    }
    auto current=worldApplicationTickInvocation;
    if(current.permit){
        bool ownerCurrent=applicationOwnerCurrent(current),sceneExited=false;
        if(ownerCurrent)try{sceneExited=current.client->getCurrentSceneStack()->getTopSceneShared().get()!=current.scene.get();}
        catch(...){ownerCurrent=false;}
        auto op=current.operation;std::lock_guard lock(op->mutex);
        if(active.load()==op&&op->tx.token==current.permit.identity.token&&op->tx.attempt==current.permit.identity.attempt){
            bool ready=op->executed.tickReturned(current.permit,ownerCurrent,true,sceneExited);
            if(op->executed.sealed())op->tx.seal();
            else if(!op->apiReload&&ready&&op->tx.phase==WorldSwitchPhase::Reloading&&!op->tx.finished){
                op->tx.finish(op->tx.attempt,true);
                packDiagnostic("RESOURCEPACK_WORLD event=native_application token=%llu attempt=%llu tick=%llu callback_normal=1 loading=1 scene_exit=1",
                    diagNumber(op->tx.token),diagNumber(op->tx.attempt),diagNumber(current.tick));
            }
        }
    }
    return result;
    }
};
// Client-native navigation hook also comes from the same exact runtime
// decorated-name symbol. Do not synthesize a setter or progress callback.
struct WorldSettingsApplyHook : MainMenuScreenModel {
    using Apply=std::function<void(MinecraftScreenModel&)>;
    static inline ll::memory::FuncPtr nativeTarget{};
    static inline void (*nativeOrigin)(MainMenuScreenModel*,Apply){};
    static int hook() {
        if(nativeOrigin)return 0;
        nativeTarget=reinterpret_cast<ll::memory::FuncPtr>(native2660::resolveTitleNavigate());
        if(!nativeTarget)return -1;
        if(ll::memory::hookEx(nativeTarget,
            ll::memory::toFuncPtr(&WorldSettingsApplyHook::detour),
            reinterpret_cast<ll::memory::FuncPtr*>(&nativeOrigin),
            ll::memory::HookPriority::Normal,ll::memory::SaveNone,true)!=0) {
            nativeTarget=nullptr;
            return -1;
        }
        return 0;
    }
    void origin(Apply apply) {nativeOrigin(this,std::move(apply));}
    void detour(Apply apply) {
    NativeHookLifetime::Lease lease(lifetime);auto context=nativeSettingsPackContext;
    auto op=lease.admitted()&&context.valid()?settingsOperation(context.controller):nullptr;
    // The fallback is observation only: an unscoped vanilla navigation cannot
    // acquire the candidate's apply permission merely by sharing its model.
    auto observed=op;
    if(!observed&&lease.admitted()&&GetCurrentThreadId()==ownerThread.load())try{
        auto candidate=active.load();auto* game=observedGame.load();
        if(candidate&&candidate->nativeSettings&&game&&runtimeReady.load()
            &&worldGeneration.load()==candidate->editorOwner.world
            &&nativeSettingsClient(*this,*game,candidate->settingsContext.client)==candidate->client)observed=candidate;
    }catch(...){}
    settingsApplicationDiagnostic(observed,SettingsApplicationStage::NavigationEnter,bool(op),unsigned(bool(apply)));
    if(!op||!apply){
        try{origin(std::move(apply));}catch(...){settingsApplicationDiagnostic(observed,SettingsApplicationStage::NavigationThrow,false);throw;}
        settingsApplicationDiagnostic(observed,SettingsApplicationStage::NavigationReturn,false);return;
    }
    // Allocate before moving origin. Failure preserves its exact captures.
    struct Body {NativeSettingsApplyOnce once;std::function<void(MinecraftScreenModel&)> original;};
    std::shared_ptr<Body> body;std::function<void(MinecraftScreenModel&)> wrapped;
    try{
        body=std::make_shared<Body>();
        wrapped=[op,body,context](MinecraftScreenModel& model){
            NativeSettingsApplyAdmission admission;
            {
                std::lock_guard lock(op->mutex);
                auto const& live=op->settingsContext;
                bool contextCurrent=active.load().get()==op.get()&&context.controller==live.controller
                    &&context.client==live.client&&context.manager==live.manager&&context.world==live.world;
                admission=nativeSettingsApplyAdmission(body->once,op->tx.token,worldGeneration.load(),contextCurrent);
                if(admission==NativeSettingsApplyAdmission::Duplicate)op->tx.seal();
            }
            settingsApplicationDiagnostic(op,admission==NativeSettingsApplyAdmission::Stale?SettingsApplicationStage::CallbackStale:
                admission==NativeSettingsApplyAdmission::Duplicate?SettingsApplicationStage::CallbackDuplicate:SettingsApplicationStage::CallbackAdmission,
                admission==NativeSettingsApplyAdmission::Execute,unsigned(admission));
            if(admission!=NativeSettingsApplyAdmission::Execute)return;
            if(op->apiReload){
                NativeSettingsPackScope settings(context);
                runWorldSelection(op,model,[&]{body->original(model);});return;
            }
            TitleCallbackLease lease(lifetime,callbacks);WorldNativeContext seed;bool scoped=false;
            unsigned callbackReason{};
            std::shared_ptr<MainMenuScreenModel> previousModel;
            try{
                auto owner=settingsOwner(model);std::lock_guard lock(op->mutex);
                auto admit=[&](unsigned reason,bool pass){if(!pass&&!callbackReason)callbackReason=reason;return pass;};
                if(admit(1,lease.admitted())&&admit(2,active.load().get()==op.get())&&admit(3,op->tx.callback(owner,titlePackNow()))
                    &&admit(4,ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch))
                    &&admit(5,keys(ids(model.getResourcePackManager().mGlobalStack.get()))==op->tx.oldGlobal)
                    &&admit(6,otherStacks(*op,model.getResourcePackManager()))){
                    auto& tick=worldApplicationTickInvocation;
                    auto modelAddress=address(&model);
                    WorldNativeApplicationReceipt::Identity id{op->tx.token,op->tx.attempt,owner.world,owner.thread,owner.game,owner.client,modelAddress};
                    if(admit(7,tick.operation==op&&tick.model&&tick.client.get()==op->client.get()
                        &&address(static_cast<MinecraftScreenModel*>(tick.model.get()))==modelAddress
                        &&tick.scene)&&admit(8,op->executed.prepare(id))){
                        tick.permit=op->executed.enterTick(id,tick.controller,address(tick.scene.get()),tick.tick);
                        if(admit(9,bool(tick.permit)&&op->executed.callbackEntered(tick.permit))
                            &&admit(10,op->tx.nativeStart(owner,op->tx.attempt,false))
                            &&admit(11,commandPeers(*op,WorldPackReloadCommand::BeginNative))){
                            previousModel=std::move(op->completionModel);op->completionModel=tick.model;
                            op->applicationScene=tick.scene;
                            seed={op->tx.token,op->tx.attempt,0,op->tx.owner,false};scoped=true;
                        }else op->tx.seal();
                    }else op->tx.seal();
                }else op->tx.seal();
            }catch(...){callbackReason=12;std::lock_guard lock(op->mutex);op->tx.seal();}
            settingsApplicationDiagnostic(op,SettingsApplicationStage::CallbackPrepared,scoped,callbackReason);
            if(!scoped)return;
            NativeSettingsPackScope settings(scoped?context:NativeSettingsPackContext{});WorldNativeScope native(seed);
            auto permit=worldApplicationTickInvocation.permit;
            try{body->original(model);}catch(...){settingsApplicationDiagnostic(op,SettingsApplicationStage::CallbackThrow,scoped);std::lock_guard lock(op->mutex);
                op->executed.callbackReturned(permit,false);op->tx.startResult(op->tx.attempt,false);op->tx.seal();throw;}
            settingsApplicationDiagnostic(op,SettingsApplicationStage::CallbackReturn,scoped);
            if(scoped){
                try{auto actual=keys(ids(model.getResourcePackManager().mGlobalStack.get()));std::lock_guard lock(op->mutex);
                    if(op->tx.phase==WorldSwitchPhase::Reloading){
                        if(!op->executed.callbackReturned(permit,true)){op->tx.seal();return;}
                        op->tx.startResult(op->tx.attempt,true);
                        op->tx.changed=actual!=op->tx.oldGlobal;op->tx.setterRejected=actual!=op->tx.targetGlobal;
                    }
                }catch(...){std::lock_guard lock(op->mutex);op->tx.seal();}
            }
        };
    }catch(...){
        {std::lock_guard lock(op->mutex);op->tx.seal();}
        return;
    }
    bool admitted=false;
    {std::lock_guard lock(op->mutex);
        if(op->nativeNavigation||op->tx.phase!=WorldSwitchPhase::AwaitApply)op->tx.seal();
        else {op->nativeNavigation=true;if(op->tx.awaitingSelection)op->tx.queueCallback(titlePackNow());
            body->once.token=op->tx.token;body->once.world=op->tx.owner.world;admitted=true;}
    }
    if(!admitted){settingsApplicationDiagnostic(op,SettingsApplicationStage::NavigationDenied,false);return;}
    body->original=std::move(apply);
    body.reset();try{origin(std::move(wrapped));}catch(...){settingsApplicationDiagnostic(op,SettingsApplicationStage::NavigationThrow,true);throw;}
    settingsApplicationDiagnostic(op,SettingsApplicationStage::NavigationReturn,true);
    }
};
LL_TYPE_INSTANCE_HOOK(WorldSettingsComposeHook,ll::memory::HookPriority::Normal,ResourcePackManager,
    &ResourcePackManager::composeFullStack,int,ResourcePackStack& output,ResourcePackStack const& global,ResourcePackStack const& level){
    NativeHookLifetime::Lease lease(lifetime);if(!lease.admitted())return origin(output,global,level);
    auto context=nativeSettingsPackContext;auto op=active.load();
    if(!context.valid()&&op&&op->nativeSettings&&worldNativeContext.seed()
        &&worldNativeContext.token==op->tx.token&&worldNativeContext.owner.manager==address(this)
        &&worldNativeContext.owner.world==worldGeneration.load())context=op->settingsContext;
    NativeSettingsPackScope scope(context);
    return withNativeSettingsGlobal(mUseGlobalPackStack,address(this),worldGeneration.load(),[&]{return origin(output,global,level);});
}
