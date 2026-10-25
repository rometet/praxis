#include "core/CoexistencePolicy.h"
#include "core/BlockStateComparePolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    int count=0;
    auto check=[&](bool ok){++count;if(!ok)throw std::runtime_error("Unique feature policy failed");};
    std::array<bool,featureCount> overlap{};
    for(auto feature:lamiumOverlapCandidates) {
        auto id=static_cast<unsigned>(feature);
        check(id<featureCount);check(!overlap[id]);overlap[id]=true;
        check(potentialLamiumOverlap(feature));
        check(mayDisablePraxisOverlap(true,feature,true));
        check(!mayDisablePraxisOverlap(false,feature,true));
        check(!mayDisablePraxisOverlap(true,feature,false));
    }
    for(unsigned i=0;i<featureCount;++i)check(potentialLamiumOverlap(static_cast<Feature>(i))==overlap[i]);
    for(auto f:{Feature::Resource,Feature::RedstoneOverlay,Feature::WeatherChanger,Feature::NoRender})
        check(!mayDisablePraxisOverlap(true,f,true));
    auto a=parseBlockStateSample("minecraft:observer @ 1, 2, 3\n{name:observer,states:{facing_direction:2}}",9,100);
    auto b=parseBlockStateSample("minecraft:observer @ 6, 5, 4\n{name:observer,states:{facing_direction:2}}",9,200);
    auto c=parseBlockStateSample("minecraft:observer @ 6, 5, 4\n{name:observer,states:{facing_direction:3}}",9,201);
    check(a.has_value()&&b.has_value()&&c.has_value());
    check(a->label=="minecraft:observer @ 1, 2, 3");
    check(recentBlockStateSample(*a,9,2600));
    check(!recentBlockStateSample(*a,9,2601));
    check(!recentBlockStateSample(*a,10,110));
    check(!recentBlockStateSample(*a,9,99));
    check(compareBlockStates(&*a,&*b)==BlockStateComparison::Same); // position ignored
    check(compareBlockStates(&*a,&*c)==BlockStateComparison::Different);
    auto worldChange=*a;worldChange.generation++;
    check(compareBlockStates(&worldChange,&*b)==BlockStateComparison::Unavailable);
    check(compareBlockStates(nullptr,&*b)==BlockStateComparison::Unavailable);
    check(!parseBlockStateSample("",9,100));
    check(!parseBlockStateSample("minecraft:stone @ 1, 2, 3",9,100));
    check(!parseBlockStateSample("minecraft:stone\n{states:{}}",9,100));
    check(!parseBlockStateSample(std::string(4097,'a'),9,100));
    std::cout<<"Coexistence & block comparison: "<<count<<" checks PASS\n";
}
