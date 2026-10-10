#ifdef PRAXIS_PASSIVE_OBSERVER_LIFECYCLE_TEST
// Exercise these exact install/stop bodies with only the OS/registration boundary
// doubled. Production detours and SDK signatures still compile in NativeProbe.
#include "passive/ObserverLifecycleHarness.h"
#else
#include "resourcepacks/PassiveReloadObserver.h"
#include "resourcepacks/TaskOutcome.h"
#include "resourcepacks/PassiveReloadTraceRuntime.h"
#include "resourcepacks/PassiveReloadUpdate.h"
#include "core/NativeHookLifetime.h"
#include "core/RuntimePolicy.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourceLoadManager.h"
#include "mc/deps/core/threading/TaskGroup.h"
#include "mc/deps/core/threading/BackgroundTaskBase.h"
#include "mc/deps/core/threading/BackgroundTask.h"
#include "mc/deps/core/threading/TaskResult.h"
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <utility>

namespace praxis::companion::packs {
namespace {
// Retain CPU registries and detours until process exit, including partial install
// and task capsules whose original callable outlives an admitted callback.
auto& lifetime=*new NativeHookLifetime;
std::atomic_bool installAttempted{};
std::atomic_bool observerStopping{};
std::atomic<unsigned> installers{};
std::atomic<std::uint64_t> nativeRevision{1};
// Passive diagnostic only. No native mutation, pack UI or resource retirement.
auto& taskLineage=*new TaskLineage;
auto& taskOutcomes=*new TaskOutcomeRegistry;
std::atomic_bool lineageLost{};
std::atomic_bool passiveObserversInstalled{};
thread_local NativeTaskStamp enclosingTaskCycle;
template<class Fn> void observeTasks(Fn&& fn)noexcept {
    try{std::forward<Fn>(fn)();}catch(...){lineageLost=true;passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);}
}
struct TaskCycleScope {
    NativeTaskStamp previous,current;
    explicit TaskCycleScope(NativeTaskStamp stamp):previous(enclosingTaskCycle),current(stamp){
        observeTasks([&]{taskLineage.begin(current);if(taskLineage.view(current).poisoned){lineageLost=true;passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);}});enclosingTaskCycle=current;
    }
    ~TaskCycleScope(){enclosingTaskCycle=previous;observeTasks([&]{taskLineage.initialEnqueueReturned(current);});}
};
void anchor(){}
bool pin(){HMODULE h{};return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
    reinterpret_cast<LPCWSTR>(&anchor),&h)!=0;}
std::uintptr_t address(void const* p){return reinterpret_cast<std::uintptr_t>(p);}
NativeTaskStamp currentTaskStamp()noexcept {
    NativeTaskStamp stamp;
    if(!passiveReloadTrace.enabled()||!passiveReloadTrace.view().session)return stamp;
    observeTasks([&]{
        auto executing=address(BackgroundTaskBase::gCurrentTask());
        auto parent=taskLineage.lookup(executing);
        if(executing&&!parent){
            if(enclosingTaskCycle.valid())passiveReloadTrace.unknown(ReloadTraceEvent::CallbackUnknown,GetTickCount64());
            return;
        }
        if(parent&&taskLineage.view(parent->stamp).poisoned){
            lineageLost=true;passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);return;
        }
        if(parent)stamp=parent->stamp;
        if(enclosingTaskCycle.valid()){
            if(stamp.valid()&&!sameTaskCycle(stamp,enclosingTaskCycle)){
                taskLineage.poison(stamp);taskLineage.poison(enclosingTaskCycle);lineageLost=true;return;
            }
            stamp=enclosingTaskCycle;
        }
        stamp=passiveReloadParentStamp(executing,parent?&*parent:nullptr,enclosingTaskCycle);
    });
    return lineageLost.load()?NativeTaskStamp{}:stamp;
}
std::uintptr_t observedCurrentTask()noexcept {
    if(!lifetime.admitted()||!passiveReloadTrace.enabled())return 0;
    std::uintptr_t task{};observeTasks([&]{task=address(BackgroundTaskBase::gCurrentTask());});return task;
}
struct PassiveUpdateContext {MinecraftGame* game{};std::uint64_t update{};bool normal{};};
thread_local PassiveUpdateContext passiveUpdate;
struct PassiveUpdateScope {
    PassiveUpdateContext previous;
    explicit PassiveUpdateScope(MinecraftGame& game)noexcept:previous(passiveUpdate){
        passiveUpdate={&game,0,false};
        try{
            auto update=passiveReloadTrace.update(GetTickCount64(),worldGeneration.load(),runtimeReady.load(),game.isInGame());
            passiveUpdate={&game,update,update&&!previous.game&&!observedCurrentTask()};
        }catch(...){passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);}
    }
    ~PassiveUpdateScope(){passiveUpdate=previous;}
};
ReloadTraceOwner passiveOwner(MinecraftGame& game,ResourcePackManager* expected=nullptr){
    // Query only in an admitted normal native reload/update context. The formal
    // non-owner getter is held just for this observation, never past origin.
    if(passiveUpdate.game!=&game||!passiveUpdate.normal||worldGeneration.load()||!runtimeReady.load()
        ||!passiveReloadTrace.view().titleSeen||game.isInGame()||game.isEditorModeEnabled())return {};
    auto& packs=game.getResourcePackManager();if(expected&&expected!=&packs)return {};
    auto loads=game.getResourceLoadManager();
    return {address(&game),address(&packs),address(loads.get()),0,true};
}
void samplePassiveTitle(MinecraftGame& game)noexcept{
    try{
        if(!passiveReloadTrace.enabled()||passiveUpdate.game!=&game||!passiveUpdate.normal
            ||worldGeneration.load()||game.isInGame()||passiveReloadTrace.view().titleSeen)return;
        thread_local std::uint64_t nextSample{};
        auto now=GetTickCount64();if(now<nextSample)return;nextSample=now+500;
        // Compare a fixed UI label; never output a screen name, telemetry,
        // player identity, resource path, pack identity or content.
        for(auto const& [id,client]:game.getClientInstanceMap())
            if(client&&!client->getLocalPlayer()&&client->getScreenName()=="start_screen"){
                passiveReloadTrace.title(address(&game),now);break;
            }
    }catch(...){passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);}
}
struct PassiveCallbackEntry {
    std::optional<TaskTicket> ticket;std::uintptr_t game{};std::uint64_t world{};
    explicit PassiveCallbackEntry(MinecraftGame* owner,ReloadTraceEvent event):game(address(owner)),world(worldGeneration.load()){
        if(!passiveReloadTrace.enabled()||!passiveReloadTrace.view().session)return;
        bool healthy{};
        observeTasks([&]{ticket=taskLineage.lookup(observedCurrentTask());
            healthy=ticket&&!lineageLost.load()&&!taskLineage.view(ticket->stamp).poisoned&&taskOutcomes.healthy();
        });
        if(ticket&&passiveReloadCallbackMatches(*ticket,&*ticket,game,world,observedCurrentTask(),healthy))
            passiveReloadTrace.event(event,ticket->stamp,GetTickCount64(),ticket->queue);
        else{ticket.reset();passiveReloadTrace.unknown(ReloadTraceEvent::CallbackUnknown,GetTickCount64());}
    }
    void returned(ReloadTraceEvent event,bool normal)noexcept{
        if(!ticket)return;
        std::optional<TaskTicket> current;bool healthy{};
        observeTasks([&]{current=taskLineage.lookup(ticket->task);
            healthy=!lineageLost.load()&&!taskLineage.view(ticket->stamp).poisoned&&taskOutcomes.healthy();
        });
        if(passiveReloadCallbackMatches(*ticket,current?&*current:nullptr,game,worldGeneration.load(),
            observedCurrentTask(),healthy))
            passiveReloadTrace.event(event,ticket->stamp,GetTickCount64(),ticket->queue,normal?1:0);
        else passiveReloadTrace.unknown(ReloadTraceEvent::CallbackUnknown,GetTickCount64());
    }
};
// Exact existing SDK hooks, observing the engine's own normal calls only.
LL_TYPE_INSTANCE_HOOK(UpdateHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::$update,void){
    NativeHookLifetime::Lease lease(lifetime);
    std::optional<PassiveUpdateScope> diagnostic;
    runPassiveReloadUpdate([&]{
        if(lease.admitted()&&passiveObserversInstalled.load()&&passiveReloadTrace.enabled())diagnostic.emplace(*this);
    },[&]{origin();},[&]{
        if(lease.admitted()&&passiveObserversInstalled.load()){
            if(diagnostic)samplePassiveTitle(*this);emitPassiveReloadTrace();
        }
    });
}
LL_TYPE_INSTANCE_HOOK(ActiveHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_onActiveResourcePacksChanged,void,
    ResourcePackManager& manager,bool fullVanilla){
    NativeHookLifetime::Lease lease(lifetime);if(!lease.admitted()){origin(manager,fullVanilla);return;}
    auto revision=nativeRevision.fetch_add(1)+1;
    std::optional<TaskCycleScope> taskScope;NativeTaskStamp diagnosticStamp;
    if(passiveObserversInstalled.load()&&passiveReloadTrace.enabled())observeTasks([&]{
        diagnosticStamp=passiveReloadTrace.start(passiveOwner(*this,&manager),revision,passiveUpdate.update,
            passiveUpdate.game==this&&passiveUpdate.normal,fullVanilla,GetTickCount64());
        if(diagnosticStamp.valid())taskScope.emplace(diagnosticStamp);
    });
    try{origin(manager,fullVanilla);}
    catch(...){passiveReloadTrace.event(ReloadTraceEvent::StartReturned,diagnosticStamp,GetTickCount64(),0,0);throw;}
    if(diagnosticStamp.valid())passiveReloadTrace.event(ReloadTraceEvent::StartReturned,diagnosticStamp,GetTickCount64(),0,1);
    if(diagnosticStamp.valid()&&worldGeneration.load())passiveReloadTrace.stop(ReloadTraceEnd::OwnerChanged);
}
LL_TYPE_INSTANCE_HOOK(FinishHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_finishReloadingResources,void,
    ResourcePackManager& manager,bool fullVanilla){
    NativeHookLifetime::Lease lease(lifetime);if(!lease.admitted()){origin(manager,fullVanilla);return;}
    auto diagnosticStamp=enclosingTaskCycle;
    bool diagnostic=passiveReloadTrace.enabled()&&fullVanilla&&diagnosticStamp.valid()
        &&diagnosticStamp.game==address(this)&&diagnosticStamp.manager==address(&manager)
        &&diagnosticStamp.world==worldGeneration.load()&&diagnosticStamp.preGameSession;
    if(diagnostic)passiveReloadTrace.event(ReloadTraceEvent::FinishEnter,diagnosticStamp,GetTickCount64());
    else if(passiveReloadTrace.enabled()&&passiveReloadTrace.view().session)
        passiveReloadTrace.unknown(ReloadTraceEvent::CallbackUnknown,GetTickCount64());
    try{origin(manager,fullVanilla);}
    catch(...){if(diagnostic)passiveReloadTrace.event(ReloadTraceEvent::FinishReturned,diagnosticStamp,GetTickCount64(),0,0);throw;}
    if(diagnostic)passiveReloadTrace.event(ReloadTraceEvent::FinishReturned,diagnosticStamp,GetTickCount64(),0,1);
    if(diagnostic&&worldGeneration.load())passiveReloadTrace.stop(ReloadTraceEnd::OwnerChanged);
}
LL_TYPE_INSTANCE_HOOK(TerrainHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_onTerrainAtlasReloaded,void){
    NativeHookLifetime::Lease lease(lifetime);if(!lease.admitted()){origin();return;}
    PassiveCallbackEntry diagnostic(this,ReloadTraceEvent::TerrainEnter);
    try{origin();}catch(...){diagnostic.returned(ReloadTraceEvent::TerrainReturned,false);throw;}
    diagnostic.returned(ReloadTraceEvent::TerrainReturned,true);
}
LL_TYPE_INSTANCE_HOOK(ItemHook,ll::memory::HookPriority::Normal,MinecraftGame,&MinecraftGame::_onItemAtlasReloaded,void){
    NativeHookLifetime::Lease lease(lifetime);if(!lease.admitted()){origin();return;}
    PassiveCallbackEntry diagnostic(this,ReloadTraceEvent::ItemEnter);
    try{origin();}catch(...){diagnostic.returned(ReloadTraceEvent::ItemReturned,false);throw;}
    diagnostic.returned(ReloadTraceEvent::ItemReturned,true);
}
using WorkerTask=BackgroundTask<TaskResult,void>;
using CallbackTask=BackgroundTask<void,void>;
template<class Result,class NativeTask,class Factory>
std::shared_ptr<NativeTask> observeFactory(gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,
    Bedrock::Threading::Async<void> predecessor,bool async,brstd::move_only_function<Result()> task,Factory&& factory){
    if(!passiveObserversInstalled.load()||!passiveReloadTrace.enabled())return factory(owner,info,std::move(predecessor),async,std::move(task));
    auto stamp=currentTaskStamp();
    if(!stamp.valid()||!task)return factory(owner,info,std::move(predecessor),async,std::move(task));
    using Body=ObservedNativeBody<brstd::move_only_function<Result()>>;
    std::shared_ptr<TaskBodyWitness> witness;std::shared_ptr<Body> state;
    brstd::move_only_function<Result()> wrapped;bool ready{};
    try{
        witness=std::make_shared<TaskBodyWitness>();
        state=std::make_shared<Body>(witness,std::move(task));
        wrapped=brstd::move_only_function<Result()>([state]() -> Result {
            return invokeObservedNativeBody(*state,observedCurrentTask(),
                [](auto const& result)noexcept{return result.mIsDone;});
        });
        ready=true;
    }catch(...){
        // Observer allocation failure must preserve the original move-only
        // callable and native behavior; never turn it into an empty callback.
        lineageLost=true;if(state)task=std::move(state->body);state.reset();
    }
    if(!ready)return factory(owner,info,std::move(predecessor),async,std::move(task));
    // Hand off before invoking native create: a synchronous native rejection or
    // cancellation must dispose the original captures immediately, without a
    // local observer shared_ptr extending them to the factory's return.
    state.reset();
    std::shared_ptr<NativeTask> native;
    try{native=factory(owner,info,std::move(predecessor),async,std::move(wrapped));}
    catch(...){witness->poison();lineageLost=true;throw;}
    auto identity=address(static_cast<BackgroundTaskBase*>(native.get()));witness->bind(identity);
    observeTasks([&]{if(!taskOutcomes.factory(identity,address(owner.get()),stamp,witness))lineageLost=true;});
    // The registry retains ONLY the CPU witness, never original captures.
    return native;
}
LL_TYPE_STATIC_HOOK(WorkerFactoryHook,ll::memory::HookPriority::Normal,WorkerTask,&WorkerTask::create,std::shared_ptr<WorkerTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<TaskResult()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return observeFactory<TaskResult,WorkerTask>(owner,info,std::move(predecessor),async,std::move(task),
        [](auto owner,auto const& info,auto predecessor,bool async,auto task){return origin(owner,info,std::move(predecessor),async,std::move(task));});
}
LL_TYPE_STATIC_HOOK(CallbackFactoryHook,ll::memory::HookPriority::Normal,CallbackTask,&CallbackTask::create,std::shared_ptr<CallbackTask>,
    gsl::not_null<IBackgroundTaskOwner*> owner,TaskStartInfo const& info,Bedrock::Threading::Async<void> predecessor,
    bool async,brstd::move_only_function<void()> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted())return origin(owner,info,std::move(predecessor),async,std::move(task));
    return observeFactory<void,CallbackTask>(owner,info,std::move(predecessor),async,std::move(task),
        [](auto owner,auto const& info,auto predecessor,bool async,auto task){return origin(owner,info,std::move(predecessor),async,std::move(task));});
}
// Exact MCAPI signatures; observe normal native calls only. No task injection,
// external callback execution, flush, cancel, or opaque task/manager reads.
LL_TYPE_INSTANCE_HOOK(TaskQueueHook,ll::memory::HookPriority::Normal,TaskGroup,&TaskGroup::_queueInternal,void,
    std::shared_ptr<BackgroundTaskBase> task){
    NativeHookLifetime::Lease lease(lifetime);
    if(!lease.admitted()||!passiveObserversInstalled.load()||!passiveReloadTrace.enabled())return origin(std::move(task));
    NativeTaskStamp stamp;if(lease.admitted())stamp=currentTaskStamp();
    if(lease.admitted())observeTasks([&]{
        if(stamp.valid()){
            auto ticket=taskLineage.enqueue(address(task.get()),address(this),stamp);
            if(!ticket.queue||!taskOutcomes.queued(ticket,address(static_cast<IBackgroundTaskOwner*>(this))))lineageLost=true;
            if(ticket.queue)passiveReloadTrace.event(ReloadTraceEvent::TaskQueued,ticket.stamp,GetTickCount64(),ticket.queue);
            if(lineageLost.load())passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);
        }
    });
    try{origin(std::move(task));}catch(...){
        if(lease.admitted()&&stamp.valid())observeTasks([&]{taskLineage.poison(stamp);});throw;
    }
}
LL_TYPE_INSTANCE_HOOK(TaskCompleteHook,ll::memory::HookPriority::Normal,TaskGroup,&TaskGroup::$taskComplete,void,
    gsl::not_null<BackgroundTaskBase*> task){
    NativeHookLifetime::Lease lease(lifetime);std::optional<TaskTicket> entry;std::shared_ptr<TaskBodyWitness> body;
    if(!lease.admitted()||!passiveObserversInstalled.load()||!passiveReloadTrace.enabled())return origin(task);
    if(lease.admitted())observeTasks([&]{
        entry=taskLineage.lookup(address(task.get()));
        if(entry)body=taskOutcomes.take(*entry,address(static_cast<IBackgroundTaskOwner*>(this)));
        if(!taskOutcomes.healthy())lineageLost=true;
        if(lineageLost.load())passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);
    });
    try{origin(task);}catch(...){
        if(entry)observeTasks([&]{taskLineage.poison(entry->stamp);});throw;
    }
    // The exact native body normally returning Done/void, its capture disposal
    // and this same queue ticket jointly classify a BODY terminal outcome.
    // Cancellation before execution, Requeue/cancel, error, missing or stale
    // evidence stay Unknown/Error. This does NOT prove native epilogue drain.
    // Never access task after origin, which may have destroyed its native owner.
    if(entry)observeTasks([&]{auto outcome=body&&!lineageLost.load()?body->classify(entry->task):TaskTerminal::Unknown;
        bool accepted=taskLineage.terminal(*entry,address(this),outcome);
        passiveReloadTrace.event(ReloadTraceEvent::TaskTerminal,entry->stamp,GetTickCount64(),entry->queue,
            static_cast<unsigned>(accepted?outcome:TaskTerminal::Unknown));
    });
}

// Observe the engine's own call. No call of isComplete, external flush/cancel,
// secondary-base conversion, private manager layout or native task dereference.
using PassiveFlushAbi=void(TaskGroup::*)(std::function<void()>);
static_assert(std::is_same_v<decltype(&TaskGroup::flush),PassiveFlushAbi>);
// Hook.h's public integer identifier accepts the already verified result of
// normal symbol resolution. This is not an RVA lookup or fallback.
LL_TYPE_INSTANCE_HOOK(TraceFlushHook,ll::memory::HookPriority::Normal,TaskGroup,
    reinterpret_cast<std::uintptr_t>(passiveReloadFlushTarget()),void,
    std::function<void()> wait){
    NativeHookLifetime::Lease lease(lifetime);ReloadFlushTicket ticket;
    // Capture in the actual detour frame, not in the observation lambda.
    auto returnSite=_ReturnAddress();
    if(lease.admitted()&&passiveObserversInstalled.load()&&passiveReloadTrace.enabled())observeTasks([&]{
        bool caller=passiveReloadFlushCaller(returnSite);
        bool executing=observedCurrentTask()!=0;
        bool context=passiveUpdate.game&&passiveUpdate.normal&&!executing;
        auto owner=caller&&context?passiveOwner(*passiveUpdate.game):ReloadTraceOwner{};
        ticket=passiveReloadTrace.flushBegin(owner,passiveUpdate.update,caller,context,executing,
            GetTickCount64(),address(this));
    });
    try{origin(std::move(wait));}
    catch(...){passiveReloadTrace.flushEnd(ticket,false,GetTickCount64());throw;}
    passiveReloadTrace.flushEnd(ticket,true,GetTickCount64());
}

#endif
} // namespace

void installPassiveReloadObserver() noexcept {
    // Count before the stop check: concurrent stop must retain owners while an
    // installer is in flight. No install lock is held across native hook calls.
    installers.fetch_add(1,std::memory_order_seq_cst);
    struct Exit {~Exit(){installers.fetch_sub(1,std::memory_order_seq_cst);}} exit;
    if(observerStopping.load(std::memory_order_seq_cst)||installAttempted.exchange(true)||!preparePassiveReloadTrace())return;
    try{
        if(!lifetime.prepare(pin)){reportPassiveReloadRegistration(3,"module_pin_failed");stopPassiveReloadTrace(ReloadTraceEnd::ObservationError);return;}
        reportPassiveReloadRegistration(1,"registering");
        lifetime.registrationStarted();
        bool hooks=UpdateHook::hook()==0&&ActiveHook::hook()==0&&FinishHook::hook()==0
            &&TerrainHook::hook()==0&&ItemHook::hook()==0&&WorkerFactoryHook::hook()==0
            &&CallbackFactoryHook::hook()==0&&TaskQueueHook::hook()==0
            &&TaskCompleteHook::hook()==0&&TraceFlushHook::hook()==0;
        if(hooks&&lifetime.open()){
            passiveObserversInstalled=true;reportPassiveReloadRegistration(2,"registered");
        }else{reportPassiveReloadRegistration(3,"hook_registration_failed");stopPassiveReloadTrace(ReloadTraceEnd::ObservationError);}
    }catch(...){lifetime.close();reportPassiveReloadRegistration(3,"registration_exception");stopPassiveReloadTrace(ReloadTraceEnd::ObservationError);}
}
bool stopPassiveReloadObserver() noexcept {
    observerStopping.store(true,std::memory_order_seq_cst);
    lifetime.close();
    stopPassiveReloadTrace();
    return lifetime.readers()==0&&installers.load(std::memory_order_seq_cst)==0;
}
} // namespace praxis::companion::packs
