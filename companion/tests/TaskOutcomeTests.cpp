#include "resourcepacks/TaskOutcome.h"
#include "mc/platform/brstd/move_only_function.h"
#include "mc/deps/core/threading/TaskResult.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace praxis::companion::packs;
static unsigned checks{}, failures{};
void check(bool value) { ++checks; if (!value) ++failures; }
NativeTaskStamp stamp(std::uint64_t serial) { return {11,22,33,serial,{44,11,22,55,33,serial,serial}}; }
struct Result { bool done; std::uint64_t deadline, identity; };
using Function = brstd::move_only_function<Result()>;
using Body = ObservedNativeBody<Function>;
constexpr auto finished = [](Result const& result) noexcept { return result.done; };
struct Capture {
    std::shared_ptr<TaskBodyWitness> witness;
    bool& destroyed;
    bool& releaseOrdered;
    Capture(std::shared_ptr<TaskBodyWitness> value,bool& destroyed,bool& ordered)
        :witness(std::move(value)),destroyed(destroyed),releaseOrdered(ordered) {}
    ~Capture() { destroyed=true; releaseOrdered=witness->classify(101)==TaskTerminal::Unknown; }
};
int main() {
    {
        // Use the exact formal SDK result type; this executes no MCAPI symbol.
        static_assert(sizeof(TaskResult)==16);
        static_assert(std::is_same_v<decltype(TaskResult::mIsDone),bool>);
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        TaskResult original{}; original.mIsDone=true;
        using NativeBody=ObservedNativeBody<brstd::move_only_function<TaskResult()>>;
        auto body=std::make_shared<NativeBody>(witness,brstd::move_only_function<TaskResult()>([original]{return original;}));
        auto result=invokeObservedNativeBody(*body,101,[](TaskResult const& value)noexcept{return value.mIsDone;});
        check(result.mIsDone&&witness->classify(101)==TaskTerminal::Unknown);
        body.reset(); check(witness->classify(101)==TaskTerminal::Success);
    }
    {
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        bool destroyed{}, ordered{};
        auto capture=std::make_unique<Capture>(witness,destroyed,ordered);
        auto body=std::make_shared<Body>(witness,Function([capture=std::move(capture)]{return Result{true,912345,77};}));
        auto result=invokeObservedNativeBody(*body,101,finished);
        check(result.done&&result.deadline==912345&&result.identity==77);
        check(!destroyed&&witness->classify(101)==TaskTerminal::Unknown);
        body.reset(); check(destroyed&&ordered); check(witness->classify(101)==TaskTerminal::Success);
        // The CPU witness survives; it does not retain the original capture.
        check(witness.use_count()==1);
    }
    {
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        unsigned runs{};
        auto body=std::make_shared<Body>(witness,Function([&]{++runs;return Result{runs==1001,7000+runs,runs};}));
        for(unsigned i=1;i<=1001;++i) {
            auto result=invokeObservedNativeBody(*body,101,finished);
            check(result.done==(i==1001)&&result.deadline==7000+i&&result.identity==i);
            check(witness->classify(101)==TaskTerminal::Unknown);
        }
        body.reset(); check(runs==1001&&witness->classify(101)==TaskTerminal::Success);
    }
    {
        // Requeue followed by native cancellation must not become Success.
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        auto body=std::make_shared<Body>(witness,Function([]{return Result{false,99,88};}));
        invokeObservedNativeBody(*body,101,finished); body.reset();
        check(witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        // Cancellation / wrong-execution-context Error before the body ran.
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        bool ran{}; auto body=std::make_shared<Body>(witness,Function([&]{ran=true;return Result{true,0,0};}));
        body.reset(); check(!ran&&witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        struct OriginalException { int identity; };
        auto body=std::make_shared<Body>(witness,Function([]()->Result{throw OriginalException{713};}));
        bool exact{}; try { invokeObservedNativeBody(*body,101,finished); }
        catch(OriginalException const& error) { exact=error.identity==713; }
        body.reset(); check(exact); check(witness->classify(101)==TaskTerminal::Error);
    }
    {
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101); unsigned ran{};
        using VoidBody=ObservedNativeBody<brstd::move_only_function<void()>>;
        auto body=std::make_shared<VoidBody>(witness,brstd::move_only_function<void()>([&]{++ran;}));
        invokeObservedNativeBody(*body,101,finished); check(ran==1); check(witness->classify(101)==TaskTerminal::Unknown);
        body.reset(); check(witness->classify(101)==TaskTerminal::Success);
    }
    for(auto task:{std::uintptr_t{},std::uintptr_t{102}}) {
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101); unsigned ran{};
        auto body=std::make_shared<Body>(witness,Function([&]{++ran;return Result{true,0,0};}));
        invokeObservedNativeBody(*body,task,finished); body.reset();
        check(ran==1&&witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        // Factory reentrancy before the returned task identity is bound.
        auto witness=std::make_shared<TaskBodyWitness>();
        auto body=std::make_shared<Body>(witness,Function([]{return Result{true,0,0};}));
        invokeObservedNativeBody(*body,101,finished); witness->bind(101); body.reset();
        check(witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        // Observer setup failure recovery keeps a moved callable executable.
        auto witness=std::make_shared<TaskBodyWitness>(); bool destroyed{}, ordered{};
        auto capture=std::make_unique<Capture>(witness,destroyed,ordered);
        auto body=std::make_shared<Body>(witness,Function([capture=std::move(capture)]{return Result{true,33,44};}));
        Function recovered=std::move(body->body); body.reset(); check(!destroyed);
        auto result=recovered(); check(result.done&&result.deadline==33&&result.identity==44);
        recovered=nullptr; check(destroyed); check(witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        // A native factory may synchronously dispose a callback before it
        // returns. The observer's setup owner must already have been released.
        auto witness=std::make_shared<TaskBodyWitness>(); bool destroyed{}, ordered{};
        auto capture=std::make_unique<Capture>(witness,destroyed,ordered);
        auto state=std::make_shared<Body>(witness,Function([capture=std::move(capture)]{return Result{true,0,0};}));
        std::weak_ptr<Body> nativeBody=state;
        Function wrapped([state]{return invokeObservedNativeBody(*state,101,finished);});
        state.reset(); check(!destroyed&&!nativeBody.expired());
        auto nativeFactory=[&](Function incoming){
            incoming=nullptr;
            check(destroyed&&ordered&&nativeBody.expired());
            check(witness.use_count()==1&&witness->classify(101)==TaskTerminal::Unknown);
        };
        nativeFactory(std::move(wrapped)); check(destroyed&&nativeBody.expired());
    }
    for(unsigned bad=0;bad<4;++bad) {
        TaskBodyWitness witness; witness.bind(101); witness.enter(101);
        if(bad==0)witness.enter(101); if(bad==1)witness.bind(101);
        if(bad==2)witness.bodyReleased(); if(bad==3)witness.poison();
        witness.normalReturn(true); witness.bodyReleased(); check(witness.classify(101)==TaskTerminal::Unknown);
    }
    {
        TaskBodyWitness witness; witness.bind(101); witness.enter(101);
        std::atomic_bool ready{}, proceed{};
        std::thread other([&]{ready=true;while(!proceed.load())std::this_thread::yield();witness.normalReturn(true);witness.bodyReleased();});
        while(!ready.load())std::this_thread::yield(); check(witness.classify(101)==TaskTerminal::Unknown);
        proceed=true; other.join(); check(witness.classify(101)==TaskTerminal::Success);
    }
    {
        TaskOutcomeRegistry registry; auto s=stamp(1); TaskTicket ticket{101,201,1,s};
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        check(registry.factory(101,201,s,witness)); check(registry.queued(ticket,201));
        auto found=registry.take(ticket,201); check(found==witness); check(!registry.take(ticket,201));
        check(registry.healthy());
        // Same native address, different queue/cycle never inherits old state.
        auto next=stamp(2); auto fresh=std::make_shared<TaskBodyWitness>(); fresh->bind(101);
        check(registry.factory(101,201,next,fresh)); check(registry.queued({101,201,2,next},201));
        check(!registry.take(ticket,201)); check(!registry.healthy()); check(fresh->classify(101)==TaskTerminal::Unknown);
    }
    for(unsigned bad=0;bad<4;++bad) {
        TaskOutcomeRegistry registry; auto s=stamp(1); TaskTicket ticket{101,201,1,s};
        auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101); witness->enter(101); witness->normalReturn(true); witness->bodyReleased();
        check(registry.factory(101,201,s,witness)); check(registry.queued(ticket,201));
        auto wrong=ticket; if(bad==0)wrong.queue++; if(bad==1)wrong.stamp.world++;
        if(bad==2)wrong.stamp.transaction.operation++; if(bad==3)wrong.stamp.serial++;
        check(!registry.take(wrong,201)); check(!registry.healthy()); check(witness->classify(101)==TaskTerminal::Unknown);
    }
    {
        TaskOutcomeRegistry registry; auto s=stamp(1); auto witness=std::make_shared<TaskBodyWitness>(); witness->bind(101);
        check(registry.factory(101,201,s,witness)); check(!registry.queued({101,201,1,s},202)); check(!registry.healthy());
    }
    {
        TaskOutcomeRegistry registry; auto s=stamp(1); TaskTicket other{101,201,1,s};
        check(registry.queued(other,201)); check(!registry.take(other,201)); check(registry.healthy());
    }
    {
        TaskOutcomeRegistry registry; auto s=stamp(1);
        for(unsigned i=1;i<=4096;++i)check(registry.factory(i,201,s,std::make_shared<TaskBodyWitness>()));
        check(!registry.factory(99999,201,s,std::make_shared<TaskBodyWitness>())); check(!registry.healthy());
    }
    // BODY Success and synthetic CPU lineage still cannot authorize Apply.
    check(SupportedNativeSerialization==NativeSerialization::Unverified);
    std::cout<<"Task outcome: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
