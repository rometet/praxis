#include "resourcepacks/NativeVanillaPackSelection.h"
#include "resourcepacks/NativeSettingsPackScope.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
int count=0;
void check(bool okay,char const* msg){
    ++count;
    if(!okay){std::fprintf(stderr,"R29 FAIL: %s\n",msg);std::exit(1);}
}
WorldNativeCompatibilityAdmission baseline(){
    WorldNativeCompatibilityAdmission t{};
    t.installed=t.ownerThread=t.sameGame=t.sameWorld=t.sameManager=
        t.identity=t.initial=t.pending=t.prepared=t.originalInGame=t.gameReady=
        t.loadReady=t.notLeaving=t.notMultiplayer=t.noNativeTask=true;
    t.offlineCloneLevelPreserved=true;
    return t;
}
}
int main(){
    std::vector<std::string> anchor{"world-a","world-b","world-c"};
    auto on=anchor;on.push_back("resource-x");
    check(nativeVanillaPackDelta(anchor,anchor)==NativeVanillaPackDelta::Unchanged,"no change");
    check(nativeVanillaPackChange(anchor,on).delta==NativeVanillaPackDelta::EnableOne,"one append");
    check(nativeVanillaPackChange(anchor,on).index==3,"append index 3");
    check(nativeVanillaPackChange(on,anchor).delta==NativeVanillaPackDelta::DisableOne,"one remove");
    check(nativeVanillaPackChange(on,anchor).index==3,"remove tail index");
    std::vector<std::string> front{"resource-x","world-a","world-b","world-c"};
    check(nativeVanillaPackDelta(anchor,front)==NativeVanillaPackDelta::EnableOne,"standard GUI front insert permitted");
    check(nativeVanillaPackChange(anchor,front).index==0,"front index 0");
    check(nativeVanillaPackDelta(front,anchor)==NativeVanillaPackDelta::DisableOne,"standard GUI front remove permitted");
    check(nativeVanillaPackChange(front,anchor).index==0,"front remove index");
    std::vector<std::string> middle{"world-a","resource-x","world-b","world-c"};
    check(nativeVanillaPackDelta(anchor,middle)==NativeVanillaPackDelta::EnableOne,"middle insert permitted");
    check(nativeVanillaPackChange(anchor,middle).index==1,"middle index 1");
    check(nativeVanillaPackDelta(middle,anchor)==NativeVanillaPackDelta::DisableOne,"middle remove permitted");
    check(nativeVanillaPackChange(middle,anchor).index==1,"middle remove index");
    std::vector<std::string> reorder{"world-b","world-a","world-c"};
    check(nativeVanillaPackDelta(anchor,reorder)==NativeVanillaPackDelta::Unsupported,"reordering forbidden");
    std::vector<std::string> removal{"world-a","world-c"};
    check(nativeVanillaPackDelta(anchor,removal)==NativeVanillaPackDelta::DisableOne,
        "shape alone cannot authorize removal of protected source pack");
    std::vector<std::string> replace{"world-a","world-b","other"};
    check(nativeVanillaPackDelta(anchor,replace)==NativeVanillaPackDelta::Unsupported,"replacement forbidden");
    auto both=on;both.push_back("resource-y");
    check(nativeVanillaPackDelta(anchor,both)==NativeVanillaPackDelta::Unsupported,"bulk ON refused");
    check(nativeVanillaPackDelta(both,anchor)==NativeVanillaPackDelta::Unsupported,"bulk OFF refused");
    auto duplicate=on;duplicate.push_back("resource-x");
    check(nativeVanillaPackDelta(on,duplicate)==NativeVanillaPackDelta::Unsupported,"duplicate UUID addition denied");
    auto duplicatedOriginal=anchor;duplicatedOriginal.insert(duplicatedOriginal.begin(),"world-a");
    check(nativeVanillaPackDelta(duplicatedOriginal,anchor)==NativeVanillaPackDelta::Unsupported,
        "duplicate owner removal denied");
    check(vanillaScopedCloneGrant(true,false,true,2,true,false,false),"exact verified clone allowed");
    check(!vanillaScopedCloneGrant(false,false,true,2,true,false,false),"nonclone denied");
    check(!vanillaScopedCloneGrant(true,true,true,2,true,false,false),"multiplayer denied");
    check(!vanillaScopedCloneGrant(true,false,false,2,true,false,false),"changed level denied");
    check(!vanillaScopedCloneGrant(true,false,true,1,true,false,false),"level 1 denied");
    check(!vanillaScopedCloneGrant(true,false,true,3,true,false,false),"level 3 denied");
    check(!vanillaScopedCloneGrant(true,false,true,2,false,false,false),"global off denied");
    check(!vanillaScopedCloneGrant(true,false,true,2,true,true,false),"initialization denied");
    check(!vanillaScopedCloneGrant(true,false,true,2,true,false,true),"pending denied");
    check(nativeSettingsMayIncludeGlobal(false,false,false,true),"verified clone Global permitted");
    check(!nativeSettingsMayIncludeGlobal(false,true,false,true),"multiplayer still denied");
    check(!nativeSettingsMayIncludeGlobal(false,false,false,false),"unverified Level not allowed");
    check(nativeSettingsMayIncludeGlobal(false,false,true),"original level-empty unchanged");
    check(nativeSettingsMayIncludeGlobal(true,true,false),"original native-true preserved");
    auto t=baseline();
    check(worldNativeCompatibilityFailures(t)==(1u<<8),"native compatibility requires specific diff");
    t.singleVanillaEditorChange=true;
    check(admitWorldNativeCompatibility(t),"registered vanilla native change permitted");
    t.noNativeTask=false;
    check(!admitWorldNativeCompatibility(t),"pending native task must still be zero");
    t.noNativeTask=true;t.notMultiplayer=false;
    check(!admitWorldNativeCompatibility(t),"native editor never bypasses multiplayer");
    t.notMultiplayer=true;t.offlineCloneLevelPreserved=false;
    check(!admitWorldNativeCompatibility(t),"native editor never bypasses Level preservation");
    std::printf("R29 vanilla ResourcePack screen policy PASS %d checks\n",count);
}
