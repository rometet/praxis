#include "resourcepacks/WorldPackReloadState.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace lholo::resourcepacks;
namespace {
unsigned n{};
void check(bool passed,char const* label) {
    ++n;
    if(!passed){std::fprintf(stderr,"R30 Glass R5 real native State failed: %s\n",label);std::exit(1);}
}
WorldPackReloadRequest request() {
    WorldPackReloadRequest r{};
    r.token=44;r.world=5;r.client=222;r.renderer=333;r.oldEpoch=1;r.nextEpoch=2;
    return r;
}
bool cmd(State& s,WorldPackReloadCommand action,WorldPackReloadRequest const& r,WorldPackReloadReply& reply){
    reply={};return s.call(action,&r,&reply)==1;
}
void init(State& s,std::shared_ptr<void> module){
    check(s.enable(module),"real GlassR5 State initialized");
    s.observe(Owner{111,222,333,444});
}
}
int main() {
    auto guard=std::make_shared<int>(123);
    auto r=request();
    {
        // R29 real observed bug: SetGlobalStack starts a native mutation
        // while the LHolo participant is only in Prepared -> error=10.
        State old;
        init(old,guard);
        WorldPackReloadReply q{};
        check(cmd(old,WorldPackReloadCommand::Pause,r,q),"pause old");
        check(cmd(old,WorldPackReloadCommand::Prepare,r,q),"prepare old");
        auto lease=old.beginMutation([]{});
        auto snapshot=old.snapshot();
        check(snapshot->reply.phase==WorldPackReloadPhase::Invalidated,
              "late prearm is invalidated by native Global setter");
        check(snapshot->reply.error==static_cast<unsigned>(Error::Invalidated),
              "observed R29 error 10 reproduced");
        old.finishMutation(lease,[]{});
    }
    {
        // R30 change: before _setResourcePackStack (which calls the
        // native _setGlobalStack) both real peers already hold NativeActive.
        State newState;
        init(newState,guard);
        WorldPackReloadReply q{};
        check(cmd(newState,WorldPackReloadCommand::Pause,r,q),"pause new");
        check(cmd(newState,WorldPackReloadCommand::Prepare,r,q),"prepare new");
        check(cmd(newState,WorldPackReloadCommand::BeginNative,r,q),
              "BeginNative BEFORE native setting setter");
        check(cmd(newState,WorldPackReloadCommand::Query,r,q),
              "query before vanilla setter");
        check(q.phase==WorldPackReloadPhase::NativeActive&&q.error==0,
              "prearm transitions into NativeActive");
        {
            auto first=newState.beginMutation([]{});
            auto live=newState.snapshot();
            check(live->reply.phase==WorldPackReloadPhase::NativeActive,
                  "native setter begins in allowed NativeActive");
            check(live->reply.cpuReaders>0&&live->reply.nativeBorrowers>0,
                  "real owner retains native borrowed readers during setter");
            check(cmd(newState,WorldPackReloadCommand::Query,r,q),
                  "native callback Query during setter");
            check(q.phase==WorldPackReloadPhase::NativeActive&&q.error==0,
                  "native callback sees active rather than Prepared");
            newState.finishMutation(first,[]{});
        }
        auto post=newState.snapshot();
        check(post->reply.phase==WorldPackReloadPhase::NativeActive&&post->reply.error==0,
              "after vanilla setter, LHolo still valid");
        check(post->reply.cpuReaders==0&&post->reply.nativeBorrowers==0,
              "vanilla setter returns its mutation leases");
        // Engine then consumes the existing NativeActive receipt rather
        // than calling BeginNative a second time.
        check(cmd(newState,WorldPackReloadCommand::Query,r,q),
              "engine receives live NativeActive after setter");
        check(q.phase==WorldPackReloadPhase::NativeActive&&q.error==0,
              "engine native receipt stays valid");
    }
    std::printf("R30 merged Glass R5 State vanilla prearm PASS %u checks\n",n);
}
