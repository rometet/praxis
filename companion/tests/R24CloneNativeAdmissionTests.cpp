#include "resourcepacks/R24OfflineCloneGate.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* what){
    ++checks;
    if(!value){std::fprintf(stderr,"R24 FAIL: %s\n",what);std::exit(1);}
}
WorldNativeCompatibilityAdmission fullyPrepared(){
    WorldNativeCompatibilityAdmission p{};
    p.installed=p.ownerThread=p.sameGame=p.sameWorld=p.sameManager
        =p.identity=p.initial=p.pending=p.singleLocalAppend=p.prepared
        =p.originalInGame=p.gameReady=p.loadReady=p.notLeaving
        =p.notMultiplayer=p.noNativeTask=true;
    return p;
}
}
int main() {
    auto c=R24WorldPermit{
        R24CloneDirectory,R24CloneDisplayName,false,true,false,false,2};
    check(isR24OfflineClone(c),"exact 2-pack test clone permitted");
    auto old=c;
    old.directoryId="SomeOtherWorld";
    check(!isR24OfflineClone(old),"other world denied");
    old=c;old.displayName="Test World";
    check(!isR24OfflineClone(old),"same directory changed title denied");
    old=c;old.multiplayer=true;
    check(!isR24OfflineClone(old),"multiplayer denied");
    old=c;old.globalEnabled=false;
    check(!isR24OfflineClone(old),"global override disabled denied");
    old=c;old.pending=true;
    check(!isR24OfflineClone(old),"pending restack denied");
    old=c;old.initializing=true;
    check(!isR24OfflineClone(old),"initializing denied");
    old=c;old.levelPacks=1;
    check(!isR24OfflineClone(old),"other Level RP count denied");
    old=c;old.levelPacks=3;
    check(!isR24OfflineClone(old),"extra Level RP denied");
    auto a=fullyPrepared();
    a.levelEmpty=false;
    check(worldNativeCompatibilityFailures(a)==(1u<<15),"Level RP default denial");
    a.offlineCloneLevelPreserved=true;
    check(admitWorldNativeCompatibility(a),"exact authorized clone level allowed");
    a.notMultiplayer=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass multiplayer");
    a.notMultiplayer=true;a.singleLocalAppend=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass single append");
    a.singleLocalAppend=true;a.noNativeTask=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass native tasks");
    a.noNativeTask=true;a.pending=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass pending check");
    a.pending=true;a.initial=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass initial state");
    a.initial=true;a.identity=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass native owner identity");
    a.identity=true;a.prepared=false;
    check(!admitWorldNativeCompatibility(a),"cannot bypass peer readiness");
    std::printf("R24 restricted full reload admission PASS (%u checks)\n",checks);
}
