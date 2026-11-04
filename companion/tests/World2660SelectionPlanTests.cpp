#include "resourcepacks/World2660SelectionPlan.h"
#include "resourcepacks/Title2660Receipt.h"
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool valid,char const* msg){
    ++checks;
    if(!valid){std::fprintf(stderr,"FAIL %s\n",msg);std::exit(1);}
}
Pack pack(char const* key,char const* uuid,bool selectable=true) {
    Pack p;p.key=key;p.uuid=uuid;p.version="1.0.0";p.selectable=selectable;return p;
}
Catalog catalog(){
    Catalog c;c.complete=true;c.generation=3;
    c.packs={pack("a","aaa"),pack("b","bbb"),pack("c","ccc")};
    return c;
}
AppliedStackView native(){
    return {true,{
        {"aaa","1.0.0","",6},
        {"ui-pack","1.0.0","default",6},
        {"bbb","1.0.0","",6}
    }};
}
Selection choice(std::initializer_list<char const*> list){
    Selection s;s.generation=3;
    for(auto x:list)s.order.emplace_back(x);
    return s;
}
}
int main(){
    auto c=catalog();auto current=native();
    {
        // Exact 26.51 user controls with 26.60 native PackType::Invalid:
        // multiple selections, new pack, priority reorder and protected
        // subpack preservation.
        AppliedStackView active{true,{{"aaa","1.0.0","",0},
                                       {"bbb","1.0.0","",0},
                                       {"user-ui","1.0.0","default",0}}};
        auto plan=planWorld2660Global(c,choice({"b","a","c"}),active);
        check(plan.result==World2660PlanResult::Ready,"26.60 multi-pack reorder plus add");
        check(plan.target.size()==4&&plan.target[0].uuid=="bbb"
              &&plan.target[1].uuid=="aaa"
              &&plan.target[2].uuid=="user-ui"
              &&plan.target[3].uuid=="ccc","protected subpack and priorities preserved");
        auto off=planWorld2660Global(c,choice({"b"}),active);
        check(off.result==World2660PlanResult::Ready
              &&off.target.size()==2&&off.target[1].uuid=="user-ui",
              "26.60 multi-pack removal retains protected entry");
    }
    {
        auto p=planWorld2660Global(c,choice({"a","b"}),current);
        check(p.result==World2660PlanResult::Unchanged,"no-op recognized");
        check(p.target[1].subpack=="default","non-editable subpack preserved");
    }
    {
        auto p=planWorld2660Global(c,choice({"b","a"}),current);
        check(p.result==World2660PlanResult::Ready,"reorder accepted");
        check(p.target[0].uuid=="bbb"&&p.target[1].uuid=="ui-pack"
              &&p.target[2].uuid=="aaa","protected pack position intact");
    }
    {
        auto p=planWorld2660Global(c,choice({"a","b","c"}),current);
        check(p.result==World2660PlanResult::Ready,"register new pack candidate");
        check(p.target.size()==4&&p.target[3].uuid=="ccc","new global appended");
    }
    {
        auto p=planWorld2660Global(c,choice({"a"}),current);
        check(p.result==World2660PlanResult::Ready,"disable one editable pack");
        check(p.target.size()==2&&p.target[1].uuid=="ui-pack","protected untouched");
    }
    {
        auto p=planWorld2660Global(c,choice({}),current);
        check(p.result==World2660PlanResult::Ready,"empty editable choice is allowed");
        check(p.target.size()==1&&p.target[0].uuid=="ui-pack","empty never removes protected UI pack");
    }
    {
        auto p=planWorld2660Global(c,choice({"missing"}),current);
        check(p.result==World2660PlanResult::UnknownSelection,"unregistered choice rejected");
    }
    {
        auto p=planWorld2660Global(c,choice({"a","a"}),current);
        check(p.result==World2660PlanResult::DuplicateOrProtected,"duplicate request rejected");
    }
    {
        auto p=planWorld2660Global(c,choice({"a","b"}),AppliedStackView{});
        check(p.result==World2660PlanResult::UnknownNative,"unknown original cannot mutate");
    }
    {
        auto old=current;old.order.push_back(old.order[0]);
        auto p=planWorld2660Global(c,choice({"a","b"}),old);
        check(p.result==World2660PlanResult::DuplicateOrProtected,"duplicate native identity rejected");
    }
    {
        auto foreign=current;foreign.order[1].uuid="ccc";foreign.order[1].subpack="custom";
        auto p=planWorld2660Global(c,choice({"a","b","c"}),foreign);
        check(p.result==World2660PlanResult::DuplicateOrProtected,"protected native UUID conflict rejected");
    }
    {
        auto invalid=c;invalid.complete=false;
        check(planWorld2660Global(invalid,choice({"a"}),current).result==
              World2660PlanResult::StaleCatalog,"incomplete catalog rejected");
        auto stale=choice({"a"});stale.generation=2;
        check(planWorld2660Global(c,stale,current).result==
              World2660PlanResult::StaleCatalog,"stale scan generation rejected");
    }
    {
        auto duplicateCatalog=c;duplicateCatalog.packs.push_back(pack("alias","aaa"));
        check(planWorld2660Global(duplicateCatalog,choice({"a"}),current).result==
              World2660PlanResult::DuplicateOrProtected,"duplicate catalog UUID rejected");
    }
    {
        auto locked=c;locked.packs[0].selectable=false;
        check(planWorld2660Global(locked,choice({"a","b"}),current).result==
              World2660PlanResult::UnknownSelection,"locked pack rejected");
    }
    {
        auto unknown=current;unknown.order.push_back({"behavior","5","",3});
        auto p=planWorld2660Global(c,choice({"a"}),unknown);
        check(p.result==World2660PlanResult::Ready&&p.target.back().packType==3,
              "unrelated behavior pack retained");
    }
    {
        Title2660Receipt receipt;
        check(receipt.prepare(1,99,{"pack-A"},{"pack-A"},200,true),
            "explicit same-global reload can have owned completion");
        check(receipt.beginNativeCall(99,10),"reload admitted on correct owner");
        receipt.setterResult(99,true);
        receipt.onActive(99);
        receipt.onFinish(99);
        check(receipt.endNativeCall(99,11),"reload callbacks still required");
        check(receipt.poll(99,1,12,{"pack-A"},true,false,true)==
              Title2660Receipt::Poll::Wait,"reload requires distinct settled frames");
        check(receipt.poll(99,2,13,{"pack-A"},true,false,true)==
              Title2660Receipt::Poll::Applied,"reload completes only after native proof");
    }
    {
        Title2660Receipt firstPack;
        check(firstPack.prepare(12,99,{},{"pack-A"},200,false,true),
              "world can enable first Global pack");
        check(firstPack.beginNativeCall(99,10),"first-pack native call starts");
        firstPack.setterResult(99,true);
        firstPack.onActive(99);firstPack.onFinish(99);
        check(firstPack.endNativeCall(99,11),"first-pack native callback receipt");
        check(firstPack.poll(99,1,12,{"pack-A"},true,false,true)==
              Title2660Receipt::Poll::Wait,"first-pack requires settled frame");
        check(firstPack.poll(99,2,13,{"pack-A"},true,false,true)==
              Title2660Receipt::Poll::Applied,"first-pack applies after proof");
    }
    {
        Title2660Receipt lastPack;
        check(lastPack.prepare(13,99,{"pack-A"},{},200,false,true),
              "world can disable final Global pack");
        check(lastPack.beginNativeCall(99,10),"last-pack native call starts");
        lastPack.setterResult(99,true);
        lastPack.onActive(99);lastPack.onFinish(99);
        check(lastPack.endNativeCall(99,11),"last-pack native callback receipt");
        check(lastPack.poll(99,1,12,{},true,false,true)==
              Title2660Receipt::Poll::Wait,"last-pack requires settled frame");
        check(lastPack.poll(99,2,13,{},true,false,true)==
              Title2660Receipt::Poll::Applied,"last-pack applies after proof");
    }
    {
        Title2660Receipt unchangedTitleSafety;
        check(!unchangedTitleSafety.prepare(14,99,{},{"pack-A"},200),
              "title empty-stack admission remains unchanged");
    }
    std::printf("World2660SelectionPlanTests checks=%u PASS\n",checks);
}
