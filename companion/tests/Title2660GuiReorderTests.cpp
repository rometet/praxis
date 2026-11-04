#include "resourcepacks/Title2660GuiReorder.h"
#include <cstdio>
#include <cstdlib>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value,char const* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
TitlePackView owner(){
    TitlePackView state;
    state.nativeReorderOnly=true;state.available=true;state.current.known=true;
    state.until=1000;state.revision=30;
    state.current.order={{"A","1","",6},{"B","1","default",6},{"C","2","",6}};
    return state;
}
}
int main(){
    {
        Title2660GuiReorder ctrl;
        auto o=owner();auto reversed=o.current.order;
        std::swap(reversed[0],reversed[2]);
        check(ctrl.request(o,reversed,20),"explicit reorder of same three native packs");
        check(ctrl.isBusy(),"queued request blocks duplicate");
        check(!ctrl.request(o,reversed,21),"double request denied");
        auto request=ctrl.take();
        check(request.has_value()&&request->token==1&&request->original.size()==3,"queue has an exact original identity snapshot");
        check(request->target[0].uuid=="C"&&request->target[1].subpack=="default","subpack remains unchanged");
        check(!ctrl.take().has_value(),"single-consumption request");
        ctrl.report(request->token,Title2660GuiReorderState::Applying,"Applying");
        check(ctrl.view().busy&&ctrl.view().state==Title2660GuiReorderState::Applying,"active operation remains exclusive");
        ctrl.report(request->token+1,Title2660GuiReorderState::Applied,"Bad");
        check(ctrl.view().state==Title2660GuiReorderState::Applying,"foreign completion ignored");
        ctrl.report(request->token,Title2660GuiReorderState::Applied,"Done");
        check(!ctrl.view().busy&&ctrl.view().state==Title2660GuiReorderState::Applied,"native completed request settles");
    }
    {
        Title2660GuiReorder ctrl;
        auto o=owner();auto target=o.current.order;
        std::swap(target[0],target[1]);
        check(!ctrl.request(o,o.current.order,20),"unchanged pack selection ignored");
        auto missing=target;missing.pop_back();
        check(!ctrl.request(o,missing,20),"pack removal refused");
        auto newPack=target;newPack[0].uuid="alien";
        check(!ctrl.request(o,newPack,20),"enabling an unregistered pack refused");
        auto duplicate=target;duplicate[2]=target[1];
        check(!ctrl.request(o,duplicate,20),"duplicate pack cannot replace a member");
        auto subpackChange=target;subpackChange[0].subpack="different";
        check(!ctrl.request(o,subpackChange,20),"subpack settings retained exactly");
        auto changedVersion=target;changedVersion[1].version="9";
        check(!ctrl.request(o,changedVersion,20),"stale pack version refused");
        auto disabled=o;disabled.available=false;
        check(!ctrl.request(disabled,target,20),"inactive game UI refuses request");
        auto wrongScreen=o;wrongScreen.nativeReorderOnly=false;
        check(!ctrl.request(wrongScreen,target,20),"non-title screen refuses request");
        auto busy=o;busy.busy=true;
        check(!ctrl.request(busy,target,20),"other native operation busy");
        auto expired=o;expired.until=10;
        check(!ctrl.request(expired,target,20),"expired view refuses request");
        auto unknown=o;unknown.current.known=false;
        check(!ctrl.request(unknown,target,20),"unknown global stack refused");
        auto single=o;single.current.order.resize(1);
        check(!ctrl.request(single,target,20),"invalid native stack rejected");
        check(ctrl.request(o,target,20),"valid change accepted after refused requests");
        auto id=ctrl.view().token;
        check(ctrl.cancelBeforeNative(id),"queued command cancellable before setter");
        check(!ctrl.take().has_value(),"cancelled item not delivered to native code");
        check(!ctrl.view().busy,"cancel clears exclusive busy status");
        check(ctrl.request(o,target,20),"new command permitted after pre-native cancel");
        auto second=ctrl.take();
        ctrl.report(second->token,Title2660GuiReorderState::RecoveryRequired,"Recovery");
        check(ctrl.needsRecovery(),"unproven rollback locks further mutation");
        check(!ctrl.request(o,target,20),"recovery requires restart");
    }
    std::printf("Title2660GuiReorderTests checks=%u PASS\n",checks);
}
