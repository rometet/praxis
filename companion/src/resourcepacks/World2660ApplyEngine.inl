// Included only by WorldPack2660Native.cpp, in its private namespace.
// The 26.60 world path never imports 26.51 private screen, scene, or virtual
// thunk symbols. No synthetic game-state flags are written.
enum class World2660Step {
    Idle, Pausing, WaitingNative, Settling, Reacquiring,
    Failed, Finished
};
struct World2660Operation {
    World2660Step step{World2660Step::Idle};
    std::uint64_t token{}, owner{}, generation{}, deadline{}, settledTick{};
    std::uintptr_t game{}, client{}, renderer{};
    std::vector<PackInstanceId> originalIDs, desiredIDs;
    std::vector<AppliedPack> original, desired, level, base, treatment, full;
    std::vector<PackInstanceId> exactBeforeFull;
    std::array<WorldPackReloadParticipant,2> peers{};
    unsigned peerCount{},stable{};
    std::shared_ptr<ll::mod::NativeMod> lholo;
    WorldPackReloadRequest request;
    Title2660Receipt receipt;
    bool rollback{}, setterAttempted{}, fullReloadObserved{}, geometryRebuilt{};
    std::string status;
};
auto& worldOperation=*new World2660Operation;
std::atomic<DWORD> worldNativeThread{};
std::atomic<std::uint64_t> worldNonce{};
std::atomic_bool worldPermanentRecovery{};

std::vector<std::string> world2660Keys(std::vector<AppliedPack> const& order){
    std::vector<std::string> out;
    out.reserve(order.size());
    for(auto const& item:order){
        out.push_back(std::to_string(item.packType)+"/"+item.uuid+"/"+
                      item.version+"/"+item.subpack);
    }
    return out;
}
std::string world2660NormalPath(std::string const& p){
    auto in=std::filesystem::path(std::u8string_view(
        reinterpret_cast<char8_t const*>(p.data()),p.size())).lexically_normal();
    auto utf=in.wstring();
    std::transform(utf.begin(),utf.end(),utf.begin(),[](wchar_t c){return std::towlower(c);});
    auto a=std::filesystem::path(utf).u8string();
    return {reinterpret_cast<char const*>(a.data()),a.size()};
}
bool world2660LayerStillOwned(ResourcePackManager& manager,
                 std::vector<AppliedPack> const& level,
                 std::vector<AppliedPack> const& base,
                 std::vector<AppliedPack> const& treatment) {
    std::uint32_t reason{};
    auto actualLevel=snapshot(manager.mLevelStack.get(),reason,true);
    auto actualBase=snapshot(manager.mBaseGameStack.get(),reason,true);
    auto actualTreatment=snapshot(manager.mTreatmentStack.get(),reason,true);
    return actualLevel.known&&actualBase.known&&actualTreatment.known &&
        same(actualLevel.order,level)&&same(actualBase.order,base)&&
        same(actualTreatment.order,treatment);
}
WorldPackReloadReply world2660Call(unsigned peer,WorldPackReloadCommand command) {
    WorldPackReloadReply reply{};
    if(peer>=worldOperation.peerCount || !worldOperation.peers[peer] ||
       worldOperation.peers[peer](command,&worldOperation.request,&reply)!=1) {
        reply.error=1;
    }
    return reply;
}
bool world2660Receipt(WorldPackReloadReply const& p,WorldPackReloadPhase phase,
                      std::uint64_t epoch) {
    auto const& r=worldOperation.request;
    return !p.error&&p.bytes==sizeof(p)&&p.abi==WorldPackReloadAbi&&
        p.token==r.token&&p.world==r.world&&p.client==r.client&&
        p.renderer==r.renderer&&p.epoch==epoch&&p.phase==phase&&
        !p.cpuReaders&&!p.asyncJobs&&!p.nativeBorrowers &&
        p.ownedGpu!=OwnedGpuFence::Pending &&
        p.ownedGpu!=OwnedGpuFence::Unknown;
}
bool world2660All(WorldPackReloadCommand command,
                  WorldPackReloadPhase expected,std::uint64_t epoch) {
    for(unsigned i=0;i<worldOperation.peerCount;++i){
        auto reply=world2660Call(i,command);
        if(!world2660Receipt(reply,expected,epoch))return false;
    }
    return true;
}
void world2660ClosePeers(bool mutated) noexcept {
    for(unsigned i=0;i<worldOperation.peerCount;++i) {
        auto reply=world2660Call(i,mutated?
            WorldPackReloadCommand::Stop:WorldPackReloadCommand::CancelBeforeNative);
        if(!mutated&&!reply.error)world2660Call(i,WorldPackReloadCommand::ResumeCancelled);
    }
}
void world2660Finish(World2660Step step,char const* message,
                     bool cannotProve=false) noexcept {
    auto& op=worldOperation;
    op.status=message;
    if(cannotProve){
        worldPermanentRecovery.store(true,std::memory_order_release);
        world2660ClosePeers(op.setterAttempted);
    }
    op.step=step;
    char log[260]{};
    std::snprintf(log,sizeof(log),
        "RESOURCEPACK_WORLD2660 token=%llu state=%u mutated=%u restored=%u recovery=%u",
        static_cast<unsigned long long>(op.token),unsigned(step),
        unsigned(op.setterAttempted),unsigned(op.rollback),
        unsigned(cannotProve));
    logGuiInfo(log);
}
bool world2660ResolvePeers() {
    auto& op=worldOperation;
    op.peers={};
    op.peers[0]=[](WorldPackReloadCommand c,WorldPackReloadRequest const* r,
                   WorldPackReloadReply* p) noexcept -> int {
        return praxisWorldParticipant().command(c,r,p);
    };
    op.peerCount=1;
    op.lholo.reset();
    auto mod=ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
    if(!mod)return true;
    if(mod->getType()!=ll::mod::NativeModManagerName || !mod->isEnabled())
        return false;
    auto owned=std::static_pointer_cast<ll::mod::NativeMod>(mod);
    if(!owned||!owned->getHandle())return false;
    auto ptr=GetProcAddress(reinterpret_cast<HMODULE>(owned->getHandle()),
                            "lholo_resource_reload_v4");
    if(!ptr)return false; // When installed, LHolo must participate.
    op.peers[1]=reinterpret_cast<WorldPackReloadParticipant>(ptr);
    op.peerCount=2;
    op.lholo=std::move(owned);
    return true;
}
bool world2660Begin(World2660Frame const& frame,TitlePackRequest const& request,
                    AppliedStackView const& global,AppliedStackView const& level,
                    AppliedStackView const& base,AppliedStackView const& treatment,
                    AppliedStackView const& full,std::uint64_t now) {
    auto& op=worldOperation;
    if(op.step!=World2660Step::Idle && op.step!=World2660Step::Finished &&
       op.step!=World2660Step::Failed)return false;
    if(worldPermanentRecovery.load(std::memory_order_acquire) ||
       !frame.normal||!frame.renderReady||!frame.globalIncluded ||
       !global.known||!level.known||!base.known||!treatment.known||!full.known ||
       !frame.manager || frame.manager->mPendingRestack ||
       frame.manager->mInitializing || global.order.size()>512 ||
       !request.revision || !request.token)return false;
    // The GUI publishes a copied, timestamped native view. The engine
    // necessarily increments its update serial before consuming the
    // request; exact equality would reject every legitimate click.
    auto const serial=worldUpdateSerial.load(std::memory_order_acquire);
    if(request.revision>serial || serial-request.revision>500)return false;

    World2660Plan plan;
    auto& repo=frame.client->getResourcePackRepository();
    auto catalog=request.catalog;
    if(request.reload) {
        if(global.order.empty())return false;
        plan.result=World2660PlanResult::Ready;
        plan.target=global.order;
    }else {
        if(!catalog)return false;
        plan=planWorld2660Global(*catalog,request.selection,global);
        if(plan.result!=World2660PlanResult::Ready)return false;
        auto rootBytes=catalog->root.u8string();
        auto catalogueRoot=world2660NormalPath(
            {reinterpret_cast<char const*>(rootBytes.data()),rootBytes.size()});
        if(catalogueRoot!=world2660NormalPath(repo.getResourcePacksPath().value) &&
           catalogueRoot!=world2660NormalPath(repo.getDevelopmentResourcePacksPath().value))
            return false;
    }
    auto source=repo.getPacksByResourceLocation(PackOrigin::User);
    auto dev=repo.getPacksByResourceLocation(PackOrigin::Dev);
    source.insert(source.end(),dev.begin(),dev.end());
    if(source.size()>512)return false;
    std::vector<PackInstanceId> before;
    before.reserve(global.order.size());
    for(auto const& entry:*frame.manager->mGlobalStack.get()->mStack) {
        auto copy=identity(entry);
        if(!copy)return false;
        auto pack=entry.mPack.get();
        auto sub=subpack(*pack,entry.mSubpackIndex);
        if(!sub)return false;
        before.emplace_back(pack->mImpl->mPack->mManifest->mIdentity.get(),*sub);
    }
    std::vector<PackInstanceId> after;
    after.reserve(plan.target.size());
    for(auto const& value:plan.target) {
        bool found{};
        for(std::size_t i=0;i<global.order.size();++i) {
            if(world2660Same(value,global.order[i])) {
                after.emplace_back(before[i].mPackId.get(),before[i].mSubpackName.get());
                found=true;break;
            }
        }
        if(found)continue;
        if(!catalog)return false; // Reload-only may not add packs.
        auto const it=std::find_if(catalog->packs.begin(),catalog->packs.end(),
             [&](Pack const& pack){return pack.selectable&&pack.uuid==value.uuid&&
                                       pack.version==value.version;});
        if(it==catalog->packs.end()||value.packType!=6||!value.subpack.empty())return false;
        std::shared_ptr<ResourcePack> matched;
        for(auto* pack:source){
            if(!pack||!pack->mImpl.get()||!pack->mImpl->mPack.get()||
               !pack->mImpl->mPack->mManifest||
               !pack->mImpl->mPack->mAccessStrategy)continue;
            auto const& native=*pack->mImpl->mPack.get();
            auto const& id=native.mManifest->mIdentity.get();
            if(static_cast<unsigned>(id.mPackType)!=6 ||
               id.mId->asString()!=value.uuid||
               id.mVersion->asString()!=value.version||
               world2660NormalPath(native.mAccessStrategy->getPackLocation().mPath.get().value)!=
               world2660NormalPath(it->key))continue;
            auto owned=repo.getResourcePackSatisfiesPackId(id,true,ResourcePackSelectionPolicy::Default);
            if(!owned||owned.get()!=pack||matched)return false;
            matched=std::move(owned);
        }
        if(!matched)return false;
        after.emplace_back(matched->mImpl->mPack->mManifest->mIdentity.get(),std::string{});
    }
    auto nonOwner=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
        Bedrock::NonOwnerPointer<IResourcePackRepository const>(repo)};
    auto stage=std::make_unique<ResourcePackStack>(after,nonOwner,false);
    auto restore=std::make_unique<ResourcePackStack>(before,nonOwner,false);
    std::uint32_t reason{};
    auto proposed=snapshot(stage.get(),reason);
    auto original=snapshot(restore.get(),reason);
    if(!proposed.known||!original.known||
       !same(proposed.order,plan.target)||!same(original.order,global.order))
        return false;

    op=World2660Operation{};
    op.step=World2660Step::Pausing;
    op.token=request.token;
    op.owner=reinterpret_cast<std::uintptr_t>(frame.manager);
    op.client=reinterpret_cast<std::uintptr_t>(frame.client.get());
    op.renderer=frame.renderer;
    op.generation=frame.generation;
    op.deadline=now+30000;
    op.originalIDs=std::move(before);
    op.desiredIDs=std::move(after);
    op.original=global.order;
    op.desired=std::move(plan.target);
    op.level=level.order;
    op.base=base.order;
    op.treatment=treatment.order;
    op.full=full.order;
    if(!world2660ResolvePeers()){op.step=World2660Step::Failed;return false;}
    op.request={};
    op.request.token=request.token;
    op.request.world=frame.generation;
    op.request.client=op.client;
    op.request.renderer=op.renderer;
    op.request.oldEpoch=currentResourceEpoch();
    op.request.nextEpoch=op.request.oldEpoch+1;
    if(!op.request.oldEpoch)return false;
    unsigned paused{};
    for(unsigned i=0;i<op.peerCount;++i){
        auto reply=world2660Call(i,WorldPackReloadCommand::Pause);
        if(reply.error){
            for(unsigned j=0;j<paused;++j){
                world2660Call(j,WorldPackReloadCommand::CancelBeforeNative);
                world2660Call(j,WorldPackReloadCommand::ResumeCancelled);
            }
            op.step=World2660Step::Failed;
            return false;
        }
        ++paused;
    }
    if(!titlePackControl().claimSetter(op.token,1)){
        world2660ClosePeers(false);
        op.step=World2660Step::Failed;
        return false;
    }
    op.status="ワールド内の描画・非同期処理の停止を確認しています。";
    return true;
}
bool world2660ExecuteNative(MinecraftGame& game,World2660Frame const& frame,ResourcePackManager& manager,
                             bool restore,std::uint64_t now){
    auto& op=worldOperation;
    if(reinterpret_cast<std::uintptr_t>(&manager)!=op.owner||
       op.deadline<now||!world2660LayerStillOwned(manager,op.level,op.base,op.treatment))
        return false;
    auto& repo=frame.client->getResourcePackRepository();
    auto nonOwner=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
        Bedrock::NonOwnerPointer<IResourcePackRepository const>(repo)};
    auto const& ids=restore?op.originalIDs:op.desiredIDs;
    auto candidate=std::make_unique<ResourcePackStack>(ids,nonOwner,false);
    std::uint32_t reason{};
    auto copied=snapshot(candidate.get(),reason);
    if(!copied.known||!same(copied.order,restore?op.original:op.desired))
        return false;
    auto receiptOwner=static_cast<std::uint64_t>(op.owner);
    if(!op.receipt.beginNativeCall(receiptOwner,now))return false;
    worldNativeThread.store(GetCurrentThreadId(),std::memory_order_release);
    struct Scope {
        ~Scope(){worldNativeThread.store(0,std::memory_order_release);}
    } resetThread;
    bool accepted=manager.setStack(std::move(candidate),ResourcePackStackType::Global);
    op.setterAttempted=true;
    op.receipt.setterResult(receiptOwner,accepted);
    if(!accepted)return false;
    manager._composeFullStack();
    game.onActiveResourcePacksChanged(manager);
    return op.receipt.endNativeCall(receiptOwner,titlePackNow());
}
void world2660Advance(MinecraftGame& game,World2660Frame const& frame,
                      AppliedStackView const& global,std::uint64_t now) {
    auto& op=worldOperation;
    if(op.step==World2660Step::Idle||op.step==World2660Step::Failed||
       op.step==World2660Step::Finished)return;
    if(reinterpret_cast<std::uintptr_t>(frame.manager)!=op.owner ||
       reinterpret_cast<std::uintptr_t>(frame.client.get())!=op.client ||
       op.generation!=frame.generation){
        world2660Finish(World2660Step::Failed,
            "ワールドが切り替わったため復元を確認できません。再起動してください。",true);
        return;
    }
    if(now>op.deadline){
        if(!op.setterAttempted)world2660ClosePeers(false);
        world2660Finish(World2660Step::Failed,
            "ワールド再読み込みの時間切れです。Minecraftを再起動してください。",
            op.setterAttempted);
        return;
    }
    if(op.step==World2660Step::Pausing){
        bool allPrepared=true;
        for(unsigned i=0;i<op.peerCount;++i){
            auto reply=world2660Call(i,WorldPackReloadCommand::Prepare);
            if(!world2660Receipt(reply,WorldPackReloadPhase::Prepared,
                                   op.request.oldEpoch)) allPrepared=false;
        }
        if(!allPrepared)return;
        if(!world2660All(WorldPackReloadCommand::BeginNative,
                         WorldPackReloadPhase::NativeActive,op.request.oldEpoch)){
            // A partial BeginNative has no ordinary CancelBeforeNative path.
            // No Global setter occurred, but a peer may own a closed epoch.
            world2660Finish(World2660Step::Failed,
                "描画停止の合意に失敗しました。再起動してください。",true);
            return;
        }
        if(!op.receipt.prepare(op.token,op.owner,
            world2660Keys(op.original),world2660Keys(op.desired),now+180000,
            same(op.original,op.desired),true)){
            // Peers are already NativeActive. Stop further admission; no
            // engine stack mutation has started.
            world2660Finish(World2660Step::Failed,
                "パック構成の完了判定を初期化できませんでした。",true);
            return;
        }
        op.step=World2660Step::WaitingNative;
        op.deadline=now+180000;
        try{
            if(!world2660ExecuteNative(game,frame,*frame.manager,false,now)){
                op.step=World2660Step::Settling;
                return;
            }
        }catch(...){
            op.receipt.markRecoveryRequired();
            world2660Finish(World2660Step::Failed,
               "適用中に例外が発生しました。元の構成へ再入場が必要です。",true);
            return;
        }
        op.step=World2660Step::Settling;
        op.status="Minecraftがワールド内リソースを読み込み中です。";
        return;
    }
    if(op.step==World2660Step::Settling) {
        // The finish callback may legitimately span a loading screen.
        // Process an explicit rollback receipt immediately; otherwise
        // wait for native loading to settle before sampling the GPU view.
        if(op.receipt.phase()!=Title2660Receipt::Phase::RollbackRequired &&
           (!frame.renderReady || frame.manager->mPendingRestack))return;
        auto verdict=op.receipt.poll(op.owner,
            worldUpdateSerial.load(std::memory_order_acquire),now,
            world2660Keys(global.order),frame.normal,false,
            world2660LayerStillOwned(*frame.manager,op.level,op.base,op.treatment));
        if(verdict==Title2660Receipt::Poll::NeedRollback){
            if(!op.receipt.beginRollback(op.owner,now+30000)){
                world2660Finish(World2660Step::Failed,"復元の開始を確認できません。",true);
                return;
            }
            op.rollback=true;op.deadline=now+30000;
            op.status="変更が確定できなかったため、元のパックへ復元中です。";
            try {
                if(!world2660ExecuteNative(game,frame,*frame.manager,true,now)){
                    world2660Finish(World2660Step::Failed,"ワールド内復元が失敗しました。",true);
                    return;
                }
            }catch(...){
                world2660Finish(World2660Step::Failed,"ワールド内復元中に例外が発生しました。",true);
                return;
            }
            return;
        }
        if(verdict==Title2660Receipt::Poll::RecoveryRequired){
            world2660Finish(World2660Step::Failed,"ネイティブ再読み込みの完了が証明できません。",true);
            return;
        }
        if(verdict==Title2660Receipt::Poll::Wait)return;
        if(verdict!=Title2660Receipt::Poll::Applied &&
           verdict!=Title2660Receipt::Poll::Restored)return;
        if(!op.geometryRebuilt && frame.client->getLevelRenderer()){
            frame.client->getLevelRenderer()->rebuildAllRenderChunkGeometry();
            op.geometryRebuilt=true;
        }
        op.step=World2660Step::Reacquiring;
        op.deadline=now+30000;
        // The native reload can legitimately recreate the world renderer.
        op.request.renderer=frame.renderer;
    }
    if(op.step==World2660Step::Reacquiring){
        if(!world2660All(WorldPackReloadCommand::NativeSettled,
                          WorldPackReloadPhase::Reacquiring,op.request.nextEpoch) &&
           !world2660All(WorldPackReloadCommand::Query,
                          WorldPackReloadPhase::Ready,op.request.nextEpoch))return;
        if(!world2660All(WorldPackReloadCommand::Query,
                         WorldPackReloadPhase::Ready,op.request.nextEpoch))return;
        if(!world2660All(WorldPackReloadCommand::Resume,
                         WorldPackReloadPhase::Idle,op.request.nextEpoch)){
            world2660Finish(World2660Step::Failed,
              "LHoloまたはPraxisの描画復帰を確認できません。",true);
            return;
        }
        world2660Finish(World2660Step::Finished,
            op.rollback ? "元のパック構成と描画の復元を確認しました。" :
            "パック変更・再読み込み・描画再開を確認しました。");
    }
}
