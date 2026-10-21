#include "resourcepacks/R27LocalToggle.h"
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <utility>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void test(bool okay,char const* text){
    ++checks;
    if(!okay){std::fprintf(stderr,"R27 FAIL %s\n",text);std::exit(1);}
}
AppliedPack protectedPack(char const* uuid){
    return {uuid,"1.0.0","",0};
}
AppliedPack texture(char const* uuid){
    // 1.26.51.01 runtime PackInstanceId carries identity type Invalid(0)
    // even though the pack's verified manifest has Resources(6).
    return {uuid,"1.0.0","",0};
}
std::shared_ptr<Catalog> catalog(){
    auto c=std::make_shared<Catalog>();
    c->complete=true;c->generation=17;
    c->packs.push_back({"localA","Test Powder A","test-a","1.0.0","", "texture",true});
    c->packs.push_back({"localB","Test Powder B","test-b","1.0.0","", "texture",true});
    return c;
}
WorldNativeCompatibilityAdmission valid(){
    WorldNativeCompatibilityAdmission a{};
    a.installed=a.ownerThread=a.sameGame=a.sameWorld=a.sameManager=a.identity
      =a.initial=a.pending=a.prepared=a.originalInGame=a.gameReady=a.loadReady
      =a.notLeaving=a.notMultiplayer=a.noNativeTask=true;
    a.levelEmpty=false;a.offlineCloneLevelPreserved=true;
    return a;
}
}
int main(){
    std::vector<std::string> before{"world-a","world-b","world-c"};
    std::vector<std::string> on{"world-a","world-b","world-c","local-a"};
    std::vector<std::string> after{"world-a","world-b","world-c"};
    test(r27NativeToggleShape(before,on,1,1)==R27LocalToggle::Enable,
        "append one protected-prefix pack");
    test(r27NativeToggleShape(on,after,0,1)==R27LocalToggle::Disable,
        "remove exactly one trailing pack");
    test(r27NativeToggleShape(after,on,1,1)==R27LocalToggle::Enable,
        "ON again after OFF");
    test(r27NativeToggleShape(before,on,2,1)==R27LocalToggle::Invalid,
        "two selections denied");
    test(r27NativeToggleShape(on,after,1,1)==R27LocalToggle::Invalid,
        "disable requires empty proposed selection");
    test(r27NativeToggleShape(before,on,1,0)==R27LocalToggle::Invalid,
        "unowned addition denied");
    test(r27NativeToggleShape(on,after,0,2)==R27LocalToggle::Invalid,
        "two candidate owners denied");
    test(r27NativeToggleShape(on,on,1,1)==R27LocalToggle::Invalid,
        "no native mutation masquerading as ON");
    std::vector<std::string> wrongOrder{"world-b","world-a","world-c"};
    test(r27NativeToggleShape(on,wrongOrder,0,1)==R27LocalToggle::Invalid,
        "reorder original packs rejected");
    std::vector<std::string> notTail{"world-a","local-a","world-c"};
    test(r27NativeToggleShape(on,notTail,0,1)==R27LocalToggle::Invalid,
        "cannot remove middle world pack");
    std::vector<std::string> twice{"world-a","world-b","world-c","local-a","local-b"};
    test(r27NativeToggleShape(twice,after,0,1)==R27LocalToggle::Invalid,
        "cannot remove two local packs");
    auto c=catalog();
    auto a=c->packs[0],b=c->packs[1];
    AppliedStackView empty{true,{protectedPack("one"),protectedPack("two"),protectedPack("three")}};
    auto state=r27UiToggleState(*c,empty,a);
    test(state.canEnable&&!state.canDisable&&!state.targetActive,
        "R27 UI offers ON only when no selected local active");
    AppliedStackView withA=empty;withA.order.push_back(texture("test-a"));
    state=r27UiToggleState(*c,withA,a);
    test(state.targetActive&&state.canDisable&&!state.canEnable,
        "R27 UI offers OFF for one tail local pack");
    state=r27UiToggleState(*c,withA,b);
    test(!state.canEnable&&!state.canDisable,
        "R27 UI cannot replace active local B without first disabling A");
    AppliedStackView atMiddle{true,{protectedPack("one"),texture("test-a"),protectedPack("three")}};
    state=r27UiToggleState(*c,atMiddle,a);
    test(!state.canDisable,"non-tail local pack cannot be disabled");
    AppliedStackView multi=withA;multi.order.push_back(texture("test-b"));
    state=r27UiToggleState(*c,multi,a);
    test(!state.canDisable,"two editable packs must not be bulk-disabled");
    // Alternate SDK identity representation Resources(6) remains supported.
    auto variant=withA;variant.order.back().packType=6;
    state=r27UiToggleState(*c,variant,a);
    test(state.canDisable,"manifest-style resource type6 also accepted");
    variant=withA;variant.order.back().subpack="other";
    state=r27UiToggleState(*c,variant,a);
    test(!state.targetActive&&!state.canDisable,"named subpack must never be removed");
    variant=withA;variant.order.back().version="2.0.0";
    state=r27UiToggleState(*c,variant,a);
    test(!state.targetActive&&!state.canDisable,"version mismatch must not be removed");
    AppliedStackView unknown{};
    test(!r27UiToggleState(*c,unknown,a).canEnable,
        "unverified active global cannot be changed");
    auto nonselect=a;nonselect.selectable=false;
    test(!r27UiToggleState(*c,empty,nonselect).canEnable,
        "non-selectable local pack cannot be toggled");
    auto gate=valid();gate.singleLocalRemove=true;
    test(admitWorldNativeCompatibility(gate),"R27 OFF has native Full gate admitted");
    gate.singleLocalRemove=false;gate.singleLocalAppend=true;
    test(admitWorldNativeCompatibility(gate),"R26 ON remains authorized");
    gate.singleLocalAppend=false;
    test(!admitWorldNativeCompatibility(gate),"no specific native diff denied");
    gate.singleLocalRemove=true;gate.notMultiplayer=false;
    test(!admitWorldNativeCompatibility(gate),"OFF never bypasses multiplayer");
    gate.notMultiplayer=true;gate.offlineCloneLevelPreserved=false;
    test(!admitWorldNativeCompatibility(gate),"OFF never bypasses clone");
    gate.offlineCloneLevelPreserved=true;gate.pending=false;
    test(!admitWorldNativeCompatibility(gate),"OFF never bypasses Pending flag");
    // The existing request controller must accept an empty, fully validated
    // selection as an actual DELETE-ONE proposal, not mistake it for reload.
    TitlePackControl control;
    TitlePackView view{};
    view.available=true;view.inWorld=true;view.canReload=true;
    view.current=withA;view.revision=7;view.until=titlePackNow()+10000;
    control.publish(view);
    Selection off{};off.generation=c->generation;
    auto result=control.requestResult(c,off,titlePackNow(),false);
    test(result.accepted&&result.token!=0,"native backend queue accepts OFF proposal");
    auto queued=control.take();
    test(queued&&queued->selection.order.empty()&&!queued->reload,
        "OFF remains explicit empty proposal, not a reload");
    std::printf("R27 ON/OFF UI and native policy PASS (%u checks)\n",checks);
}
