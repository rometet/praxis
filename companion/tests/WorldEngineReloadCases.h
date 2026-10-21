#pragma once
#include "resourcepacks/WorldEngineReloadReceipt.h"
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/NativeSettingsPackScope.h"

template<class Check> void worldEngineReloadCases(Check check){
    using namespace praxis::companion::packs;
    using Receipt=WorldEngineReloadReceipt;
    Receipt::Identity id{9,1,7,44,11,33,66};
    check(worldEngineDispatchAction(false,false)==WorldEngineDispatchAction::Request,
        "fresh completed selection dispatches engine exactly once");
    check(worldEngineDispatchAction(true,false)==WorldEngineDispatchAction::AwaitNativeEntry,
        "public engine call return without synchronous native hook waits instead of sealing or retrying");
    check(worldEngineDispatchAction(true,true)==WorldEngineDispatchAction::Started,
        "eventual real native hook ends deferred start waiting");
    auto selected=[&]{Receipt r;check(r.prepare(id)&&r.selectionEntered(id),"engine receipt binds actual selection");
        check(r.selectionReturned(id,true,true),"selection returned with exact target");return r;};
    auto started=[&]{auto r=selected();check(r.engineEntered(id,10,true,true),"actual native entry joins composed Full and Prepared peers");return r;};
    auto loaded=[&]{auto r=started();check(r.engineReturned(10,true)&&r.finishReturned(10,true),"actual engine normal return and finish");
        check(r.needsUI(10,false,true,true)&&r.requestUI(10)&&r.uiReturned(10,true),"UI reload follows real native finish");
        check(r.loadingObserved(10,false,true,true),"later post-UI loading query admits draw");return r;};
    {
        auto r=selected();
        check(!r.applicationReady()&&!r.frameReady()&&!r.beginFrame(11,33,7,55,1),"selection-only cannot complete or draw");
        check(!r.needsUI(10,false,true,true),"loading query and exact Full do not fabricate native start/finish");
        check(!r.applicationReady(),"native listener absence keeps selection receipt incomplete");
        check(r.engineEntered(id,10,true,true),"eventual native listener acceptance permits start after waiting");
        check(!r.needsUI(10,false,true,true),"entry without return/finish cannot request UI");
    }
    for(unsigned fault=0;fault!=4;++fault){
        auto r=selected();auto wrong=id;
        if(fault==0)++wrong.token;if(fault==1)++wrong.world;
        check(!r.engineEntered(wrong,10,fault!=2,fault!=3)&&r.sealed(),"wrong token/world/Full/Prepared fails closed");
    }
    {
        auto r=started();check(r.engineReturned(10,true),"native normal entry return recorded");
        check(!r.needsUI(10,false,true,true),"normal start and query without finish stay incomplete");
        check(!r.finishReturned(9,true)&&r.sealed(),"foreign finish serial cannot complete current operation");
    }
    {
        auto r=started();check(r.finishReturned(10,true),"synchronous native finish can precede entry return");
        check(!r.needsUI(10,false,true,true),"finish alone cannot request UI");
        check(r.engineReturned(10,true),"matching entry returns after synchronous finish");
        check(!r.needsUI(10,true,true,true)&&!r.needsUI(10,false,false,true)&&!r.needsUI(10,false,true,false),"pending/Full/loading independently block UI");
        check(r.needsUI(10,false,true,true)&&r.requestUI(10),"all native gates permit exactly one UI reload");
        check(!r.loadingObserved(10,false,true,true),"UI request without normal return cannot complete");
        check(!r.uiReturned(10,false)&&r.sealed(),"UI exception preserves closed receipt");
    }
    {
        auto r=loaded();check(!r.needsUI(10,false,true,true),"successful UI reload is not repeated");
        check(!r.beginFrame(11,34,7,55,1)&&!r.beginFrame(11,33,8,55,1),"wrong client/world cannot issue current draw permit");
        auto frame=r.beginFrame(11,33,7,55,1);check(bool(frame),"post-UI ready receipt issues real draw permit");
        check(!r.frameReturned(frame,11,33,7,55,true)&&!r.frameReady(),"GameRenderer return without level draw is insufficient");
        frame=r.beginFrame(11,33,7,55,2);check(r.levelRendered(frame,55,true)&&r.frameReturned(frame,11,33,7,55,true)&&r.frameReady(),"joined current LevelRenderer and GameRenderer normal draw completes");
        r.requireCurrentFrame(56);check(!r.frameReady(),"renderer replacement invalidates old draw");
        frame=r.beginFrame(11,33,7,56,3);check(r.levelRendered(frame,56,true)&&r.frameReturned(frame,11,33,7,56,true)&&r.frameReady(),"replacement renderer must provide its own completed draw");
        check(!r.loadingObserved(10,true,true,true)&&!r.frameReady(),"new pending work invalidates previous ready draw");
        check(r.loadingObserved(10,false,true,true)&&!r.frameReady(),"later loading success cannot reuse draw before loading gap");
    }
    {
        auto r=loaded();auto frame=r.beginFrame(11,33,7,55,1);
        check(!r.frameReturned(frame,11,33,8,55,true)&&r.sealed(),"world loss during in-flight draw seals receipt");
    }
    {
        TitlePackControl control;TitlePackView v;v.inWorld=v.available=v.canReload=v.current.known=true;v.until=100;v.revision=7;control.publish(v);
        Selection edited;edited.generation=999;edited.order={"unapplied-proposal"};
        check(control.requestResult({},edited,10,true).accepted,"current-active reload does not require catalog or consume edited candidate");
        auto request=control.take();check(request&&request->reload&&request->selection.order.empty()&&request->revision==7,"reload request explicitly discards candidate and retains owner revision");
        check(!control.requestResult({},edited,10,true).accepted,"reload cannot bypass busy transaction");
    }
    for(unsigned fault=0;fault!=5;++fault){
        TitlePackControl control;TitlePackView v;v.inWorld=v.available=v.canReload=v.current.known=true;v.until=100;
        if(fault==0)v.available=false;if(fault==1)v.canReload=false;if(fault==2)v.current.known=false;if(fault==3)v.busy=true;
        control.publish(v);check(!control.requestResult({},Selection{},fault==4?101:10,true).accepted,"current reload retains availability/capability/current/busy/expiry gates");
    }
    {
        TitlePackControl control;TitlePackView v;v.inWorld=v.available=v.canReload=v.current.known=true;v.until=100;control.publish(v);
        auto native=control.reserveWorldOperationToken();check(native==1,"native Settings reserves from shared operation namespace");
        check(control.requestResult({},Selection{},10,true).accepted,"custom request follows native reservation");
        auto request=control.take();check(request&&request->token>native,"custom token is strictly newer than native token");
        auto later=control.reserveWorldOperationToken();check(later>request->token&&!control.canCancel(later)&&control.canCancel(request->token)&&control.claimSetter(request->token,1),"native reservation cannot cancel a custom request or change its setter claim");
        control.close();check(!control.reserveWorldOperationToken()&&!control.canCancel(request->token),"closed backend cannot grant token or cancellation");
    }
    {
        NativeSettingsPackScope scope({1,33,22,7,true,false});bool global{};
        check(!nativeSettingsPreGame(false,33,7),"engine driver composition scope never changes world preGame classification");
        check(withNativeSettingsGlobal(global,22,7,[&]{return global;})&&!global,"permitted composition scope restores native Global policy after return");
    }
}
