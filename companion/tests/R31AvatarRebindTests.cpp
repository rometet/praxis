#include "resourcepacks/R31AvatarRebindGate.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using namespace praxis::companion::packs;
namespace {
unsigned checked{};
void test(bool truth, char const* label) {
    ++checked;
    if(!truth){std::fprintf(stderr,"R31 player rebind policy failed: %s\n",label);std::exit(1);}
}
R31AvatarRebindGate valid(){
    R31AvatarRebindGate g{};
    g.offlineClone=g.vanillaSettings=g.apiReload=g.nativeLoading=
    g.applicationReady=g.frameOwner=g.gameplayScreen=g.gameReady=
    g.noRestack=g.actorDefinitions=g.peersNativeActive=g.firstAttempt=true;
    return g;
}
}
int main(){
    auto g=valid();
    test(r31AvatarRebindAllowed(g),"single local clone permit");
    test(r31AvatarRebindMissing(g)==0,"no missing bits");
#define REFUSE(member,bit) {auto c=g;c.member=false;test(!r31AvatarRebindAllowed(c),#member " denied");test((r31AvatarRebindMissing(c)&(1u<<(bit)))!=0,#member " diagnostic bit");}
    REFUSE(offlineClone,0);
    REFUSE(vanillaSettings,1);
    REFUSE(apiReload,2);
    REFUSE(nativeLoading,3);
    REFUSE(applicationReady,4);
    REFUSE(frameOwner,5);
    REFUSE(gameplayScreen,6);
    REFUSE(gameReady,7);
    REFUSE(noRestack,8);
    REFUSE(actorDefinitions,9);
    REFUSE(peersNativeActive,10);
    REFUSE(firstAttempt,11);
#undef REFUSE
    auto wrong=g;
    wrong.offlineClone=false;wrong.peersNativeActive=false;
    test(r31AvatarRebindMissing(wrong)==((1u<<0)|(1u<<10)),
        "exact combined diagnostic mask");
    wrong=g;wrong.nativeLoading=false;wrong.applicationReady=false;
    test(r31AvatarRebindMissing(wrong)==((1u<<3)|(1u<<4)),
        "Loading/phase invalid, no rebind");
    std::printf("R31 player entity rebind policy PASS (%u checks)\n",checked);
}
