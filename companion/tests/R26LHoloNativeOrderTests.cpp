#include "resourcepacks/WorldPackReloadState.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace lholo::resourcepacks;
namespace {
unsigned checks{};
void check(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"R26 LHolo test failed: %s\n",label);std::exit(1);}}
WorldPackReloadRequest request(){
    WorldPackReloadRequest r;r.token=7;r.world=5;r.client=222;r.renderer=333;r.oldEpoch=1;r.nextEpoch=2;return r;
}
bool call(State& state,WorldPackReloadCommand cmd,WorldPackReloadRequest const& r,WorldPackReloadReply& p){
    p={};return state.call(cmd,&r,&p)==1;
}
void setup(State& state,std::shared_ptr<void> const& module){
    check(state.enable(module),"LHolo state enable");
    state.observe(Owner{111,222,333,444});
}
}
int main(){
    auto held=std::make_shared<int>(3);
    auto r=request();
    {
        State late;
        setup(late,held);
        WorldPackReloadReply q{};
        check(call(late,WorldPackReloadCommand::Pause,r,q),"LHolo pause");
        check(call(late,WorldPackReloadCommand::Prepare,r,q),"LHolo prepared");
        {
            auto lease=late.beginMutation([]{});
            auto active=late.snapshot();
            check(active->reply.phase==WorldPackReloadPhase::Invalidated,
                  "late BeginNative causes LHolo invalidation");
            check(active->reply.error==static_cast<unsigned>(Error::Invalidated),
                  "late mutation invalidated with error10");
            late.finishMutation(lease,[]{});
        }
    }
    {
        State ordered;
        setup(ordered,held);
        WorldPackReloadReply q{};
        check(call(ordered,WorldPackReloadCommand::Pause,r,q),"native pause");
        check(call(ordered,WorldPackReloadCommand::Prepare,r,q),"native prepared");
        check(call(ordered,WorldPackReloadCommand::BeginNative,r,q),"native BeginNative BEFORE compose");
        {
            auto lease=ordered.beginMutation([]{});
            auto live=ordered.snapshot();
            check(live->reply.phase==WorldPackReloadPhase::NativeActive,
                  "correct early BeginNative retains active phase");
            check(live->reply.cpuReaders>0&&live->reply.nativeBorrowers>0,
                  "native composition callback legitimately owns CPU readers");
            check(live->reply.error==0,"native active has no LHolo invalidation");
            check(call(ordered,WorldPackReloadCommand::Query,r,q),
                  "native active Query accepted mid mutation");
            check(q.phase==WorldPackReloadPhase::NativeActive&&q.cpuReaders>0,
                  "mid mutation must not be rejected as invalid");
            ordered.finishMutation(lease,[]{});
        }
        auto live=ordered.snapshot();
        check(live->reply.phase==WorldPackReloadPhase::NativeActive,
              "composition returned without invalidating LHolo");
        check(live->reply.cpuReaders==0&&live->reply.nativeBorrowers==0,
              "native mutation cleanly released");
    }
    std::printf("R26 installed LHolo State pre-arm order PASS (%u checks)\n",checks);
}
