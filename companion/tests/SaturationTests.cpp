#include "core/SaturationPolicy.h"
#include <limits>
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool v,char const* m){++checks;if(!v)throw std::runtime_error(m);};
    try {
        SaturationObservation sample{true,true,true,17,0,20,5,5};
        ClientSaturationTracker tracker;
        check(validSaturationObservation(sample),"valid typed client fields accepted");
        for(unsigned i=0;i<200;++i)check(!tracker.observe(sample,7,0,i*100).value,"unchanged default remains Unknown indefinitely");
        sample.current=13.4f;auto value=tracker.observe(sample,7,0,20000);
        check(value.value==13.4f&&value.source==FoodSource::ClientAttribute,"observed update publishes exact client field with honest source");
        sample.current=0;value=tracker.observe(sample,7,0,20100);
        check(value.value==0&&foodText(value,20)=="0.00","real zero after update is a value, never Unknown");
        sample.current=5;check(tracker.observe(sample,7,0,20200).value==5,"return to default after update remains a genuine client read");
        sample.current=20;check(tracker.observe(sample,7,0,20300).value==20,"upper bound preserved without clamp/estimate");
        check(!tracker.observe(sample,8,0,20400).value,"new world generation discards prior update evidence");
        sample.current=19;check(tracker.observe(sample,8,0,20500).value==19,"new world needs its own update");
        check(!tracker.observe(sample,8,1,20600).value,"dimension change invalidates prior value");
        sample.current=18;check(tracker.observe(sample,8,1,20700).value==18,"new dimension update accepted");
        check(!tracker.observe(sample,8,1,22001).value,"stale collection gap resets to Unknown");
        sample.current=17;check(tracker.observe(sample,8,1,22101).value==17,"fresh update after gap accepted");
        check(!tracker.observe(sample,8,1,21000).value,"backwards clock resets evidence");
        sample.current=16;check(tracker.observe(sample,8,1,21100).value==16,"clock reset still needs a new change");
        ++sample.attributeId;check(!tracker.observe(sample,8,1,21200).value,"registered ID replacement discards prior evidence");
        sample.current=15;check(tracker.observe(sample,8,1,21300).value==15,"replacement ID needs its own update");
        tracker.reset();check(!tracker.observe(sample,8,1,21400).value,"disable/quit/reset cannot reuse a prior value");
        for(unsigned field=0;field<4;++field)for(float bad:{-1.f,21.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            auto invalid=sample;
            if(field==0)invalid.minimum=bad;if(field==1)invalid.maximum=bad;
            if(field==2)invalid.defaultValue=bad;if(field==3)invalid.current=bad;
            // Negative max/current/default and oversized min/current/default
            // are invalid; min21 and max21 are rejected by ordering/range too.
            check(!validSaturationObservation(invalid),"invalid finite/range metadata or value rejected");
            tracker.observe(sample,9,0,100);auto changedSample=sample;changedSample.current=14;
            tracker.observe(changedSample,9,0,200);
            check(!tracker.observe(invalid,9,0,300).value,"invalid observation never publishes numeric saturation");
            check(!tracker.observe(sample,9,0,400).value,"invalid input clears previously accepted evidence");
        }
        for(unsigned mode=0;mode<3;++mode){auto missing=sample;
            if(mode==0)missing.present=false;if(mode==1)missing.canonicalIdentity=false;if(mode==2)missing.syncable=false;
            check(!tracker.observe(missing,10,0,100).value,"missing/wrong attribute or unsyncable data stays Unknown");}
        auto firstNondefault=sample;firstNondefault.current=13.4f;tracker.reset();
        check(!tracker.observe(firstNondefault,11,0,100).value,"first nondefault may still be initialization and stays Unknown");
        check(!tracker.observe(firstNondefault,11,0,200).value,"finite nondefault alone is not receipt evidence");
        auto owned=firstNondefault;owned.current=6;value=tracker.observe(owned,11,0,300);owned.current=99;
        check(value.value==6,"publication owns value after source mutation");
        check(foodHudLines(FoodSnapshot{{20.f,FoodSource::ClientAttribute},value,{}})[1]=="Saturation (client): 6.00",
            "actual display labels client state, never authoritative server value");
        std::cout<<"Client saturation policy: "<<checks<<" checks PASS; game receive/currentness NOT_RUN\n";return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
