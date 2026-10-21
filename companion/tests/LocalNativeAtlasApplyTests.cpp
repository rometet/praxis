#include "resourcepacks/LocalNativeAtlasApplyControl.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace praxis::companion::packs;
using namespace std::chrono_literals;
namespace {
unsigned checks{};
void require(bool pass, char const* reason) {
    ++checks;
    if(!pass) {std::fprintf(stderr,"R23 FAIL %s\n",reason);std::exit(1);}
}
LocalTerrainAtlasProof readyProof(){
    LocalTerrainAtlasProof p{};
    p.stagedToken=17;p.world=9;
    p.gameAtlas=p.terrainManager=p.sameAtlasManager=p.textureGroup=true;
    p.sourcePathPowder=p.uvFinite=p.uvInsideAtlas
        =p.sourceDimensionsMatch=p.uvAreaNonzero=true;
    p.candidateW=p.candidateH=p.uvSourceW=p.uvSourceH=16;
    p.uvTexW=p.uvTexH=1024;
    p.u0=0.1f;p.v0=0.1f;p.u1=0.2f;p.v1=0.2f;
    return p;
}
}
int main(){
    auto proof=readyProof();
    LocalNativeAtlasApplyControl one;
    require(one.view().phase==NativeAtlasApplyPhase::Idle,"idle");
    require(!one.request(true,proof,88),"wrong staging token rejected");
    require(one.request(true,proof,17),"queue valid ON");
    require(!one.request(true,proof,17),"no duplicate ON");
    auto req=one.take();
    require(bool(req) && req->enable && req->token==1,"one native ON request");
    require(!one.failIfStalled(std::chrono::steady_clock::now()+30s),
        "before native issue no expiry");
    one.begun(req->token,true);
    one.statusCallback(req->token);
    one.finished(req->token);
    require(one.view().phase==NativeAtlasApplyPhase::WaitingFrame
        && one.view().statusCallbacks==1 && one.view().finished==1,
        "native callback alone is not accepted as render success");
    one.frame(req->token,0);
    require(one.view().phase==NativeAtlasApplyPhase::Failed
        && one.view().renderFrames==1,"zero source hits fail closed");
    one.finished(req->token);
    require(one.view().phase==NativeAtlasApplyPhase::Failed,
        "late completion cannot restore failed state");
    require(!one.request(false,proof,17),"cannot restore without verified ON");
    LocalNativeAtlasApplyControl two;
    require(two.request(true,proof,17),"fresh ON");
    auto on=two.take();
    two.begun(on->token,true);
    two.finished(on->token);
    two.frame(on->token,3);
    require(two.view().phase==NativeAtlasApplyPhase::Enabled
        && two.view().sourceHits==3 && two.view().renderVerified,
        "native completion AND source hit AND game frame enable");
    require(two.request(false,proof,17),"OFF only from verified enabled");
    auto off=two.take();
    require(bool(off) && !off->enable,"OFF request observed");
    two.begun(off->token,true);
    two.finished(off->token);
    two.frame(off->token,0);
    require(two.view().phase==NativeAtlasApplyPhase::Restored,
        "OFF requires native completion and draw");
    LocalNativeAtlasApplyControl three;
    require(three.request(true,proof,17),"timeout queued");
    auto t=three.take();
    auto now=std::chrono::steady_clock::now();
    three.begun(t->token,true);
    require(!three.failIfStalled(now+19s),"not timeout at 19 seconds");
    require(three.failIfStalled(now+22s),"timeout at 22 seconds");
    three.finished(t->token);three.frame(t->token,5);
    require(three.view().phase==NativeAtlasApplyPhase::Failed
        && three.view().renderFrames==0,"timeout cannot be resurrected");
    std::printf("R23 LocalNativeAtlasApplyControl PASS (%u checks)\n",checks);
}
