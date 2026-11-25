#include "core/TabTpsSourcePort.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace praxis::companion::tabtps::source;

int main(){
    unsigned n{};
    auto check=[&](bool v,char const* label){++n;if(!v)throw std::runtime_error(label);};
    auto sample=[](){
        Snapshot s{};s.epoch=7;s.sequence=8;s.serverMonotonicMs=9000;
        s.tpsCenti=2000;s.msptUs=4250;s.pingMs=38;
        s.cpuSystemCenti=1832;s.cpuProcessCenti=785;
        s.memoryUsedMiB=512;s.memoryCommittedMiB=1024;s.memoryMaxMiB=4096;
        s.onlinePlayers=3;s.maxPlayers=20;return s;
    };
    auto s=sample();
    check(valid(s),"base packet valid");
    auto wire=encode(s);check(wire.starts_with("PTPS2|"),"wire schema");
    check(decode(wire).has_value() && *decode(wire)==s,"strict roundtrip");
    check(!decode("PTPS1|7|8|9000").has_value(),"reject v1");
    check(!decode(wire+"|14"),"reject fields");
    check(!decode(wire.substr(0,wire.size()-1)+"x"),"reject junk suffix");
    check(!decode(std::string(257,'x')),"reject max size");
    check(plain(render(s,{Module::Tps,Module::Mspt}))=="TPS: 20.00 MSPT: 4.25",
       "original default footer exact text");
    check(plain(render(s,{Module::Tps,Module::Mspt,Module::Ping}))
       =="TPS: 20.00 MSPT: 4.25 Ping: 38ms","action/boss default");
    check(plain(render(s,{Module::Cpu}))=="CPU: 18.32%, 7.85% (sys., proc.)",
       "CPU source formatting");
    check(plain(render(s,{Module::Memory}))=="RAM: 512M/1024M (max. 4096M)",
       "memory max source formatting");
    s.memoryCommittedMiB=4096;
    check(plain(render(s,{Module::Memory}))=="RAM: 512M/4096M",
       "memory max omitted when equal");
    s=sample();
    check(plain(render(s,{Module::Players}))=="Online: 3/20","online");
    check(plain(render(s,{})).empty(),"empty header has no separator");
    check(plain(render(s,{Module::Players,Module::Tps}," | "))=="Online: 3/20 | TPS: 20.00",
       "custom separator only once");
    std::vector<Module> parsed;
    check(modules(" tps, ,mspt, cpu ",parsed) && parsed.size()==3
        && parsed[0]==Module::Tps && parsed[2]==Module::Cpu,"configured module order");
    check(!modules("tps,invalid",parsed),"unknown module rejected");
    const std::array<std::pair<Module,double>,13> thresholds{{
      {Module::Tps,20.0},{Module::Tps,18.5},{Module::Tps,18.49},
      {Module::Tps,15.01},{Module::Tps,15.0},
      {Module::Mspt,25.0},{Module::Mspt,25.01},{Module::Mspt,40.0},
      {Module::Mspt,40.01},
      {Module::Ping,99},{Module::Ping,100},{Module::Ping,249},{Module::Ping,250}
    }};
    const std::array<Grade,13> expected{{
      Grade::Good,Grade::Good,Grade::Medium,Grade::Medium,Grade::Low,
      Grade::Good,Grade::Medium,Grade::Medium,Grade::Low,
      Grade::Good,Grade::Medium,Grade::Medium,Grade::Low
    }};
    for(std::size_t i=0;i<thresholds.size();++i)
        check(grade(thresholds[i].first,thresholds[i].second)==expected[i],
              "source gradient boundary");
    check(gradient(green,darkGreen,0,4)==green,"gradient first char");
    check(gradient(green,darkGreen,3,4)!=darkGreen,"last char never end color");
    auto pixels=render(s,{Module::Tps});
    check(pixels[0].rgb==gray&&pixels[1].rgb==gray&&pixels[2].rgb==gray,
       "label gray");
    check(pixels[3].rgb==white,"colon white");
    check(pixels[5].rgb==green,"number first char green");
    s.tpsCenti=1850;s.msptUs=25000;
    check(bossGrade(s,Fill::Tps)==Grade::Medium,"boss 18.5 yellow but text good");
    check(bossGrade(s,Fill::Mspt)==Grade::Medium,"boss 25 yellow but text good");
    s.msptUs=40000;
    check(bossGrade(s,Fill::Mspt)==Grade::Low,"boss 40 red but text medium");
    s.msptUs=10000;
    auto b=bossProgress(s,Fill::Mspt);
    check(b&&std::fabs(*b-0.2f)<0.00001,"mspt10 is 20 percent fill");
    check(bossProgress(s,Fill::ReverseMspt) &&
        std::fabs(*bossProgress(s,Fill::ReverseMspt)-0.8f)<0.00001,"reverse");
    s.msptUs=-1;
    check(!bossProgress(s,Fill::Mspt),"unknown mspt not fabricated");
    s=sample();s.pingMs=-1;s.cpuSystemCenti=-1;s.memoryUsedMiB=-1;
    check(plain(render(s,{Module::Ping,Module::Cpu,Module::Memory}))
        =="Ping: -- CPU: -- RAM: --","unknown metrics not zero");
    check(!valid(Snapshot{}),"invalid empty measurement");
    check(!valid(Snapshot{1,1,1,2200}),"tps out of bounds");
    s=sample();s.onlinePlayers=21;check(!valid(s),"online > max rejected");
    Receiver receiver;receiver.reset(12);
    s=sample();check(receiver.accept(s,12,1000),"first update");
    check(!receiver.accept(s,12,2000),"duplicate");
    check(receiver.fresh(12,4000).has_value(),"fresh 3s inclusive");
    check(!receiver.fresh(12,4001),"stale after 3s");
    receiver.reset(13);check(!receiver.fresh(12,3000),"disconnect clear");
    check(!receiver.accept(s,12,4000),"wrong generation");
    receiver.reset(13);s.sequence++;s.serverMonotonicMs++;
    check(receiver.accept(s,13,5000),"new session first");
    RollingTps60s roll;roll.reset(1000);
    for(std::uint64_t t=1050;t<=61000;t+=50)roll.complete(t);
    check(roll.centi(60999)==absent,"no fabricated warm start");
    check(roll.centi(61000)==2000,"60 second 20TPS");
    roll.reset(1000);
    for(std::uint64_t t=1100;t<=61000;t+=100)roll.complete(t);
    check(roll.centi(61000)==1000,"60 second measured 10TPS");
    // Original TabTPS: 20-tick groups, 60 weighted slots, no Java warm-start.
    RollingTpsOriginal1m sourceRolling;
    sourceRolling.reset(1000);
    for(std::uint64_t t=1050;t<=61000;t+=50)sourceRolling.complete(t);
    check(sourceRolling.centi(61000)==absent,"no warm-start before 60 full samples");
    sourceRolling.complete(61050);
    for(std::uint64_t t=61100;t<=62000;t+=50)sourceRolling.complete(t);
    check(sourceRolling.centi(62000)==2000,"source weighted 60 20-tick samples");
    sourceRolling.reset(1000);
    for(std::uint64_t t=1100;t<=123000;t+=100)sourceRolling.complete(t);
    check(sourceRolling.centi(123000)==1000,"20tick-weighted 10TPS");
    TickTimeAverage ms;for(int i=0;i<20;++i)ms.complete(i%2?2000:1000);
    check(ms.meanUs()==1500,"recent mspt average independent of TPS");
    std::cout<<"TabTPS source-port acceptance checks: "<<n<<" PASS\n";
}
