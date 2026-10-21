// Private implementation included after the native Settings model/owner helpers.
// All SDK calls below run on the verified MinecraftGame update owner. Workers
// carry immutable context through formal BackgroundTask factories instead.
bool engineOwnerCurrent(Operation const& op,MinecraftGame& game) {
    if(GetCurrentThreadId()!=ownerThread.load()||GetCurrentThreadId()!=op.tx.owner.thread
        ||observedGame.load()!=&game||address(&game)!=op.tx.owner.game||!runtimeReady.load()
        ||worldGeneration.load()!=op.tx.owner.world||!game.isInGame()||!op.client)return false;
    auto clients=game.getClientInstanceMap();
    return clients.size()==1&&clients.begin()->second==op.client&&op.client->getLocalPlayer()
        &&(op.tx.started||address(op.client->getLevelRenderer())==op.tx.owner.renderer)
        &&address(&op.client->getResourcePackManager())==op.tx.owner.manager;
}
std::vector<std::string> expectedEngineFull(Operation const& op,ResourcePackManager& manager) {
    if(op.request.reload){if(!op.expectedFullKnown)throw std::runtime_error("active Full snapshot unavailable");return op.expectedFull;}
    if(!manager.mGlobalStack.get()||!manager.mLevelStack.get())throw std::runtime_error("native stack unavailable");
    auto& repository=op.client->getResourcePackRepository();
    auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{Bedrock::NonOwnerPointer<IResourcePackRepository const>(repository)};
    ResourcePackStack scratch(std::vector<PackInstanceId>{},repo,false);
    auto context=op.settingsContext;context.allowPreGame=false;
    NativeSettingsPackScope scope(context);
    manager.composeFullStack(scratch,*manager.mGlobalStack.get(),*manager.mLevelStack.get());
    return keys(ids(&scratch));
}
bool engineFullMatches(Operation const& op,ResourcePackManager& manager,std::vector<std::string> const& expected) {
    if(!otherStacks(op,manager)||keys(ids(manager.mGlobalStack.get()))!=op.tx.targetGlobal)return false;
    std::shared_lock lock(manager.mFullStackAccess.get());
    return keys(ids(manager.mFullStack.get()))==expected;
}
void runWorldSelection(std::shared_ptr<Operation> const& op,MinecraftScreenModel& model,std::function<void()> setter) noexcept {
    TitleCallbackLease lease(lifetime,callbacks);WorldNativeContext seed;unsigned reason{};
    try{
        if(!lease.admitted()||active.load()!=op)return;
        auto owner=settingsOwner(model);auto& tick=worldApplicationTickInvocation;
        auto& manager=model.getResourcePackManager();
        {
            std::lock_guard lock(op->mutex);
            if(!op->nativeSettings){
                auto admit=nativeSettingsApplyAdmission(op->selectionOnce,op->tx.token,worldGeneration.load(),active.load()==op);
                if(admit==NativeSettingsApplyAdmission::Stale)return;
                if(admit==NativeSettingsApplyAdmission::Duplicate){op->engine.seal();op->tx.seal();return;}
                if(titlePackControl().cancelled(op->tx.token)||titlePackControl().stopped()){op->tx.cancel();return;}
            }
            auto pass=[&](unsigned code,bool value){if(!value&&!reason)reason=code;return value;};
            bool nativePrepared=op->nativeSettings
                ?(op->r26PeersNativeArmed && op->group.nativeActive(op->tx))
                :ready(*op,WorldPackReloadPhase::Prepared,op->tx.participant.oldEpoch);
            if(!pass(1,owner.present&&!owner.pending)||!pass(2,tick.operation==op&&tick.model&&tick.client==op->client&&tick.scene
                &&address(static_cast<MinecraftScreenModel*>(tick.model.get()))==address(&model))
                ||!pass(3,keys(ids(manager.mGlobalStack.get()))==op->tx.oldGlobal&&otherStacks(*op,manager))
                ||!pass(4,nativePrepared)
                ||!pass(5,op->tx.callback(owner,titlePackNow()))){op->tx.seal();return;}
            if(!op->nativeSettings&&!titlePackControl().claimSetter(op->tx.token,op->tx.attempt)){
                // The cancellation mutex won before any native mutation.
                op->tx.phase=WorldSwitchPhase::Cancelled;return;
            }
            WorldEngineReloadReceipt::Identity id{op->tx.token,op->tx.attempt,owner.world,owner.thread,owner.game,owner.client,address(&model)};
            if(!op->engine.prepare(id)||!op->engine.selectionEntered(id)){op->tx.seal();return;}
            op->completionModel=tick.model;op->applicationScene=tick.scene;
            seed={op->tx.token,op->tx.attempt,0,op->tx.owner,false};
        }
        // Cancellation has crossed its final mutex claim; Root already owns
        // the exact replay and original native callback.
        WorldNativeScope native(seed);setter();
        auto actual=keys(ids(manager.mGlobalStack.get()));
        {
            std::lock_guard lock(op->mutex);
            op->tx.changed=actual!=op->tx.oldGlobal;op->tx.setterRejected=actual!=op->tx.targetGlobal;
            if(!op->engine.selectionReturned(op->engine.identity(),true,actual==op->tx.targetGlobal)||!otherStacks(*op,manager))op->tx.seal();
            else op->selectionCommitted=true;
        }
        packDiagnostic("RESOURCEPACK_WORLD event=selection_return token=%llu attempt=%llu normal=1 committed=%u native_started=%u",
            diagNumber(seed.token),diagNumber(seed.attempt),unsigned(op->selectionCommitted),unsigned(op->tx.started));
    }catch(...){std::lock_guard lock(op->mutex);op->engine.seal();op->tx.seal();
        packDiagnostic("RESOURCEPACK_WORLD event=selection_failure token=%llu reason=%u retained=1",diagNumber(op->tx.token),reason);}
}
// Passive, bounded owner-thread vtable inventory. This does NOT invoke any
// resource-pack listener, reinterpret its verdict, change a native flag, or
// alter the setStack/full composition policy. No runtime absolute addresses
// or user pack identities are retained or logged.
void auditResourceLoadGate(MinecraftGame& game) noexcept {
    try{
        auto manager=game.getResourceLoadManager();
        auto byte=*reinterpret_cast<unsigned char const*>(&manager->mUnk8953d0);
        packDiagnostic("RESOURCEPACK_WORLD event=resource_load_gate state_byte=%u required=1 leave_done=%u in_game=%u",
            unsigned(byte),unsigned(game.isLeaveGameDone()),unsigned(game.isInGame()));
    }catch(...){packDiagnostic("RESOURCEPACK_WORLD event=resource_load_gate unreadable=1");}
}
void auditNativePackListeners(MinecraftGame& game,ResourcePackManager& manager) noexcept {
    try {
        auto& gameManager=game.getResourcePackManager();
        auto const gameModule=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        constexpr std::uintptr_t GameImageBudget=311249032;
        packDiagnostic("RESOURCEPACK_WORLD event=listener_owner same_game_manager=%u client_count=%zu game_count=%zu client_global=%u game_global=%u client_pending=%u game_pending=%u client_initializing=%u",
            unsigned(&gameManager==&manager),manager.mListeners->size(),gameManager.mListeners->size(),
            unsigned(bool(manager.mUseGlobalPackStack)),unsigned(bool(gameManager.mUseGlobalPackStack)),
            unsigned(bool(manager.mPendingRestack)),unsigned(bool(gameManager.mPendingRestack)),unsigned(bool(manager.mInitializing)));
        unsigned ordinal=0;
        for(auto const& item:*manager.mListeners){
            if(ordinal==24)break;
            ResourcePackListener* listener=item.get();
            auto table=*reinterpret_cast<void***>(listener);
            std::uintptr_t method=reinterpret_cast<std::uintptr_t>(table[2]);
            std::uintptr_t vfptr=reinterpret_cast<std::uintptr_t>(table);
            bool gameMethod=gameModule&&method>=gameModule&&method-gameModule<GameImageBudget;
            bool gameVtable=gameModule&&vfptr>=gameModule&&vfptr-gameModule<GameImageBudget;
            packDiagnostic("RESOURCEPACK_WORLD event=listener_entry index=%u code_module_game=%u code_rva=%llu vtable_module_game=%u vtable_rva=%llu",
                ordinal++,unsigned(gameMethod),diagNumber(gameMethod?method-gameModule:0),
                unsigned(gameVtable),diagNumber(gameVtable?vfptr-gameModule:0));
        }
        if(manager.mListeners->size()>ordinal)
            packDiagnostic("RESOURCEPACK_WORLD event=listener_entry_truncated observed=%u total=%zu",
                ordinal,manager.mListeners->size());
    }catch(...){packDiagnostic("RESOURCEPACK_WORLD event=listener_inventory_failure");}
}
void sealWorldEngine(Operation& op,unsigned reason) noexcept {
    packDiagnostic("RESOURCEPACK_WORLD event=engine_abort token=%llu attempt=%llu reason=%u pending=%u started=%u requested=%u",
        diagNumber(op.tx.token),diagNumber(op.tx.attempt),reason,
        unsigned(op.tx.owner.pending),unsigned(op.tx.started),unsigned(op.explicitEngineRequested));
    op.engine.seal();op.tx.seal();
}
// Called on the verified Minecraft update owner immediately BEFORE the
// native engine changes any texture resources. LHolo's IconPackComposeHook
// opens a NativeMutation lease at _composeFullStack entry and deliberately
// invalidates if still Prepared. R24/R25 incorrectly issued BeginNative only
// inside _onActiveResourcePacksChanged, AFTER the compose mutation started.
bool armR26NativePeers(Operation& op,char const* route) noexcept {
    std::lock_guard lock(op.mutex);
    if(op.r26PeersNativeArmed)return true;
    bool prepared=ready(op,WorldPackReloadPhase::Prepared,op.tx.participant.oldEpoch);
    bool began=prepared&&commandPeers(op,WorldPackReloadCommand::BeginNative);
    if(began)op.r26PeersNativeArmed=true;
    packDiagnostic("RESOURCEPACK_R26 event=prearm route=%s token=%llu prepared=%u begin=%u epoch=%llu",
        route,diagNumber(op.tx.token),unsigned(prepared),unsigned(began),
        diagNumber(op.tx.participant.oldEpoch));
    if(!began){
        for(unsigned i=0;i<op.group.count;++i){
            auto const& peer=op.group.observations[i];
            packDiagnostic("RESOURCEPACK_R26 event=prearm_peer_fail peer=%u seen=%u accepted=%u phase=%u error=%u readers=%u jobs=%u borrowers=%u epoch=%llu",
                i,unsigned(peer.observed),unsigned(peer.accepted),
                unsigned(peer.phase),peer.error,peer.readers,peer.jobs,
                peer.borrowers,diagNumber(peer.epoch));
        }
    }
    return began;
}
void serviceWorldEngineReload(MinecraftGame& game) noexcept {
    auto op=active.load();if(!op||!op->apiReload||!installed.load()||callbacks.load()||GetCurrentThreadId()!=ownerThread.load())return;
    unsigned stage=1;
    try{
        WorldNativeContext seed;std::shared_ptr<MainMenuScreenModel> model;bool pending{},started{};
        {
            std::lock_guard lock(op->mutex);
            if(op->tx.phase!=WorldSwitchPhase::Reloading||!op->selectionCommitted)return;
            if(!op->engineServiceLogged){
                op->engineServiceLogged=true;
                packDiagnostic("RESOURCEPACK_WORLD event=engine_service token=%llu attempt=%llu committed=%u pending=%u started=%u model=%u",
                    diagNumber(op->tx.token),diagNumber(op->tx.attempt),unsigned(op->selectionCommitted),
                    unsigned(op->client->getResourcePackManager().mPendingRestack),unsigned(op->tx.started),
                    unsigned(bool(op->completionModel)));
            }
            if(!engineOwnerCurrent(*op,game)||titlePackNow()>op->tx.deadline){sealWorldEngine(*op,1);return;}
            auto& manager=op->client->getResourcePackManager();
            if(!otherStacks(*op,manager)||keys(ids(manager.mGlobalStack.get()))!=op->tx.targetGlobal){sealWorldEngine(*op,2);return;}
            seed={op->tx.token,op->tx.attempt,0,op->tx.owner,false};model=op->completionModel;
            pending=manager.mPendingRestack;started=op->tx.started;
        }
        auto& manager=op->client->getResourcePackManager();
        bool inventoryNow=false;
        {std::lock_guard lock(op->mutex);
            if(!op->listenerSnapshotLogged){op->listenerSnapshotLogged=true;inventoryNow=true;}
        }
        if(inventoryNow){auditNativePackListeners(game,manager);auditResourceLoadGate(game);}
        stage=3;
        if(!model||nativeSettingsClient(*model,game,op->tx.owner.client)!=op->client)
            throw std::runtime_error("completion model owner changed");
        if(nativeTasks.load())return;
        stage=4;auto expected=expectedEngineFull(*op,manager);
        {
            std::lock_guard lock(op->mutex);
            if(op->expectedFullKnown&&op->expectedFull!=expected){sealWorldEngine(*op,5);return;}
            op->expectedFull=std::move(expected);op->expectedFullKnown=true;
        }
        if(pending&&!started){
            // Native listeners may veto. Never clear pending or bypass them.
            auto context=op->settingsContext;context.allowPreGame=false;
            WorldNativeScope native(seed);NativeSettingsPackScope settings(context);
            auto currentTime=titlePackNow();
            {
                std::lock_guard lock(op->mutex);
                if(currentTime<op->nextComposeServiceAt)return;
                // A persistent native veto once caused 3,257 synchronous
                // composition attempts in 20 seconds. Poll at most 10 Hz.
                op->nextComposeServiceAt=currentTime+100;
            }
            // Arm both peers before entering LHolo's resource-composition
            // hook. This enforces Prepared-with-zero-borrowers FIRST and
            // NativeActive during the subsequent owned mutation.
            if(!armR26NativePeers(*op,"compose")){
                std::lock_guard lock(op->mutex);
                sealWorldEngine(*op,12);
                return;
            }
            auto votesBefore=packInvalidVotes.load();
            auto rejectsBefore=packInvalidRejects.load();
            manager._composeFullStack();
            auto votes=packInvalidVotes.load()-votesBefore;
            auto rejects=packInvalidRejects.load()-rejectsBefore;
            std::lock_guard lock(op->mutex);++op->composeAttempts;
            unsigned state=manager.mPendingRestack?1:2;
            auto now=titlePackNow();
            if(op->engineDiagnosticState!=state||now>=op->nextRestackDiagnostic){
                op->engineDiagnosticState=state;op->nextRestackDiagnostic=now+5000;
                packDiagnostic("RESOURCEPACK_WORLD event=engine_restack token=%llu pending=%u attempts=%u native_started=%u game_listener_votes=%llu game_listener_rejects=%llu",
                    diagNumber(seed.token),unsigned(bool(manager.mPendingRestack)),op->composeAttempts,unsigned(op->tx.started),
                    diagNumber(votes),diagNumber(rejects));}
            return;
        }
        bool fullMatches=engineFullMatches(*op,manager,op->expectedFull);
        if(!pending&&!fullMatches){std::lock_guard lock(op->mutex);sealWorldEngine(*op,6);return;}
        if(!started){
            // Public Minecraft entry may schedule the native Active hook
            // asynchronously. Do not seal merely because it has not run by
            // the time this call returns; deadline/owner/provenance still
            // fail closed. Never issue a second public reload request.
            if(pending||!fullMatches||!game.isReadyToRender()||game.isShowingLoadingScreen()||!model->isResourceLoadingFinished())return;
            {std::lock_guard lock(op->mutex);
                if(worldEngineDispatchAction(op->explicitEngineRequested,op->tx.started)==WorldEngineDispatchAction::AwaitNativeEntry)
                    return;
                if(op->explicitEngineRequested)return;
                op->explicitEngineRequested=true;
            }
            packDiagnostic("RESOURCEPACK_WORLD event=engine_dispatch token=%llu attempt=%llu stage=entry",
                diagNumber(seed.token),diagNumber(seed.attempt));
            // Reload of already-active packs has no pending stack change.
            // It still must arm the LHolo/Praxis native owners before
            // Minecraft begins loading textures.
            if(!armR26NativePeers(*op,"explicit")){
                std::lock_guard lock(op->mutex);
                sealWorldEngine(*op,13);
                return;
            }
            stage=7;WorldNativeScope native(seed);game.onActiveResourcePacksChanged(manager);
            {std::lock_guard lock(op->mutex);
                packDiagnostic("RESOURCEPACK_WORLD event=engine_dispatch token=%llu attempt=%llu stage=returned started=%u",
                    diagNumber(seed.token),diagNumber(seed.attempt),unsigned(op->tx.started));
            }
            return;
        }
        auto serial=nativeReloadSerial.load();bool loading=model->isResourceLoadingFinished();bool ui{};
        {
            std::lock_guard lock(op->mutex);
            if(op->engine.serial()!=serial){sealWorldEngine(*op,8);return;}
            ui=op->engine.needsUI(serial,pending,fullMatches,loading);
            if(ui&&!op->engine.requestUI(serial)){sealWorldEngine(*op,9);return;}
        }
        if(ui){
            game.handleReloadUIDefinitions();
            std::lock_guard lock(op->mutex);
            if(nativeReloadSerial.load()!=serial||!op->engine.uiReturned(serial,true)){sealWorldEngine(*op,10);return;}
            packDiagnostic("RESOURCEPACK_WORLD event=engine_ui_return token=%llu attempt=%llu cycle=%llu normal=1",
                diagNumber(seed.token),diagNumber(seed.attempt),diagNumber(serial));
            return; // post-UI observation must be on a later owner update.
        }
        bool shouldProbeSource=false;
        {
            std::lock_guard lock(op->mutex);
            bool wasReady=op->engine.applicationReady();
            if(op->engine.loadingObserved(serial,manager.mPendingRestack,fullMatches,loading)&&!wasReady)
                packDiagnostic("RESOURCEPACK_WORLD event=native_application token=%llu attempt=%llu cycle=%llu finish=1 ui=1 loading=1 full_match=1",
                    diagNumber(seed.token),diagNumber(seed.attempt),diagNumber(serial));
            if(op->r24AuthorizedClone&&op->engine.applicationReady()
                &&!op->r24SourceResolutionLogged){
                op->r24SourceResolutionLogged=true;
                shouldProbeSource=true;
            }
        }
        if(shouldProbeSource){
            // Inspect the resolved resource *after* the actual native reload,
            // never the staged atlas replacement from r22/r23.
            int sourceIndex=-2;
            bool candidateInFull=false;
            std::size_t fullCount{};
            try {
                auto uv=BlockGraphics::getTextureUVCoordinateSet("powder_snow",0,0);
                std::array<std::string,1> exts{"png"};
                sourceIndex=manager.getPackStackIndexOfResource(
                    uv.sourceFileLocation.get(),
                    gsl::span<std::string const>(exts.data(),exts.size())).first;
                std::vector<std::string> full;
                {
                    std::shared_lock lock(manager.mFullStackAccess.get());
                    full=keys(ids(manager.mFullStack.get()));
                }
                fullCount=full.size();
                if(!op->tx.targetGlobal.empty()) {
                    auto const& added=op->tx.targetGlobal.back();
                    candidateInFull=std::find(full.begin(),full.end(),added)!=full.end();
                }
            }catch(...){sourceIndex=-3;}
            packDiagnostic("RESOURCEPACK_R24 event=post_reload_resolution token=%llu full_count=%zu candidate_in_full=%u powder_snow_pack_index=%d",
                diagNumber(seed.token),fullCount,unsigned(candidateInFull),sourceIndex);
        }
    }catch(...){std::lock_guard lock(op->mutex);sealWorldEngine(*op,100+stage);
        packDiagnostic("RESOURCEPACK_WORLD event=engine_failure token=%llu stage=%u retained=1",
            diagNumber(op->tx.token),stage);}
}
