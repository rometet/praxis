#pragma once
#include "resourcepacks/TitlePackControl.h"
#include "core/NativeHookLifetime.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <utility>

namespace praxis::companion::packs {
// Immutable CPU provenance. A seed comes from a claimed setter; an engine cycle
// is assigned only at its corresponding native Active entry. Finish must carry
// that same cycle through the actual native callable, never infer it from active.
struct TitleNativeContext {
    std::uint64_t token{}, attempt{}, cycle{};
    TitlePackOwner owner;
    bool fullVanilla{};
    bool seed() const noexcept {return token&&attempt&&owner.game&&owner.manager&&owner.client&&owner.thread;}
    bool receipt() const noexcept {return seed()&&cycle;}
    bool same(TitleNativeContext const& b) const noexcept {
        return receipt()&&b.receipt()&&token==b.token&&attempt==b.attempt&&cycle==b.cycle
            &&owner.same(b.owner)&&fullVanilla==b.fullVanilla;
    }
};
inline thread_local TitleNativeContext titleNativeContext;
class TitleNativeScope {
    TitleNativeContext previous;
public:
    explicit TitleNativeScope(TitleNativeContext value):previous(titleNativeContext){titleNativeContext=value;}
    ~TitleNativeScope(){titleNativeContext=previous;}
    TitleNativeScope(TitleNativeScope const&)=delete;
};
class TitleCallbackLease {
    NativeHookLifetime::Lease lease;
    std::atomic<unsigned>& callbacks;
    bool counted;
public:
    TitleCallbackLease(NativeHookLifetime& lifetime,std::atomic<unsigned>& count):lease(lifetime),callbacks(count),counted(lease.admitted()){
        if(counted)callbacks.fetch_add(1,std::memory_order_seq_cst);
    }
    ~TitleCallbackLease(){if(counted)callbacks.fetch_sub(1,std::memory_order_seq_cst);}
    bool admitted() const noexcept {return lease.admitted();}
};
struct TitleSetterResult {bool accepted{}, unchangedOld{};};

// This exact adapter drives production Apply and the cancellation/stop fixtures.
// No locks are held across native getters/setter. Permission is claimed only
// after validation, with cancellation and close serialized at the final gate.
template<class Context,class Validate,class Setter>
void runTitlePackApplyCallback(NativeHookLifetime& lifetime,std::atomic<unsigned>& callbacks,
    TitlePackControl& control,std::mutex& mutex,TitlePackTransaction& transaction,
    Context&& context,Validate&& validate,Setter&& setter,std::uint64_t now) noexcept {
    TitleCallbackLease lease(lifetime,callbacks);if(!lease.admitted())return;
    try{
        std::uint64_t token;
        {std::lock_guard lock(mutex);token=transaction.token;}
        if(control.cancelled(token)||control.stopped()){
            std::lock_guard lock(mutex);transaction.cancel();return;
        }
        auto owner=std::forward<Context>(context)();
        TitlePackAction action;
        {std::lock_guard lock(mutex);action=transaction.callback(owner,now);}
        if(action==TitlePackAction::None)return;
        if(!std::forward<Validate>(validate)(action)){
            std::lock_guard lock(mutex);transaction.recovery();return;
        }
        TitleNativeContext seed;
        {std::lock_guard lock(mutex);
            if(transaction.phase!=TitlePackPhase::Reloading&&transaction.phase!=TitlePackPhase::RollingBack)return;
            if(!control.claimSetter(token,transaction.attempt)){
                if(action==TitlePackAction::Apply&&!transaction.started)transaction.reject();else transaction.recovery();return;
            }
            seed={token,transaction.attempt,0,transaction.owner,false};
        }
        TitleNativeScope scope(seed);
        auto result=std::forward<Setter>(setter)(action);
        std::lock_guard lock(mutex);transaction.setterResult(result.accepted,result.unchangedOld);
    }catch(...){std::lock_guard lock(mutex);transaction.recovery();}
}

inline TitleNativeContext beginTitleNativeCycle(TitlePackTransaction& transaction,
    TitleNativeContext const& inherited,TitlePackOwner const& actual,bool fullVanilla,std::uint64_t cycle){
    if(!transaction.busy())return {};
    if(!inherited.seed()||inherited.cycle||!cycle||inherited.token!=transaction.token
        ||inherited.attempt!=transaction.attempt||!transaction.owner.same(inherited.owner)
        ||!inherited.owner.same(actual)||!actual.fresh){transaction.recovery();return {};}
    if(!transaction.nativeStart(actual,inherited.attempt))return {};
    return {inherited.token,inherited.attempt,cycle,inherited.owner,fullVanilla};
}
inline bool titleNativeFinishMatches(TitlePackTransaction const& transaction,
    TitleNativeContext const& started,TitleNativeContext const& inherited,
    std::uintptr_t game,std::uintptr_t manager,std::uint64_t world,bool fullVanilla) noexcept {
    return (transaction.phase==TitlePackPhase::Reloading||transaction.phase==TitlePackPhase::RollingBack)
        &&transaction.started&&!transaction.finished&&started.same(inherited)
        &&inherited.token==transaction.token&&inherited.attempt==transaction.attempt
        &&inherited.owner.game==game&&inherited.owner.manager==manager&&inherited.owner.world==world
        &&inherited.fullVanilla==fullVanilla;
}
// Used by the real Finish detour and tests; origin always executes exactly once.
template<class Origin>
void observeTitleNativeFinish(std::mutex& mutex,TitlePackTransaction& transaction,
    TitleNativeContext const& started,TitleNativeContext const& inherited,
    std::uintptr_t game,std::uintptr_t manager,std::uint64_t world,bool fullVanilla,Origin&& origin){
    bool matches;
    {std::lock_guard lock(mutex);matches=titleNativeFinishMatches(transaction,started,inherited,game,manager,world,fullVanilla);
        if(!matches&&transaction.busy())transaction.recovery();}
    try{std::forward<Origin>(origin)();}
    catch(...){if(matches){std::lock_guard lock(mutex);transaction.nativeFinish(inherited.attempt,false);}throw;}
    if(matches){std::lock_guard lock(mutex);
        if(titleNativeFinishMatches(transaction,started,inherited,game,manager,world,fullVanilla))transaction.nativeFinish(inherited.attempt,true);
        else if(transaction.busy())transaction.recovery();}
}
// Only context captured from the native factory is propagated. This helper is
// used by both real SDK factory wrappers and asynchronous fault fixtures.
template<class Body>
decltype(auto) invokeTitleNativeBody(TitleNativeContext context,NativeHookLifetime& lifetime,
    std::atomic<unsigned>& callbacks,Body&& body){
    TitleCallbackLease lease(lifetime,callbacks);TitleNativeScope scope(context);
    return std::forward<Body>(body)();
}
template<class Callable> struct TitleNativeCallable {
    Callable body;
    explicit TitleNativeCallable(Callable&& value):body(std::move(value)){}
};
struct TitleNativeAllocation {
    template<class Callable> auto operator()(Callable&& body) const {
        return std::make_shared<TitleNativeCallable<Callable>>(std::move(body));
    }
};
// Allocation failure falls back to the intact native callable. The local body
// owner is dropped before native create, preserving native rejection/disposal.
template<class Result,class Callable,class Factory,class Allocation=TitleNativeAllocation>
decltype(auto) wrapTitleNativeCallable(TitleNativeContext context,NativeHookLifetime& lifetime,
    std::atomic<unsigned>& callbacks,Callable task,Factory&& factory,bool& lost,Allocation allocate={}){
    if(!context.seed()||!task)return std::forward<Factory>(factory)(std::move(task));
    std::shared_ptr<TitleNativeCallable<Callable>> body;Callable wrapped;
    try{
        body=allocate(std::move(task));
        wrapped=Callable([context,body,&lifetime,&callbacks]() mutable -> Result {
            return invokeTitleNativeBody(context,lifetime,callbacks,[&]() -> Result {return body->body();});
        });
    }catch(...){
        lost=true;if(body)task=std::move(body->body);body.reset();
        return std::forward<Factory>(factory)(std::move(task));
    }
    body.reset();return std::forward<Factory>(factory)(std::move(wrapped));
}
}
