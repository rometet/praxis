#pragma once
#include "resourcepacks/WorldNativeApplicationReceipt.h"

template<class Check> void worldNativeApplicationReceiptCases(Check&& check){
    using R=praxis::companion::packs::WorldNativeApplicationReceipt;
    for(unsigned failure=0;failure!=8;++failure){
        unsigned predicates{},attachments{},lastStage{};
        bool accepted=praxis::companion::packs::registerWorldApplicationAdapter([&](unsigned stage){
            ++predicates;lastStage=stage;return failure!=stage;
        },[&](unsigned stage){++attachments;lastStage=stage;return failure!=stage-16;});
        check(accepted==(failure==0),"adapter fails closed at every version/owner/base/typed-registration failure");
        if(failure==0)check(predicates==3&&attachments==4&&lastStage==23,"all fixed SDK hooks registered once in order");
        else if(failure<4)check(predicates==failure&&!attachments&&lastStage==failure,"binding precondition failure never registers native hooks");
        else check(predicates==3&&attachments==failure-3&&lastStage==failure+16,"partial registration stops immediately at failing typed hook");
    }
    R::Identity id{101,1,7,9,11,22,33};
    auto prepare=[&](R& r){check(r.prepare(id),"API receipt prepares exact operation");};
    auto enter=[&](R& r,std::uint64_t tick=1){auto p=r.enterTick(id,44,55,tick);check(bool(p),"API receipt binds actual Applying controller/scene/model");return p;};
    auto callback=[&](R& r,R::TickPermit const& p){check(r.callbackEntered(p),"native callback admitted once in its actual tick");check(r.callbackReturned(p,true),"native callback normal return recorded");};
    auto complete=[&](R& r){prepare(r);auto p=enter(r);callback(r,p);check(r.loadingQueried(p,33,true),"native tick consumes same-model completed query");check(r.tickReturned(p,true,true,true),"native callback/query/scene-exit join completes application");};
    {
        R r;prepare(r);check(!r.prepare(id),"duplicate application initialization rejected");
        auto p=enter(r);check(!r.loadingQueried(p,33,true),"old loading flag before callback does not count");
        check(!r.tickReturned(p,true,true,false)&&!r.applicationReady(),"loading flag alone never completes application");
        p=enter(r,2);callback(r,p);check(!r.tickReturned(p,true,true,true)&&r.sealed(),"screen exit without its actual post-callback query seals");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);r.loadingQueried(p,33,true);
        check(!r.tickReturned(p,true,true,false)&&!r.applicationReady(),"completed loading without native screen exit remains pending");
        p=enter(r,2);check(r.loadingQueried(p,33,false),"next native tick replaces prior query observation");
        check(!r.tickReturned(p,true,true,true)&&r.sealed(),"early close while loading is not completion");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);
        check(r.loadingQueried(p,33,false),"in-progress native query observed");
        check(!r.tickReturned(p,true,true,false)&&!r.sealed(),"heavy pack can remain loading without premature success");
        p=enter(r,2);r.loadingQueried(p,33,true);check(r.tickReturned(p,true,true,true),"later same lifecycle native completion succeeds");
        check(!r.loadingQueried(p,33,true),"query outside the ended tick is ignored");
    }
    {
        R r;prepare(r);auto p=enter(r);check(r.callbackEntered(p),"exception fixture callback entered");
        check(!r.callbackReturned(p,false)&&r.sealed(),"native callback exception seals application");
        check(!r.beginFrame(11,22,7,66,1),"exception never produces a frame permit");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);
        check(!r.callbackEntered(p)&&r.sealed(),"duplicate current callback cannot become success");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);
        check(!r.loadingQueried(p,34,true),"unrelated model's native loading query ignored");
        check(!r.tickReturned(p,true,true,true)&&r.sealed(),"foreign query plus screen close cannot grant completion");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);r.loadingQueried(p,33,true);
        check(!r.tickReturned(p,false,true,true)&&r.sealed(),"world/client/owner teardown seals despite completed query");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);r.loadingQueried(p,33,true);
        check(!r.tickReturned(p,true,false,true)&&r.sealed(),"native Applying tick exception never grants completion");
    }
    {
        R old;prepare(old);auto oldPermit=enter(old);
        R fresh;auto next=id;next.token=102;check(fresh.prepare(next),"cancel/reentry creates a fresh operation identity");
        auto p=fresh.enterTick(next,44,56,2);check(bool(p),"new scene admitted for fresh operation");
        check(!fresh.callbackEntered(oldPermit)&&!fresh.loadingQueried(oldPermit,33,true)
            &&!fresh.tickReturned(oldPermit,true,true,true)&&!fresh.sealed(),"stale old scene/query/return does not seal new operation");
        check(fresh.callbackEntered(p)&&fresh.callbackReturned(p,true),"new callback remains usable after stale traffic");
        fresh.loadingQueried(p,33,true);check(fresh.tickReturned(p,true,true,true),"new operation completes after stale traffic");
    }
    for(unsigned field=0;field<7;++field){
        R r;prepare(r);auto wrong=id;
        if(field==0)++wrong.token;else if(field==1)++wrong.attempt;else if(field==2)++wrong.world;
        else if(field==3)++wrong.thread;else if(field==4)++wrong.game;else if(field==5)++wrong.client;else ++wrong.model;
        check(!r.enterTick(wrong,44,55,1)&&!r.sealed(),"foreign operation/attempt/world/thread/game/client/model does not attach");
        auto p=enter(r);callback(r,p);r.loadingQueried(p,33,true);
        check(r.tickReturned(p,true,true,true),"correct operation survives foreign identity attempts");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);r.loadingQueried(p,33,false);r.tickReturned(p,true,true,false);
        check(!r.enterTick(id,44,56,2)&&r.sealed(),"same operation cannot silently rebind a different Applying scene");
    }
    {
        R r;prepare(r);auto p=enter(r);
        check(!r.enterTick(id,44,55,2)&&r.sealed(),"overlapping current ticks are rejected");
    }
    {
        R r;prepare(r);auto p=enter(r);callback(r,p);r.loadingQueried(p,33,true);
        check(!r.beginFrame(11,22,7,66,1),"frame started before native scene exit is ineligible");
        r.tickReturned(p,true,true,true);
        auto f=r.beginFrame(11,22,7,66,2);check(bool(f),"later current-renderer frame admitted");
        check(!r.frameReturned(f,11,22,7,66,true)&&!r.frameReady()&&!r.sealed(),"frame that skips actual world draw waits");
        f=r.beginFrame(11,22,7,66,3);check(bool(f)&&r.levelRendered(f,66,true),"next real level draw associated with same frame");
        check(r.frameReturned(f,11,22,7,66,true)&&r.frameReady(),"normal game/level draw completes current frame receipt");
        r.requireCurrentFrame(77);check(!r.frameReady(),"renderer exchange withdraws old ready frame");
        f=r.beginFrame(11,22,7,77,4);check(bool(f)&&r.levelRendered(f,77,true)&&r.frameReturned(f,11,22,7,77,true),"replacement renderer gets its own later draw receipt");
    }
    {
        R r;complete(r);auto f=r.beginFrame(11,22,7,66,1);r.levelRendered(f,66,true);
        check(!r.frameReturned(f,11,22,8,66,true)&&r.sealed(),"world swap after frame entry seals receipt");
    }
    {
        R r;complete(r);auto f=r.beginFrame(11,22,7,66,1);
        check(!r.levelRendered(f,66,false)&&r.sealed(),"native level draw exception seals receipt");
    }
    {
        R r;complete(r);auto f=r.beginFrame(11,22,7,66,1);
        check(!r.frameReturned(f,11,22,7,66,false)&&r.sealed(),"game frame exception seals receipt");
    }
}
