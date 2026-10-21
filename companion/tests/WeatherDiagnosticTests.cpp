#include "core/WeatherChangerPolicy.h"
#include "core/WeatherChangerDiagnostics.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool value, char const* text) { ++checks; if (!value) throw std::runtime_error(text); }
template<class T> struct Storage { T value{}; T& get() { return value; } };
struct View { struct Data { bool mDrawWeather=true, mIsShadowPass=false; }; struct Sky { float mFogLevel=.7f; };
    Storage<Data> mViewData; Storage<Sky> mSkyState; };
struct Rain { Storage<std::vector<int>> mRainSplashParticles{{1,2}}; Storage<std::vector<int>> mSmokeParticles{{3}}; };
void publish(bool enabled) { auto s=std::make_shared<Settings>(); s->enabled[8]=enabled; publishedSettings.store(s); }
WeatherDiagnosticResult completed() { WeatherDiagnosticResult r; r.decision=WeatherDecision::Applied; r.applied=true; r.mutationVerified=true; return r; }
void sessionContracts() {
    WeatherDiagnosticSession s;
    auto const t=WeatherDiagnosticSession::Clock::time_point{};
    check(!s.accepting()&&!s.pending()&&!s.begin(WeatherProducer::View,t),"default OFF");
    for (unsigned first=0; first<=8; ++first) {
        std::array<bool,8> fields{}; for(unsigned i=0;i<first;++i)fields[i]=true;
        auto context=weatherDiagnosticContextResult(fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7]);
        check(static_cast<unsigned>(context)==(first==8?0:first+1),"actual diagnostic context helper retains first rejection priority");
    }
    s.start(t);
    WeatherDiagnosticState off; off.ready=off.admission=off.capability=true;
    check(weatherDiagnosticOffNativePass(off,off,true,true,true,false),"eligible OFF native passthrough");
    check(!weatherDiagnosticOffNativePass(off,off,true,true,true,true),"OFF snapshots around transient ON application never count restoration");
    check(!weatherDiagnosticOffNativePass(off,off,true,true,false,false),"retired generation cannot count OFF passthrough");
    auto on=off;on.setting=true;check(!weatherDiagnosticOffNativePass(on,off,true,true,true,false),"ON to OFF transition classified separately");
    auto ended=off;ended.ready=false;check(!weatherDiagnosticOffNativePass(off,ended,true,true,true,false),"lifecycle end cannot count OFF restoration");
    auto p=s.poll(true,off,15,t);
    check(p.started&&p.started->session==1&&p.started->hookMask==15&&!p.finished,"start once with typed hook mask");
    check(!s.poll(true,off,15,t).started,"no repeated start log");
    auto a=s.begin(WeatherProducer::View,t);
    WeatherDiagnosticEntry e; e.state=off; e.eligible=e.playing=e.hasWeather=true; e.scope=WeatherDecision::SettingOff;
    s.entry(a,e);
    auto r=completed(); r.applied=false; r.mutationVerified=false; r.offNativePass=true; r.decision=WeatherDecision::ScopeRejected;
    s.complete(a,r);
    check(!s.begin(WeatherProducer::View,t+weatherDiagnosticDuration),"deadline rejects boundary entry");
    p=s.poll(true,off,15,t+weatherDiagnosticDuration);
    check(p.finished&&p.finished->end==WeatherSessionEnd::Deadline&&p.finished->events==1,"deadline terminal report");
    check(p.finished->requestMismatch==2&&p.finished->requestedOn==2&&p.finished->publishedOn==0,"request/publication mismatch independent from calls");
    auto const& c=p.finished->producers[0];
    check(c.calls==1&&c.completed==1&&c.offNativePass==1&&c.applied==0&&c.scopes[5]==1,"OFF passthrough counted separately");
    check(!s.pending()&&!s.poll(false,{},0,t).finished,"completion report consumed once");
    s.start(t); auto late=s.begin(WeatherProducer::Rain,t); s.stop();
    check(!s.poll(false,{},15,t).finished&&!s.accepting(),"stop drains an in-flight scalar ticket");
    s.start(t); auto fresh=s.begin(WeatherProducer::Rain,t);
    s.complete(late,completed()); s.complete(fresh,completed()); s.stop(); p=s.poll(false,{},15,t);
    check(p.finished&&p.finished->events==1&&p.finished->inFlight==0&&p.finished->producers[1].completed==1,"old ticket cannot cross explicit restart");
    s.start(t);
    for(unsigned i=0;i<weatherDiagnosticEventLimit;++i){auto x=s.begin(WeatherProducer::Rain,t);check(static_cast<bool>(x),"budget admits exactly bounded events");auto big=completed();big.removed=weatherDiagnosticAmountLimit;s.complete(x,big);}
    check(!s.begin(WeatherProducer::Rain,t),"event cap rejects excess"); p=s.poll(false,{},15,t);
    check(p.finished&&p.finished->events==4096&&p.finished->end==WeatherSessionEnd::EventLimit,"fixed call limit");
    check(p.finished->producers[1].removed==weatherDiagnosticAmountLimit&&p.finished->amountSaturations==4095,"scalar amount saturation");
    s.start(t); std::vector<std::thread> threads;
    for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned j=0;j<1000;++j){auto x=s.begin(WeatherProducer::View,t);if(x)s.complete(x,completed());}});
    for(auto& thread:threads)thread.join(); p=s.poll(false,{},15,t);
    check(p.finished&&p.finished->events==4096&&p.finished->inFlight==0&&p.finished->producers[0].completed==4096,"concurrent native boundaries share exact cap");
    s.start(t); for(unsigned i=0;i<5000;++i)s.poll(false,{},15,t);s.stop();p=s.poll(false,{},15,t);
    check(p.finished&&p.finished->serviceSamples==4096&&p.finished->events==0,"service sampling bounded and zero-call result retained");
}
void productionBoundaryContracts() {
    runtimeReady=true; weatherNativeAdmission=true; worldGeneration=7; publish(true);
    WeatherDecision reason{}; unsigned originCalls{};
    for(bool diagnostics:{false,true}) {
        View v; Rain rain;
        VisualWeatherScope scope(true,true,7,diagnostics?&reason:nullptr);
        ++originCalls;
        check(applyWeatherView(v,diagnostics?&reason:nullptr),"same owned-view write with diagnostics ON/OFF");
        check(!v.mViewData.value.mDrawWeather&&v.mSkyState.value.mFogLevel==0.f,"owned view mutation readback");
        if(diagnostics)check(reason==WeatherDecision::Applied,"actual view outcome observed");
        check(suppressOwnedRainSplashes(rain,diagnostics?&reason:nullptr)==2&&rain.mSmokeParticles.value==std::vector<int>{3},"splash-only write unchanged");
        if(diagnostics)check(reason==WeatherDecision::Applied,"actual rain outcome observed");
    }
    check(originCalls==2,"diagnostic observers add no producer invocation");
    { VisualWeatherScope scope(true,true); View v; v.mViewData.value.mIsShadowPass=true;
      check(!applyWeatherView(v,&reason)&&reason==WeatherDecision::Shadow&&v.mViewData.value.mDrawWeather,"shadow rejection records actual branch"); }
    { VisualWeatherScope scope(true,false);View v;Rain r;
      check(!applyWeatherView(v,&reason)&&reason==WeatherDecision::NoWeather,"has-weather rejection");
      check(suppressOwnedRainSplashes(r,&reason)==0&&reason==WeatherDecision::NoWeather,"rain has-weather rejection"); }
    { VisualWeatherScope scope(true,true);worldGeneration=8;View v;Rain r;
      check(!applyWeatherView(v,&reason)&&reason==WeatherDecision::GenerationChanged,"world change after origin");
      check(suppressOwnedRainSplashes(r,&reason)==0&&reason==WeatherDecision::GenerationChanged,"rain world change after origin"); }
    for(auto expected:{WeatherDecision::SettingOff,WeatherDecision::RuntimeNotReady,WeatherDecision::AdmissionClosed}){
      publish(true);runtimeReady=true;weatherNativeAdmission=true;
      {VisualWeatherScope scope(true,true);if(expected==WeatherDecision::SettingOff)publish(false);if(expected==WeatherDecision::RuntimeNotReady)runtimeReady=false;if(expected==WeatherDecision::AdmissionClosed)weatherNativeAdmission=false;View v;Rain r;
       check(!applyWeatherView(v,&reason)&&reason==expected&&v.mViewData.value.mDrawWeather,"post-origin gate records exact denying read");
       check(suppressOwnedRainSplashes(r,&reason)==0&&reason==expected&&r.mRainSplashParticles.value.size()==2,"rain post-origin denying read");}
      VisualWeatherScope rejected(true,true,worldGeneration.load(),&reason);
      check(reason==expected&&!visualWeatherContext.eligible,"scope admission outcome distinguished from context rejection");
    }
    runtimeReady=true;weatherNativeAdmission=true;publish(true);
    {VisualWeatherScope rejected(false,true,worldGeneration.load(),&reason);View v;check(reason==WeatherDecision::ScopeRejected&&!applyWeatherView(v,&reason)&&reason==WeatherDecision::ScopeRejected,"primary exclusion is fail-closed");}
    weatherDiagnosticSession.start();
    try{WeatherDiagnosticCapture c(WeatherProducer::View);throw 1;}catch(int){}
    weatherDiagnosticSession.stop();auto p=weatherDiagnosticSession.poll(false,{},15);
    check(p.finished&&p.finished->producers[0].decisions[static_cast<unsigned>(WeatherDecision::OriginIncomplete)]==1&&p.finished->inFlight==0,"exception drains owned diagnostic ticket");
    check(!weatherDiagnosticSession.accepting(),"lifecycle expiry remains OFF");
    resetWeatherChangerDiagnostics();check(weatherDiagnosticSession.accepting(),"existing explicit reset starts one short session");
    weatherDiagnosticSession.stop();weatherDiagnosticSession.poll(false,{},15);
}
}
int main(){try{sessionContracts();productionBoundaryContracts();std::cout<<"Weather diagnostic production boundary/session: "<<checks<<" checks PASS; native callbacks/visuals NOT_RUN\n";return 0;}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
