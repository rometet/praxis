#include "resourcepacks/Title2660SelectionPlan.h"
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool v,char const* msg){++checks;if(!v){std::fprintf(stderr,"FAILED: %s\n",msg);std::exit(1);}}
Pack resource(char const* key,char const* id, bool editable=true) {
    Pack result;result.key=key;result.uuid=id;result.version="1.0.0";result.selectable=editable;return result;
}
AppliedPack active(char const* id,char const* sub="",unsigned type=6) {
    return {id,"1.0.0",sub,type};
}
Catalog catalog() {
    Catalog out;out.generation=77;out.complete=true;
    out.packs={resource("alpha","a"),resource("bravo","b"),resource("ui","ui",false)};
    return out;
}
AppliedStackView current() {
    return {true,{active("a"),active("ui","default"),active("b")}};
}
Selection selected(std::vector<std::string> order) {return {77,std::move(order)};}
}
int main(){
    auto c=catalog();auto currentView=current();
    auto r=planTitle2660Reorder(c,selected({"bravo","alpha"}),currentView);
    check(r.result==Title2660SelectionResult::ReorderReady,"existing pack reorder allowed");
    check(r.ordered.size()==3&&r.ordered[0].uuid=="b"&&r.ordered[1].uuid=="ui"&&r.ordered[1].subpack=="default"&&r.ordered[2].uuid=="a","unsupported UI pack keeps exact position and subpack");
    r=planTitle2660Reorder(c,selected({"alpha","bravo"}),currentView);
    check(r.result==Title2660SelectionResult::Unchanged,"no-op recognized");
    auto unknown=currentView;unknown.known=false;
    r=planTitle2660Reorder(c,selected({"bravo","alpha"}),unknown);
    check(r.result==Title2660SelectionResult::UnknownNative,"unknown native state rejected");
    auto empty=currentView;empty.order.clear();
    check(planTitle2660Reorder(c,selected({"bravo","alpha"}),empty).result==Title2660SelectionResult::UnsupportedPackCount,"empty native stack rejected");
    check(planTitle2660Reorder(c,selected({}),currentView).result==Title2660SelectionResult::UnsupportedPackCount,"empty selection refused");
    check(planTitle2660Reorder(c,selected({"bravo"}),currentView).result==Title2660SelectionResult::UnsupportedOrNotActive,"removing selected pack forbidden");
    check(planTitle2660Reorder(c,selected({"bravo","alpha","ui"}),currentView).result==Title2660SelectionResult::InvalidCatalog,"disabled pack cannot join proposal");
    check(planTitle2660Reorder(c,selected({"bravo","alpha","alpha"}),currentView).result==Title2660SelectionResult::InvalidCatalog,"duplicate selection refused");
    check(planTitle2660Reorder(c,selected({"unknown","alpha"}),currentView).result==Title2660SelectionResult::InvalidCatalog,"noncatalog pack refused");
    auto foreign=c;foreign.packs.push_back(resource("foreign","x"));
    check(planTitle2660Reorder(foreign,selected({"bravo","alpha","foreign"}),currentView).result==Title2660SelectionResult::UnsupportedOrNotActive,"adding inactive pack forbidden");
    auto changedVersion=currentView;changedVersion.order[0].version="2.0.0";
    check(planTitle2660Reorder(c,selected({"bravo","alpha"}),changedVersion).result==Title2660SelectionResult::UnsupportedOrNotActive,"stale native version refused");
    auto subpackChange=currentView;subpackChange.order[0].subpack="alt";
    check(planTitle2660Reorder(c,selected({"bravo","alpha"}),subpackChange).result==Title2660SelectionResult::UnsupportedOrNotActive,"subpack cannot be dropped");
    auto wrongType=currentView;wrongType.order[0].packType=3;
    check(planTitle2660Reorder(c,selected({"bravo","alpha"}),wrongType).result==Title2660SelectionResult::UnsupportedOrNotActive,"behavior pack not treated as texture");
    auto duplicateActive=currentView;duplicateActive.order.push_back(active("a"));
    check(planTitle2660Reorder(c,selected({"bravo","alpha"}),duplicateActive).result==Title2660SelectionResult::UnsupportedOrNotActive,"ambiguous active duplicate refused");
    auto c2=c;c2.generation=78;
    check(planTitle2660Reorder(c2,selected({"bravo","alpha"}),currentView).result==Title2660SelectionResult::InvalidCatalog,"stale scan generation refused");
    auto c3=c;c3.error="partial";
    check(planTitle2660Reorder(c3,selected({"bravo","alpha"}),currentView).result==Title2660SelectionResult::InvalidCatalog,"partial scan refused");
    auto duplicateCatalog=c;duplicateCatalog.packs.push_back(resource("alias","a"));
    check(planTitle2660Reorder(duplicateCatalog,selected({"bravo","alpha"}),currentView).result==Title2660SelectionResult::ForeignNativeEntry,"ambiguous registered UUID refused");
    std::printf("Title2660SelectionPlanTests checks=%u PASS\n",checks);
}
