// Private implementation included inside WorldPackSwitch's owner namespace.
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
            ||!bootstrap.flag(4,completionHooksReady.load())
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
            clone,client->isInMultiplayerGame(),level.size()==2,
            level.size(),bool(manager.mUseGlobalPackStack),
            bool(manager.mInitializing),bool(manager.mPendingRestack));
        bool include=nativeSettingsMayIncludeGlobal(
            manager.mUseGlobalPackStack,client->isInMultiplayerGame(),
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
    auto owned=repository.getResourcePackSatisfiesPackId(changed.mPackId.get(),true);
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
            ||general->_areAnyPackImportsPending()
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
        general->mResourceContentSource->getSortedSelectedContent(selected,game->getEntitlementManager());
        return keys(selected)==op.pendingSelection;
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
        &&!static_cast<SettingsScreenController*>(liveRoot.get())->mGeneralSettingsScreenController->_areAnyPackImportsPending();}
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
        settingsReplayPermit.used=true;return origin(event);
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
    try{view.canExit=currentRoot.canExit();view.importsIdle=general&&!general->_areAnyPackImportsPending();}
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
            general->mResourceContentSource->getSortedSelectedContent(selected,observedGame.load()->getEntitlementManager());
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
LL_TYPE_INSTANCE_HOOK(WorldSettingsRootInitHook,ll::memory::HookPriority::Normal,SettingsScreenController,
    &SettingsScreenController::_init,void){
    NativeHookLifetime::Lease lease(lifetime);WorldSettingsRootInitInvocation initialization;
    static std::atomic_uint32_t r29RootSeen{};
    if(lease.admitted() && GetCurrentThreadId()==ownerThread.load()
        && observedGame.load() && observedGame.load()->isInGame()
        && r29RootSeen.fetch_add(1)<4)
        packDiagnostic("RESOURCEPACK_R29 event=vanilla_settings_root_seen settings=%u legacy=%u global=%u reload=%u",
            unsigned(mSettingsMode==SettingsScreenMode::Settings),
            unsigned(mLegacySyncOnly),unsigned(mIsGlobalSettingsScreen),
            unsigned(mReloadTexturePacksOnExit));
    if(lease.admitted()&&GetCurrentThreadId()==ownerThread.load())try{
        std::shared_ptr<MainMenuScreenModel> model=mMainMenuScreenModel;
        if(model&&mSettingsMode==SettingsScreenMode::Settings&&!mLegacySyncOnly){
            std::shared_ptr<IClientInstance> client;auto owner=settingsOwner(*model,&client);
            if(owner.usable())initialization={this,std::move(model),std::move(client),owner,{},true};
        }
    }catch(...){}
    WorldSettingsRootInitScope scope(std::move(initialization));
    // Preserve every native _init predicate and its navigation/global result.
    // Child construction remains scoped to this invocation; no manual _init.
    NativeSettingsPackScope neutral({});
    try{origin();}catch(...){auto op=worldSettingsRootInitInvocation.operation;
        if(op){std::lock_guard lock(op->mutex);op->tx.cancel();}throw;}
    auto const& current=worldSettingsRootInitInvocation;auto op=current.operation;
    if(!op)return;
    NativeSettingsRootReloadView view;bool original=mReloadTexturePacksOnExit;
    try{
        std::shared_ptr<GeneralSettingsScreenController> general=mGeneralSettingsScreenController;
        std::shared_ptr<MainMenuScreenModel> model=mMainMenuScreenModel;
        std::shared_ptr<IClientInstance> client;auto owner=model?settingsOwner(*model,&client):WorldPackOwner{};
        view.initializationOwned=current.eligible&&current.root==this&&active.load()==op;
        view.sameModel=model==current.model;view.sameGeneral=general&&address(general.get())==op->settingsContext.controller;
        view.ownerCurrent=owner.usable()&&current.owner.same(owner)&&op->editorOwner.same(owner)&&client==current.client;
        view.settingsMode=mSettingsMode==SettingsScreenMode::Settings&&!mLegacySyncOnly;
        if(view.ownerCurrent){auto& manager=model->getResourcePackManager();
            view.globalPolicy=nativeSettingsMayIncludeGlobal(
                manager.mUseGlobalPackStack,client->isInMultiplayerGame(),
                ids(manager.mLevelStack.get()).empty(),op->r24AuthorizedClone);}
        view.childGlobal=general&&general->mIsGlobalSettings;view.childReload=general&&general->mReloadTexturePacksOnExit;
        view.sourcePresent=general&&general->mResourceContentSource;
        bool qualified=nativeSettingsRootReload(false,view);
        if(qualified){mReloadTexturePacksOnExit=nativeSettingsRootReload(original,view);
            std::lock_guard lock(op->mutex);op->settingsInitRoot=address(this);}
        else {std::lock_guard lock(op->mutex);op->tx.cancel();}
        packDiagnostic("RESOURCEPACK_WORLD event=settings_root_init session=%llu qualified=%u original_reload=%u root_reload=%u root_global=%u navigate_menu=%u child_global=%u child_reload=%u same_model=%u same_general=%u owner=%u policy=%u source=%u",
            diagNumber(op->settingsSession),unsigned(qualified),unsigned(original),unsigned(mReloadTexturePacksOnExit),unsigned(mIsGlobalSettingsScreen),unsigned(mNavigateToMenuOnExit),
            unsigned(view.childGlobal),unsigned(view.childReload),unsigned(view.sameModel),unsigned(view.sameGeneral),unsigned(view.ownerCurrent),unsigned(view.globalPolicy),unsigned(view.sourcePresent));
    }catch(...){std::lock_guard lock(op->mutex);op->tx.cancel();
        packDiagnostic("RESOURCEPACK_WORLD event=settings_root_init session=%llu qualified=0 exception=1",diagNumber(op->settingsSession));}
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsCreatedHook,ll::memory::HookPriority::Normal,GeneralSettingsScreenController,
    &GeneralSettingsScreenController::$ctor,void*,std::shared_ptr<MainMenuScreenModel> model,
    bool global,bool reloadOnExit,std::string const& initialPack,bool legacySync){
    NativeHookLifetime::Lease lease(lifetime);
    unsigned reason=8;bool scoped=lease.admitted()&&model&&beginSettings(this,*model,reason);
    auto flags=nativeSettingsConstructorFlags(global,reloadOnExit,scoped);
    auto op=scoped?settingsOperation(address(this)):nullptr;
    NativeSettingsPackScope construction(op?op->settingsContext:NativeSettingsPackContext{});
    try{auto* result=origin(std::move(model),flags.global,flags.reloadOnExit,initialPack,legacySync);
        packDiagnostic("RESOURCEPACK_WORLD event=settings_create session=%llu admitted=%u reason=%u legacy=%u global=%u reload=%u source=%u root_scope=%u",
            diagNumber(op?op->settingsSession:0),unsigned(scoped),reason,unsigned(legacySync),unsigned(mIsGlobalSettings),unsigned(mReloadTexturePacksOnExit),unsigned(bool(mResourceContentSource)),unsigned(worldSettingsRootInitInvocation.eligible));
        return result;}
    catch(...){if(scoped){auto op=settingsOperation(address(this));if(op){std::lock_guard lock(op->mutex);op->tx.cancel();}}throw;}
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsRootOpenedHook,ll::memory::HookPriority::Normal,SettingsScreenController,
    &SettingsScreenController::$onOpen,void){
    NativeHookLifetime::Lease lease(lifetime);origin();if(!lease.admitted()||GetCurrentThreadId()!=ownerThread.load())return;
    auto general=mGeneralSettingsScreenController.get();auto op=general?settingsOperation(address(general)):nullptr;
    if(!op||op->settingsInitRoot!=address(this))return;
    auto weak=static_cast<MinecraftScreenController*>(this)->weak_from_this();
    {std::lock_guard lock(op->mutex);op->settingsRoot=weak;op->settingsSceneId=getSceneId();}
    packDiagnostic("RESOURCEPACK_WORLD event=settings_editor session=%llu bound=%u global_tab=%u fullscreen=%u root_global=%u root_reload=%u",
        diagNumber(op->settingsSession),unsigned(!weak.expired()),unsigned(mCurrentTab==SettingsTabIndex::GlobalResourcesTab),unsigned(mFullscreen),unsigned(mIsGlobalSettingsScreen),unsigned(mReloadTexturePacksOnExit));
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsCanExitHook,ll::memory::HookPriority::Normal,GeneralSettingsScreenController,
    &GeneralSettingsScreenController::$canExit,bool){
    NativeHookLifetime::Lease lease(lifetime);bool result=origin();if(!lease.admitted()||!result)return result;
    auto op=settingsOperation(address(this));if(!op)return result;
    std::lock_guard lock(op->mutex);
    return op->tx.phase!=WorldSwitchPhase::Pausing&&op->tx.phase!=WorldSwitchPhase::Preparing;
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
LL_TYPE_INSTANCE_HOOK(WorldSettingsStackHook,ll::memory::HookPriority::Normal,GeneralSettingsScreenController,
    &GeneralSettingsScreenController::_setResourcePackStack,void){
    NativeHookLifetime::Lease lease(lifetime);auto op=lease.admitted()?settingsOperation(address(this)):nullptr;
    auto model=mMainMenuScreenModel.get();
    bool scoped=false;try{scoped=model&&mIsGlobalSettings&&mReloadTexturePacksOnExit&&mResourceContentSource&&settingsPrepared(op,*model);}catch(...){scoped=false;}
    settingsApplicationDiagnostic(op,SettingsApplicationStage::StackEnter,scoped);
    // R29's vanilla global setter runs BEFORE its apply-screen callback.
    // That mutation can call LHolo's IconPackComposeHook synchronously.
    // Therefore begin both resource owners here, while they still hold
    // Prepared receipts, instead of waiting for WorldUpdate/engine_service.
    // Otherwise LHolo marks Prepared -> Invalidated(error=10) and textures
    // never reach the GPU despite the vanilla editor showing the pack ON.
    bool exactVanilla=op&&op->nativeSettings&&op->r24AuthorizedClone;
    if(exactVanilla){
        bool armed=scoped&&armR26NativePeers(*op,"vanilla_settings_before_stack");
        packDiagnostic("RESOURCEPACK_R30 event=vanilla_prearm token=%llu scoped=%u armed=%u before_setter=1",
            diagNumber(op->tx.token),unsigned(scoped),unsigned(armed));
        if(!armed){
            // Never allow an unarmed vanilla setter to mutate an active world:
            // no forced zero borrower, no fake success or bypass of LHolo.
            std::lock_guard lock(op->mutex);
            op->tx.seal();
            return;
        }
    }
    // A denied setter must not borrow the UI editor's permission. Only the
    // existing owner/Prepared receipt below may admit the normal native apply.
    NativeSettingsEditorPause editing;
    NativeSettingsPackScope scope(scoped?op->settingsContext:NativeSettingsPackContext{});
    try{origin();} // Native source save, selected ordering, entitlement and exit.
    catch(...){settingsApplicationDiagnostic(op,SettingsApplicationStage::StackThrow,scoped);if(op){std::lock_guard lock(op->mutex);op->tx.seal();}throw;}
    settingsApplicationDiagnostic(op,SettingsApplicationStage::StackReturn,scoped);
    // A synchronous native setter may run without a navigate callback. Its
    // return is not proof that no native work started; quarantine the paused
    // resources instead of reopening the old epoch.
    if(op){std::lock_guard lock(op->mutex);if(!op->nativeNavigation&&op->tx.phase==WorldSwitchPhase::AwaitApply)op->tx.seal();}
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsClientPreGameHook,ll::memory::HookPriority::Normal,ClientInstance,
    &ClientInstance::$isPreGame,bool){
    bool result=origin();NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return result;
    auto* client=static_cast<IClientInstance*>(this);
    return nativeSettingsPreGame(result,address(client),worldGeneration.load())||settingsEditorPreGame(result,client);
}
LL_TYPE_INSTANCE_HOOK(WorldSettingsGlobalHook,ll::memory::HookPriority::Normal,GeneralSettingsScreenController,
    &GeneralSettingsScreenController::_setGlobalStack,void,std::vector<PackInstanceId> const& identities,bool anyVersion,bool saveStack){
    NativeHookLifetime::Lease lease(lifetime);auto op=lease.admitted()?settingsOperation(address(this)):nullptr;
    bool scoped=op&&nativeSettingsPackContext.controller==address(this);
    settingsApplicationDiagnostic(op,SettingsApplicationStage::GlobalEnter,scoped,unsigned(anyVersion)|(unsigned(saveStack)<<1),identities.size());
    if(op&&nativeSettingsPackContext.controller==address(this)){
        bool denied=false;
        {
            std::lock_guard lock(op->mutex);
            if(op->r24AuthorizedClone&&!op->r26PeersNativeArmed
                &&op->tx.phase==WorldSwitchPhase::AwaitApply){
                denied=true;
                op->tx.seal();
            }
        }
        if(denied){
            packDiagnostic("RESOURCEPACK_R30 event=vanilla_unarmed_global_denied token=%llu",
                diagNumber(op->tx.token));
            return;
        }
        try{std::lock_guard lock(op->mutex);if(op->tx.phase==WorldSwitchPhase::AwaitApply)op->tx.targetGlobal=keys(identities);}
        catch(...){std::lock_guard lock(op->mutex);op->tx.cancel();}
    }
    try{origin(identities,anyVersion,saveStack);}catch(...){settingsApplicationDiagnostic(op,SettingsApplicationStage::GlobalThrow,scoped);throw;}
    settingsApplicationDiagnostic(op,SettingsApplicationStage::GlobalReturn,scoped,0,identities.size());
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
LL_TYPE_INSTANCE_HOOK(WorldApplyingLoadingHook,ll::memory::HookPriority::Normal,MinecraftScreenModel,
    &MinecraftScreenModel::isResourceLoadingFinished,bool){
    NativeHookLifetime::Lease lease(lifetime);bool result=origin();
    auto invocation=worldApplicationTickInvocation;
    if(lease.admitted()&&invocation.permit&&applicationOwnerCurrent(invocation)){
        auto op=invocation.operation;std::lock_guard lock(op->mutex);
        if(op->tx.phase==WorldSwitchPhase::Reloading&&op->tx.token==invocation.permit.identity.token
            &&op->tx.attempt==invocation.permit.identity.attempt)
            op->executed.loadingQueried(invocation.permit,address(this),result);
    }
    return result;
}
LL_TYPE_INSTANCE_HOOK(WorldApplyingTickHook,ll::memory::HookPriority::Normal,ResourcePackApplyingScreenController,
    &ResourcePackApplyingScreenController::$tick,ui::DirtyFlag){
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
LL_TYPE_INSTANCE_HOOK(WorldSettingsApplyHook,ll::memory::HookPriority::Normal,MainMenuScreenModel,
    &MainMenuScreenModel::navigateToApplyResourcePacksScreen,void,std::function<void(MinecraftScreenModel&)> apply){
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
