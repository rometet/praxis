#include "resourcepacks/WorldPackPeers.h"
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"R26 PASS/FAIL failed %s\n",what);std::exit(1);}}
WorldPackReloadRequest request(){
    WorldPackReloadRequest r;
    r.token=1;r.world=2;r.client=3;r.renderer=4;r.oldEpoch=5;r.nextEpoch=6;
    return r;
}
WorldPackReloadReply active(WorldPackReloadRequest const& r){
    WorldPackReloadReply p;
    p.token=r.token;p.world=r.world;p.client=r.client;p.renderer=r.renderer;
    p.epoch=r.oldEpoch;p.phase=WorldPackReloadPhase::NativeActive;
    p.oldOwnersRetained=1;p.admissionClosed=1;p.ownedGpu=OwnedGpuFence::NativeManaged;
    p.cpuReaders=4;p.asyncJobs=3;p.nativeBorrowers=2;return p;
}
}
int main(){
    auto r=request();
    auto p=active(r);
    check(WorldPackPeers::nativeActiveReceipt(r,p),"correct native active valid despite owned work");
    check(!participantReceipt(r,p,WorldPackReloadPhase::Prepared,r.oldEpoch),
          "native active cannot be mistaken for Prepared");
    auto other=p;other.phase=WorldPackReloadPhase::Prepared;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"still Prepared cannot be treated as NativeActive");
    other=p;other.phase=WorldPackReloadPhase::Invalidated;other.error=10;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"LHolo invalidated denied");
    other=p;other.token++;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"foreign token denied");
    other=p;other.renderer++;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"changed owner denied");
    other=p;other.client++;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"changed client denied");
    other=p;other.world++;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"foreign world denied");
    other=p;other.epoch++;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"new epoch premature denied");
    other=p;other.admissionClosed=0;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"opened admission denied");
    other=p;other.oldOwnersRetained=0;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"old owners released early denied");
    other=p;other.error=1;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"native error denied");
    other=p;other.bytes=1;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"bad ABI struct size denied");
    other=p;other.abi=3;
    check(!WorldPackPeers::nativeActiveReceipt(r,other),"unsupported ABI version denied");
    std::printf("R26 NativeActiveReceipt PASS (%u checks)\n",checks);
}
