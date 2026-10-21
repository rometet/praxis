#include "resourcepacks/TitlePackSwitch.h"
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/TitlePackNativeAdapter.h"
#include "resourcepacks/TitleNativeCast.h"
#include "core/NativeHookLifetime.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/screens/controllers/MainMenuScreenController.h"
#include "mc/client/gui/screens/models/MainMenuScreenModel.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/Pack.h"
#include "mc/resources/PackAccessStrategy.h"
#include "mc/resources/IResourcePackRepository.h"
#include "mc/scripting/modules/PackManifest.h"
#include "mc/world/level/PackInstanceId.h"
#include "mc/deps/core/threading/BackgroundTask.h"
#include "mc/deps/core/threading/BackgroundTaskBase.h"
#include "mc/deps/core/threading/TaskResult.h"
#include <Windows.h>
#include <bcrypt.h>
#include <atomic>
#include <array>
#include <thread>
#include <cstdio>

namespace praxis::companion::packs {
namespace {
auto& lifetime=*new NativeHookLifetime;
std::atomic_bool attempted{}, installed{}, imageReady{}, imageFailed{}, worldSeen{};
std::atomic_bool imageReported{}, observationLost{};
std::atomic_bool titleTypeUnavailable{};
std::atomic<unsigned> callbacks{};
std::atomic<DWORD> ownerThread{};
std::atomic<std::uint64_t> revision{1}, engineCycles{};
std::atomic_flag ticking=ATOMIC_FLAG_INIT;
void anchor(){}
bool pin(){HMODULE h{};return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&anchor),&h)!=0;}
std::uintptr_t address(void const* p){return reinterpret_cast<std::uintptr_t>(p);}
// Candidate compatibility qualification for the specifically requested game
// image and generated SDK field/union ABI. This does not resolve a function,
// inspect instructions, or validate a PreLoader callable numerically. The SDK
// exposes no runtime verification of ScreenEventData's owning-copy/layout
// contract; admit only the already audited image until real-machine validation.
// Only this process-pinned worker performs file I/O; no game object is read by a worker.
bool exactImage(){
    constexpr std::array<unsigned char,32> expected{0xf8,0x2d,0xc9,0x67,0xd3,0x80,0x0b,0x7b,0x89,0x2c,0x38,0xa9,0x7b,0x67,0x2e,0x5f,0x32,0x57,0xd9,0xc0,0xc0,0xcf,0xfe,0xcc,0xdc,0x5f,0x83,0xde,0x02,0x36,0x3b,0x2a};
    std::array<wchar_t,32768> path{};auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!count||count>=path.size())return false;
    HANDLE file=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct FileExit {HANDLE h;~FileExit(){CloseHandle(h);}} fileExit{file};
    LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart!=311249032)return false;
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    struct HashExit {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~HashExit(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} hashExit{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectSize{},got{};if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&got,0)<0)return false;
    std::vector<unsigned char> object(objectSize);if(BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> buffer{};std::uint64_t total{};
    for(;;){DWORD read{};if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))return false;
        if(!read)break;if(titlePackControl().stopped()||BCryptHashData(hash,buffer.data(),read,0)<0)return false;total+=read;}
    std::array<unsigned char,32> result{};return total==311249032&&BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0&&result==expected;
}
std::vector<std::string> keys(std::vector<PackInstanceId> const& ids){
    std::vector<std::string> out;out.reserve(ids.size());for(auto const& id:ids)
        out.push_back(std::to_string(unsigned(id.mPackId->mPackType))+"/"+id.mPackId->mId->asString()+"/"+id.mPackId->mVersion->asString()+"/"+id.mSubpackName.get());return out;
}
std::vector<PackInstanceId> ids(ResourcePackStack const* stack){
    if(!stack)return {};if(stack->mStack->size()>512)throw std::runtime_error("stack budget");return stack->getStackAsIdentities();
}
AppliedStackView viewIds(std::vector<PackInstanceId> const& values){
    AppliedStackView out;out.known=true;
    for(auto const& id:values)
        out.order.push_back({id.mPackId->mId->asString(),id.mPackId->mVersion->asString(),id.mSubpackName.get(),unsigned(id.mPackId->mPackType)});
    return out;
}
struct Frame {
    TitlePackOwner owner;
    std::shared_ptr<IClientInstance> client;
    std::shared_ptr<MainMenuScreenModel> model;
    bool start{};
    bool typeInfoUnavailable{};
};
Frame frame(MinecraftGame& game){
    Frame out;auto thread=GetCurrentThreadId();DWORD zero{};ownerThread.compare_exchange_strong(zero,thread);
    if(thread!=ownerThread.load()||!runtimeReady.load()||game.isEditorModeEnabled())return out;
    if(game.isInGame()||worldGeneration.load()){worldSeen=true;return out;}
    auto clients=game.getClientInstanceMap();if(clients.size()!=1)return out;
    out.client=clients.begin()->second;if(!out.client)return {};
    if(out.client->getLocalPlayer()){worldSeen=true;return {};}
    if(worldSeen.load())return {};
    auto& manager=game.getResourcePackManager();
    out.owner={address(&game),address(&manager),address(out.client.get()),0,thread,true,false,manager.mPendingRestack,game.isReadyToRender()};
    // Startup/loading/OreUI screens are not title application owners.
    if(out.client->getScreenName()!="start_screen")return out;
    // A known missing RTTI contract stays closed; do not throw each update.
    if(titleTypeUnavailable.load()){out.typeInfoUnavailable=true;return out;}
    auto scene=titleNativeSharedCast<UIScene>(out.client->getCurrentSceneStack()->getTopSceneShared(),out.typeInfoUnavailable);
    if(!scene){if(out.typeInfoUnavailable)titleTypeUnavailable=true;return out;}
    if(!scene->mScreenView.get())return out;
    std::shared_ptr<ScreenController> controllerOwner=scene->mScreenView->mController;
    auto controller=titleNativeSharedCast<MainMenuScreenController>(controllerOwner,out.typeInfoUnavailable);
    if(!controller){if(out.typeInfoUnavailable)titleTypeUnavailable=true;return out;}
    out.model=controller->mMainMenuScreenModel;if(!out.model)return out;
    if(&out.model->getResourcePackManager()!=&manager||out.model->isInGame())return {};
    out.start=out.client->getScreenName()=="start_screen";
    out.owner={address(&game),address(&manager),address(out.client.get()),0,thread,true,out.model->isResourceLoadingFinished(),manager.mPendingRestack,game.isReadyToRender()};
    return out;
}
struct Operation {
    std::mutex mutex;
    TitlePackTransaction transaction;
    TitleNativeContext nativeCycle;
    TitlePackRequest request;
    std::shared_ptr<IClientInstance> client;
    std::shared_ptr<MainMenuScreenModel> model;
    std::vector<PackInstanceId> oldIds, newIds;
    std::vector<std::string> level,treatment,base;
    std::vector<std::shared_ptr<ResourcePack>> ownedPacks;
    std::unique_ptr<ResourcePackStack> candidate, backup;
    std::string detail;
    TitlePackPhase logged{TitlePackPhase::Idle};
};
auto& active=*new std::atomic<std::shared_ptr<Operation>>;
// Hold SDK owners/callback captures until process exit, including uncertainty.
// Cap retained operations so repeated title use cannot grow without bound.
auto& retained=*new std::vector<std::shared_ptr<Operation>>;
constexpr std::size_t OperationBudget=16;
void status(std::shared_ptr<Operation> const& op,TitlePackView& view){
    if(!op)return;char message[192]{};
    {std::lock_guard lock(op->mutex);view.token=op->request.token;view.phase=op->transaction.phase;view.busy=op->transaction.busy();
        view.status=op->detail.empty()?titlePackStatusJa(view.phase):op->detail;
        if(view.phase==TitlePackPhase::RecoveryRequired)view.available=false;
        if(op->logged!=view.phase){op->logged=view.phase;sprintf_s(message,"RESOURCEPACK_TITLE token=%llu attempt=%llu phase=%u start=%u returned=%u finish=%u title_only=1 runtime_acceptance=unverified",
            op->transaction.token,op->transaction.attempt,unsigned(view.phase),unsigned(op->transaction.started),unsigned(op->transaction.startReturned),unsigned(op->transaction.finished));}}
    if(*message)logGuiInfo(message);
}
bool pathEqual(std::string const& a,std::string const& b){
    auto path=[](std::string const& s){return std::filesystem::path(std::u8string_view(reinterpret_cast<char8_t const*>(s.data()),s.size())).lexically_normal().wstring();};
    auto x=path(a),y=path(b);return !x.empty()&&CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;
}
std::string packPath(ResourcePack const& pack){auto const& data=pack.mImpl.get()->mPack.get();return data&&data->mAccessStrategy?data->mAccessStrategy->getPackLocation().getFullPath().value:std::string{};}
bool ordinary(ResourcePack const& pack,Pack const& entry){
    auto const& native=pack.mImpl.get()->mPack.get();if(!native||!native->mManifest||!pathEqual(packPath(pack),entry.key))return false;
    auto const& manifest=*native->mManifest;auto const& identity=manifest.mIdentity.get();
    return identity.mPackType==PackType::Resources&&identity.mId->asString()==entry.uuid&&identity.mVersion->asString()==entry.version
        &&!manifest.mExpired&&!manifest.mIsTitleLocked&&!manifest.mIsPlatformLocked&&!manifest.mIsHidden&&!manifest.mFailsGameVersionMatch
        &&!manifest.mHasClientScript&&!manifest.mHasPlugins&&manifest.mPackDependencies->empty()&&manifest.mCapabilities->empty();
}
void prepare(std::shared_ptr<Operation> const& op,MinecraftGame& game,Frame const& current){
    auto& manager=game.getResourcePackManager();auto& repository=game.getResourcePackRepository();
    auto const& catalog=*op->request.catalog;
    auto rootBytes=catalog.root.u8string();std::string root(reinterpret_cast<char const*>(rootBytes.data()),rootBytes.size());
    if(!pathEqual(root,repository.getResourcePacksPath().value)&&!pathEqual(root,repository.getDevelopmentResourcePacksPath().value))throw std::runtime_error("通常 User/Dev の登録済みフォルダを選択してください。");
    op->oldIds=ids(manager.mGlobalStack.get());op->level=keys(ids(manager.mLevelStack.get()));
    op->treatment=keys(ids(manager.mTreatmentStack.get()));op->base=keys(ids(manager.mBaseGameStack.get()));
    auto local=repository.getPacksByResourceLocation(PackOrigin::User);auto dev=repository.getPacksByResourceLocation(PackOrigin::Dev);local.insert(local.end(),dev.begin(),dev.end());
    if(local.size()>512)throw std::runtime_error("登録済み pack の確認上限を超えました。");
    std::vector<PackInstanceId> selected;
    for(auto const& key:op->request.selection.order){
        auto entry=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key&&p.selectable;});
        if(entry==catalog.packs.end())throw std::runtime_error("候補が古くなりました。一覧を更新してください。");
        std::shared_ptr<ResourcePack> found;
        for(auto* pack:local){if(!pack||!pathEqual(packPath(*pack),entry->key))continue;
            auto const& native=pack->mImpl.get()->mPack.get();if(!native||!native->mManifest)continue;
            auto const& identity=native->mManifest->mIdentity.get();if(!ordinary(*pack,*entry))continue;
            auto owned=repository.getResourcePackSatisfiesPackId(identity,true);if(!owned||owned.get()!=pack||found)throw std::runtime_error("pack の所有権または一意な登録を確認できません。");
            found=std::move(owned);
        }
        if(!found)throw std::runtime_error("未登録・非対応の候補です。通常の管理機能で確認してください。");
        auto const& identity=found->mImpl.get()->mPack.get()->mManifest->mIdentity.get();
        selected.emplace_back(identity,std::string{});op->ownedPacks.push_back(std::move(found));
    }
    // Preserve out-of-scope Global anchors; selected ordinary textures replace
    // only editable old slots. Level/Treatment/Base are never passed to setter.
    auto merged=mergeTitleGlobal<PackInstanceId>(op->oldIds,selected,[&](PackInstanceId const& old){
        auto editable=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.selectable&&old.mSubpackName->empty()&&old.mPackId->mPackType==PackType::Resources&&old.mPackId->mId->asString()==p.uuid&&old.mPackId->mVersion->asString()==p.version;});
        if(editable!=catalog.packs.end()){
            auto owned=repository.getResourcePackSatisfiesPackId(old.mPackId.get(),true);
            if(!owned||std::find(local.begin(),local.end(),owned.get())==local.end()||!ordinary(*owned,*editable))throw std::runtime_error("現在の pack は通常の管理機能で変更してください。");
            op->ownedPacks.push_back(std::move(owned));
        }
        return editable!=catalog.packs.end();
    },[](auto const& a,auto const& b){return a.mPackId->mId->asString()==b.mPackId->mId->asString();});
    if(!merged)throw std::runtime_error("対象外の現在 pack と UUID が競合しています。");op->newIds=std::move(*merged);
    if(op->newIds.size()>512)throw std::runtime_error("Global stack の確認上限を超えました。");
    auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{Bedrock::NonOwnerPointer<IResourcePackRepository const>(repository)};
    op->candidate=std::make_unique<ResourcePackStack>(op->newIds,repo,false);op->backup=std::make_unique<ResourcePackStack>(op->oldIds,repo,false);
    if(keys(ids(op->candidate.get()))!=keys(op->newIds)||keys(ids(op->backup.get()))!=keys(op->oldIds))throw std::runtime_error("通常の stack 解決が候補または旧構成と一致しません。");
    if(!current.owner.usable()||op->request.revision!=revision.load())throw std::runtime_error("タイトルの現在状態が変わりました。");
    std::lock_guard lock(op->mutex);op->transaction.prepared(keys(op->oldIds),keys(op->newIds),titlePackNow());
}
void applyCallback(std::shared_ptr<Operation> const& op,MinecraftScreenModel& model) noexcept {
    runTitlePackApplyCallback(lifetime,callbacks,titlePackControl(),op->mutex,op->transaction,
        [&]{
            auto& manager=model.getResourcePackManager();TitlePackOwner context;
            {std::lock_guard lock(op->mutex);context=op->transaction.owner;}
            context.fresh=!worldSeen.load()&&!worldGeneration.load()&&runtimeReady.load()&&!model.isInGame()
                &&!op->client->getLocalPlayer()&&address(&manager)==context.manager
                &&model.mClient.get().get()==op->client.get()&&GetCurrentThreadId()==context.thread;
            context.loadingFinished=model.isResourceLoadingFinished();context.pending=manager.mPendingRestack;
            return context;
        },
        [&](TitlePackAction action){
            auto& manager=model.getResourcePackManager();std::vector<std::string> expected;
            {std::lock_guard lock(op->mutex);expected=action==TitlePackAction::Apply?keys(op->oldIds):op->transaction.targetGlobal;}
            return keys(ids(manager.mGlobalStack.get()))==expected&&keys(ids(manager.mLevelStack.get()))==op->level
                &&keys(ids(manager.mTreatmentStack.get()))==op->treatment&&keys(ids(manager.mBaseGameStack.get()))==op->base;
        },
        [&](TitlePackAction action){
            auto& manager=model.getResourcePackManager();
            bool accepted=manager.setStack(action==TitlePackAction::Apply?std::move(op->candidate):std::move(op->backup),ResourcePackStackType::Global);
            return TitleSetterResult{accepted,!accepted&&keys(ids(manager.mGlobalStack.get()))==keys(op->oldIds)};
        },titlePackNow());
}

void navigate(std::shared_ptr<Operation> const& op){
    op->model->navigateToApplyResourcePacksScreen([op](MinecraftScreenModel& model){applyCallback(op,model);});
}
void tick(MinecraftGame& game) noexcept {
    if(!installed.load()||titlePackControl().stopped()||(ownerThread.load()&&ownerThread.load()!=GetCurrentThreadId())||ticking.test_and_set())return;
    struct Untick {~Untick(){ticking.clear();}} untick;
    if(game.isInGame()||worldGeneration.load()){
        auto op=active.load();bool busy{};
        if(op){std::lock_guard lock(op->mutex);busy=op->transaction.busy();}
        if(!busy)return; // The independent world coordinator owns the page here.
    }
    TitlePackView view;view.revision=revision.load();view.until=titlePackNow()+1000;
    try{
        if(!imageReady.load()){view.status=imageFailed.load()?"対象ゲームの identity を確認できません。":"対象ゲームの identity を別スレッドで確認中です。";titlePackControl().publish(std::move(view));return;}
        if(!imageReported.exchange(true))logGuiInfo("RESOURCEPACK_TITLE image_guard=verified formal_sdk=26.51.5 route=native_application_screen title_only=1");
        auto current=frame(game);auto op=active.load();
        if(observationLost.load()&&op){std::lock_guard lock(op->mutex);op->transaction.recovery();}
        view.available=current.start&&current.owner.usable()&&!callbacks.load()&&!observationLost.load()&&retained.size()<OperationBudget;
        if(current.owner.usable()&&current.start&&!callbacks.load()){
            auto& manager=game.getResourcePackManager();view.current=viewIds(ids(manager.mGlobalStack.get()));
            auto& repo=game.getResourcePackRepository();view.folders={repo.getResourcePacksPath().value,repo.getDevelopmentResourcePacksPath().value};
            view.status=titlePackStatusJa(TitlePackPhase::Idle);
        }else view.status=current.typeInfoUnavailable
            ?"タイトルの native 型情報を確認できないため、タイトルでの適用は利用できません。ワールド内の通常 texture 適用を使用してください。"
            :"適用前提: 起動後まだワールド未入場・通常タイトル・読込終了。ワールド内と退出後は未対応です。";
        if(op&&titlePackControl().cancelled(op->request.token)){std::lock_guard lock(op->mutex);op->transaction.cancel();}
        if(op){
            bool busy;{std::lock_guard lock(op->mutex);busy=op->transaction.busy();}
            if(busy){
                TitlePackSettlement settled;settled.owner=current.owner;settled.startScreen=current.start;settled.callbacksIdle=!callbacks.load();
                if(current.owner.usable()&&current.start&&settled.callbacksIdle){
                    auto& manager=game.getResourcePackManager();settled.global=keys(ids(manager.mGlobalStack.get()));
                    settled.otherStacksUnchanged=keys(ids(manager.mLevelStack.get()))==op->level&&keys(ids(manager.mTreatmentStack.get()))==op->treatment&&keys(ids(manager.mBaseGameStack.get()))==op->base;
                }
                TitlePackAction action;{std::lock_guard lock(op->mutex);action=op->transaction.poll(settled,titlePackNow());}
                if(action==TitlePackAction::NavigateRollback){
                    {std::lock_guard lock(op->mutex);op->transaction.targetGlobal=settled.global;op->transaction.rollbackNavigation(titlePackNow());}
                    navigate(op);
                }
            }
        }
        bool rejectedRequest=false;
        if(auto request=titlePackControl().take()){
            bool busy{};if(op){std::lock_guard lock(op->mutex);busy=op->transaction.busy();}
            if(!busy&&view.available&&request->revision==revision.load()){
                op=std::make_shared<Operation>();op->request=std::move(*request);op->client=current.client;op->model=current.model;
                op->transaction.begin(op->request.token,current.owner,titlePackNow());retained.push_back(op);active.store(op);
                try{prepare(op,game,current);bool awaiting{};{std::lock_guard lock(op->mutex);awaiting=op->transaction.phase==TitlePackPhase::AwaitApply;}if(awaiting)navigate(op);}
                catch(std::exception const& error){std::lock_guard lock(op->mutex);if(op->transaction.phase==TitlePackPhase::Preparing||op->transaction.phase==TitlePackPhase::AwaitApply)op->transaction.reject();else op->transaction.recovery();op->detail=error.what();}
            }else {rejectedRequest=true;view.token=request->token;view.observation={};view.phase=TitlePackPhase::Rejected;view.status="現在状態が変わったため、適用を開始していません。";}
        }
        if(!rejectedRequest)status(op,view);if(view.busy)view.available=false;
        if(retained.size()>=OperationBudget){view.available=false;view.status="保持した適用履歴の上限です。再起動後に適用してください。";}
        titlePackControl().publish(std::move(view));
    }catch(...){if(auto op=active.load()){std::lock_guard lock(op->mutex);op->transaction.recovery();}view.available=false;view.current={};view.status="現在状態の確認に失敗しました。適用を停止しています。";titlePackControl().publish(std::move(view));}
}
struct NativeCycle {std::shared_ptr<Operation> op;TitleNativeContext context;};
NativeCycle entered(MinecraftGame& game,ResourcePackManager& manager,bool fullVanilla){
    revision.fetch_add(1);auto op=active.load();if(!op)return {};
    std::lock_guard lock(op->mutex);if(!op->transaction.busy())return {};
    auto owner=op->transaction.owner;owner.game=address(&game);owner.manager=address(&manager);owner.thread=GetCurrentThreadId();owner.fresh=!worldSeen.load()&&!worldGeneration.load();
    auto context=beginTitleNativeCycle(op->transaction,titleNativeContext,owner,fullVanilla,engineCycles.fetch_add(1)+1);
    if(!context.receipt())return {};op->nativeCycle=context;return {op,context};
}
// These are the same exact SDK targets/signatures already used by the passive
// observer. No new native target/RVA; each detour calls origin exactly once.
LL_TYPE_INSTANCE_HOOK(TitleUpdateHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::$update,void){
    NativeHookLifetime::Lease lease(lifetime);origin();if(lease.admitted())tick(*this);
}
LL_TYPE_INSTANCE_HOOK(TitleActiveHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_onActiveResourcePacksChanged,void,ResourcePackManager& manager,bool fullVanilla){
    NativeHookLifetime::Lease lease(lifetime);NativeCycle cycle;
    bool counted=lease.admitted();if(counted){callbacks.fetch_add(1);try{cycle=entered(*this,manager,fullVanilla);}catch(...){observationLost=true;}}
    struct Exit {bool admitted;~Exit(){if(admitted)callbacks.fetch_sub(1);}} exit{counted};
    TitleNativeScope nativeScope(cycle.context);
    try{origin(manager,fullVanilla);}catch(...){if(cycle.op){std::lock_guard lock(cycle.op->mutex);cycle.op->transaction.nativeStartReturned(cycle.context.attempt,false);}throw;}
    if(cycle.op){std::lock_guard lock(cycle.op->mutex);cycle.op->transaction.nativeStartReturned(cycle.context.attempt,true);}
}
LL_TYPE_INSTANCE_HOOK(TitleFinishHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_finishReloadingResources,void,ResourcePackManager& manager,bool fullVanilla){
    TitleCallbackLease lease(lifetime,callbacks);
    auto op=lease.admitted()?active.load():nullptr;
    // Capture inherited provenance at entry. Never relabel a notification with
    // whichever attempt is currently active; unknown still runs normal origin.
    auto inherited=titleNativeContext;
    if(!op){origin(manager,fullVanilla);return;}
    observeTitleNativeFinish(op->mutex,op->transaction,op->nativeCycle,inherited,
        address(this),address(&manager),worldGeneration.load(),fullVanilla,[&]{origin(manager,fullVanilla);});
}
using TitleWorkerTask=BackgroundTask<TaskResult,void>;
using TitleCallbackTask=BackgroundTask<void,void>;
// Propagate immutable provenance through the engine's actual created callable,
// including nested and asynchronous native callbacks. No external task invoke.
// Both targets/signatures already exist in the passive observer.
template<class Result,class Factory>
auto titleFactory(gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,
    Bedrock::Threading::Async<void> predecessor,bool async,brstd::move_only_function<Result()> task,Factory&& factory){
    bool lost{};
    try{
        auto result=wrapTitleNativeCallable<Result>(titleNativeContext,lifetime,callbacks,std::move(task),
            [&](auto body){if(lost)observationLost=true;return factory(owner,info,std::move(predecessor),async,std::move(body));},lost);
        if(lost)observationLost=true;return result;
    }catch(...){if(lost)observationLost=true;throw;}
}
LL_TYPE_STATIC_HOOK(TitleWorkerFactoryHook,ll::memory::HookPriority::Normal,TitleWorkerTask,&TitleWorkerTask::create,std::shared_ptr<TitleWorkerTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<TaskResult()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return titleFactory<TaskResult>(owner,info,std::move(predecessor),async,std::move(task),
        [](auto owner,auto const& info,auto predecessor,bool async,auto task){return origin(owner,info,std::move(predecessor),async,std::move(task));});
}
LL_TYPE_STATIC_HOOK(TitleCallbackFactoryHook,ll::memory::HookPriority::Normal,TitleCallbackTask,&TitleCallbackTask::create,std::shared_ptr<TitleCallbackTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<void()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return titleFactory<void>(owner,info,std::move(predecessor),async,std::move(task),
        [](auto owner,auto const& info,auto predecessor,bool async,auto task){return origin(owner,info,std::move(predecessor),async,std::move(task));});
}

}
bool installTitlePackSwitch() noexcept {
    if(attempted.exchange(true))return installed.load();
    try{
        if(!lifetime.prepare(pin))return false;lifetime.registrationStarted();
        if(TitleUpdateHook::hook()!=0||TitleActiveHook::hook()!=0||TitleFinishHook::hook()!=0||TitleWorkerFactoryHook::hook()!=0||TitleCallbackFactoryHook::hook()!=0||!lifetime.open()){lifetime.close();return false;}
        installed=true;
        std::thread([]{try{auto ready=exactImage();imageReady=ready;imageFailed=!ready;}catch(...){imageFailed=true;}}).detach();return true;
    }catch(...){installed=false;lifetime.close();return false;}
}
bool stopTitlePackSwitch() noexcept {
    installed=false;titlePackControl().close();lifetime.close();
    if(auto op=active.load()){std::lock_guard lock(op->mutex);if(op->transaction.busy())op->transaction.recovery();}
    // No waits, native setters or destruction of captured SDK owners on stop.
    return lifetime.readers()==0;
}
bool resourcePackImageVerified() noexcept {return imageReady.load();}
}
