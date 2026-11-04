#pragma once
#include "resourcepacks/WorldPackTransaction.h"
#include "resourcepacks/TitlePackNativeAdapter.h"
#include "resourcepacks/WorldNativeTaskReceipt.h"
#include <type_traits>

namespace praxis::companion::packs {
struct WorldNativeContext {
    std::uint64_t token{},attempt{},cycle{};
    WorldPackOwner owner;
    bool fullVanilla{};
    // producer is fixed at native Active; task/invocation are set only while
    // executing an owned callable. A child factory inherits this TLS lineage.
    enum class BodyPhase { None, Finalize, Level26, Tess11 };
    std::uint64_t producer{},finishProducer{},task{},invocation{},parentInvocation{},finalizeInvocation{};
    BodyPhase bodyPhase{BodyPhase::None};
    bool seed() const noexcept {return token&&attempt&&owner.game&&owner.manager&&owner.client&&owner.renderer&&owner.world&&owner.thread;}
    bool receipt() const noexcept {return seed()&&cycle;}
    bool same(WorldNativeContext const& b) const noexcept {
        return receipt()&&b.receipt()&&token==b.token&&attempt==b.attempt&&cycle==b.cycle&&owner.same(b.owner)&&fullVanilla==b.fullVanilla
            &&producer==b.producer;
    }
    WorldNativeProducer source() const noexcept {return {token,attempt,cycle,producer,finishProducer};}
    WorldNativeInvocation currentInvocation() const noexcept {return {source(),task,invocation,parentInvocation};}
};
inline thread_local WorldNativeContext worldNativeContext;
struct WorldNativeScope {
    WorldNativeContext previous;
    explicit WorldNativeScope(WorldNativeContext value):previous(worldNativeContext){worldNativeContext=value;}
    ~WorldNativeScope(){worldNativeContext=previous;}
};
// Tokens and attempts advance monotonically. Only an explicitly earlier
// nonzero identity is stale; unknown/future identities retain fail-closed checks.
// A callable retained by an earlier transaction still executes its native
// origin, but it cannot
// quarantine or complete the currently active transaction.
inline bool worldNativeStaleAttempt(WorldPackTransaction const& tx,WorldNativeContext const& context) noexcept {
    return context.token&&context.attempt&&tx.token
        &&(context.token<tx.token||(tx.attempt&&context.token==tx.token&&context.attempt<tx.attempt));
}
// Observation belongs only to the admitted current cycle. Native origin and
// the factories it creates retain incoming lineage when observation is skipped.
struct WorldNativeActiveInvocation {
    WorldNativeContext observed,origin;
    explicit WorldNativeActiveInvocation(WorldNativeContext inherited):origin(inherited){}
};
template<class Observe>
WorldNativeActiveInvocation prepareWorldActiveInvocation(WorldPackTransaction const& tx,
    WorldNativeContext inherited,Observe&& observe){
    WorldNativeActiveInvocation invocation(inherited);
    if(!tx.busy()||worldNativeStaleAttempt(tx,inherited))return invocation;
    invocation.observed=std::forward<Observe>(observe)();
    if(invocation.observed.receipt())invocation.origin=invocation.observed;
    return invocation;
}
template<class Origin>
decltype(auto) runWorldActiveOrigin(WorldNativeActiveInvocation const& invocation,Origin&& origin){
    WorldNativeScope scope(invocation.origin);
    return std::forward<Origin>(origin)();
}
inline WorldNativeContext beginWorldCycle(WorldPackTransaction& tx,WorldNativeContext inherited,
    WorldPackOwner const& actual,bool full,std::uint64_t cycle) noexcept {
    if(tx.phase==WorldSwitchPhase::Idle||worldNativeStaleAttempt(tx,inherited))return {};
    if(!inherited.seed()||inherited.cycle||!cycle||inherited.token!=tx.token||inherited.attempt!=tx.attempt
        ||!inherited.owner.same(tx.owner)||!tx.nativeStart(actual,inherited.attempt,full)){tx.seal();return {};}
    WorldNativeContext started{inherited.token,inherited.attempt,cycle,inherited.owner,full};
    started.producer=cycle;
    return started;
}
inline bool worldFinishMatches(WorldPackTransaction const& tx,WorldNativeContext const& started,
    WorldNativeContext const& inherited,std::uintptr_t game,std::uintptr_t manager,std::uint64_t world,bool full) noexcept {
    return tx.phase==WorldSwitchPhase::Reloading&&tx.started&&!tx.finished&&started.same(inherited)
        &&inherited.token==tx.token&&inherited.attempt==tx.attempt&&inherited.owner.game==game
        &&inherited.owner.manager==manager&&inherited.owner.world==world&&inherited.fullVanilla==full;
}
template<class Origin> void observeWorldFinish(std::mutex& mutex,WorldPackTransaction& tx,
    WorldNativeContext const& started,WorldNativeContext inherited,std::uintptr_t game,std::uintptr_t manager,
    std::uint64_t world,bool full,Origin&& origin){
    bool matches;
    {std::lock_guard lock(mutex);matches=worldFinishMatches(tx,started,inherited,game,manager,world,full);if(!matches&&tx.busy()&&!worldNativeStaleAttempt(tx,inherited))tx.seal();}
    try{std::forward<Origin>(origin)();}catch(...){if(matches){std::lock_guard lock(mutex);tx.finish(inherited.attempt,false);}throw;}
    if(matches){std::lock_guard lock(mutex);if(worldFinishMatches(tx,started,inherited,game,manager,world,full))tx.finish(inherited.attempt,true);else if(tx.busy()&&!worldNativeStaleAttempt(tx,inherited))tx.seal();}
}
// Same wrapper ownership/fallback algorithm as the reviewed common adapter;
// world provenance uses a separate TLS scope so the title observer cannot erase it.
struct WorldQueuedTask {
    std::atomic<unsigned>& count;
    std::atomic_bool released{};
    explicit WorldQueuedTask(std::atomic<unsigned>& value):count(value){count.fetch_add(1);}
    void complete() noexcept {if(!released.exchange(true))count.fetch_sub(1);}
    ~WorldQueuedTask(){complete();}
};
template<class Result,class Callable,class Factory,class Allocation=TitleNativeAllocation>
decltype(auto) wrapWorldCallable(WorldNativeContext context,NativeHookLifetime& lifetime,std::atomic<unsigned>& callbacks,
    Callable task,Factory&& factory,bool& lost,Allocation allocate={},std::atomic<unsigned>* queued=nullptr,
    std::shared_ptr<WorldNativeTaskReceipt>* receiptOut=nullptr){
    if(!context.seed()||!task)return std::forward<Factory>(factory)(std::move(task));
    std::shared_ptr<TitleNativeCallable<Callable>> body;Callable wrapped;
    std::shared_ptr<WorldQueuedTask> ticket;
    std::shared_ptr<WorldNativeTrackedTask> tracked;
    try{
        body=allocate(std::move(task));
        if(queued)ticket=std::make_shared<WorldQueuedTask>(*queued);
        if(context.receipt()&&context.producer==context.cycle&&context.finishProducer)
            tracked=std::make_shared<WorldNativeTrackedTask>(std::make_shared<WorldNativeTaskReceipt>(
                context.source(),context.invocation));
        wrapped=Callable([context,body,ticket,tracked,&lifetime,&callbacks]() mutable -> Result {
            struct Done {std::shared_ptr<WorldQueuedTask> ticket;~Done(){if(ticket)ticket->complete();}} done{ticket};
            TitleCallbackLease lease(lifetime,callbacks);
            auto active=context;
            if(tracked){
                auto event=tracked->receipt->enter();
                active.task=event.task;active.invocation=event.invocation;active.parentInvocation=event.parentInvocation;
            }
            WorldNativeScope scope(active);
            try{
                if constexpr(std::is_void_v<Result>){body->body();if(tracked)tracked->receipt->returned();}
                else {auto result=body->body();if(tracked)tracked->receipt->returned();return result;}
            }catch(...){if(tracked)tracked->receipt->threw();throw;}
        });
    }catch(...){lost=true;if(body)task=std::move(body->body);body.reset();ticket.reset();tracked.reset();return std::forward<Factory>(factory)(std::move(task));}
    if(receiptOut&&tracked)*receiptOut=tracked->receipt;
    body.reset();ticket.reset();return std::forward<Factory>(factory)(std::move(wrapped));
}
template<class Context,class Validate,class PrepareNative,class Setter>
void runWorldApply(NativeHookLifetime& lifetime,std::atomic<unsigned>& callbacks,TitlePackControl& control,
    std::mutex& mutex,WorldPackTransaction& tx,std::uint64_t peerToken,Context&& context,Validate&& validate,PrepareNative&& prepareNative,
    Setter&& setter,std::uint64_t now) noexcept {
    TitleCallbackLease lease(lifetime,callbacks);if(!lease.admitted())return;
    try{
        std::uint64_t token;{std::lock_guard lock(mutex);
            if(tx.participant.token!=peerToken||tx.phase!=WorldSwitchPhase::AwaitApply)return;token=tx.token;}
        if(control.cancelled(token)||control.stopped()){std::lock_guard lock(mutex);tx.cancel();return;}
        auto owner=std::forward<Context>(context)();
        {std::lock_guard lock(mutex);if(!tx.callback(owner,now))return;}
        if(!std::forward<Validate>(validate)()){std::lock_guard lock(mutex);tx.seal();return;}
        WorldNativeContext seed;
        {std::lock_guard lock(mutex);
            if(tx.phase!=WorldSwitchPhase::Reloading)return;
            if(!control.claimSetter(token,tx.attempt)){tx.phase=WorldSwitchPhase::Cancelled;return;}
            seed={token,tx.attempt,0,tx.owner,false};
        }
        if(!std::forward<PrepareNative>(prepareNative)()){std::lock_guard lock(mutex);tx.seal();return;}
        WorldNativeScope scope(seed);auto result=std::forward<Setter>(setter)();
        std::lock_guard lock(mutex);if(tx.phase!=WorldSwitchPhase::Reloading)return;
        tx.changed=result.accepted||!result.unchangedOld;tx.setterRejected=!result.accepted;
        // A rejected, untouched setter without a native cycle may cancel the
        // participant transaction before native work. Never fabricate finish.
        if(!result.accepted&&result.unchangedOld&&!tx.started)tx.phase=WorldSwitchPhase::Cancelled;
    }catch(...){std::lock_guard lock(mutex);tx.seal();}
}
}
