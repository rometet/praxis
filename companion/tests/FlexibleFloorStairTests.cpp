#include "core/FlexibleStairPlacementPolicy.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
using namespace praxis::companion;
namespace {
int checks{}, failures{};
void check(bool ok, char const* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n",name); } }
bool same(FlexiblePlacementPlan const& a,FlexiblePlacementPlan const& b) {
    return a.valid==b.valid && a.originalSupport==b.originalSupport && a.destination==b.destination
        && a.useSupport==b.useSupport && a.originalFace==b.originalFace
        && a.placementFace==b.placementFace && a.regionDirection==b.regionDirection
        && a.region==b.region && a.modes==b.modes
        && a.desiredYaw==b.desiredYaw && a.desiredPitch==b.desiredPitch;
}
}
int main() {
    std::array<FlexibleUV,5> const regions{{{.5f,.5f},{.1f,.5f},{.9f,.5f},{.5f,.9f},{.5f,.1f}}};
    std::array<FlexibleCell,3> const floors{{{320,100,300},{-57,-30,-112},{-12000,70,21000}}};
    int baselineWrongSupport{};
    for (auto floor:floors) for(unsigned facing=0;facing<4;++facing) {
        auto const view=static_cast<FlexibleHorizontal>(facing);std::set<float> yaws;
        for(auto uv:regions) {
            auto const before=planFlexiblePlacement(floor,FlexibleFace::Up,uv,FlexibleRotation,view);
            if(before.useSupport!=floor || before.placementFace!=FlexibleFace::Up) ++baselineWrongSupport;
            auto const p=planFlexiblePlacementForItem(floor,FlexibleFace::Up,uv,FlexibleRotation,view,true);
            check(p.valid,"floor plan valid");
            check(p.destination==FlexibleCell{floor.x,floor.y+1,floor.z},"destination stays above clicked floor");
            check(p.useSupport==floor,"no sideways or ceiling support requested");
            check(p.placementFace==FlexibleFace::Up,"ordinary stairs use upward floor face");
            check(p.desiredYaw==before.desiredYaw,"sector-selected yaw retained independently");
            auto const raw=flexibleRawUVFromOriented(uv,FlexibleFace::Up,view);
            auto const hit=flexibleRelativeHit(p.placementFace,raw);
            check(hit.y==1.f && hit.x>=0.f && hit.x<=1.f && hit.z>=0.f && hit.z<=1.f,"native click remains on floor top");
            FlexibleAdmission a{true,true,true,true,false,false};
            check(admitFlexiblePlacement(p,a)==FlexibleAdmissionReason::Accepted,"floor-only world admits the stair");
            a.destinationEmpty=false;
            check(admitFlexiblePlacement(p,a)==FlexibleAdmissionReason::DestinationOccupied,"occupied target stays blocked");
            a.destinationEmpty=true;a.supportPresent=false;
            check(admitFlexiblePlacement(p,a)==FlexibleAdmissionReason::MissingSupport,"no floor stays blocked");
            a.supportPresent=true;a.withinReach=false;
            check(admitFlexiblePlacement(p,a)==FlexibleAdmissionReason::OutOfReach,"out of reach stays blocked");
            yaws.insert(p.desiredYaw);
        }
        check(yaws.size()==4,"all four horizontal directions available without turning camera cardinal direction");
    }
    check(baselineWrongSupport==60,"baseline reproduces wrong support on every floor rotation sector");
    for(unsigned face=0;face<6;++face) for(unsigned modes=0;modes<8;++modes)
        for(auto uv:regions) for(unsigned facing=0;facing<4;++facing) {
            auto const f=static_cast<FlexibleFace>(face);auto const v=static_cast<FlexibleHorizontal>(facing);
            auto const old=planFlexiblePlacement({100,90,-100},f,uv,modes,v);
            auto const nonStair=planFlexiblePlacementForItem({100,90,-100},f,uv,modes,v,false);
            check(same(old,nonStair),"non-stair plans including trapdoors unchanged");
            if(f!=FlexibleFace::Up || modes!=FlexibleRotation)
                check(same(old,planFlexiblePlacementForItem({100,90,-100},f,uv,modes,v,true)),
                    "wall/ceiling/unmodified/offset/adjacent stair semantics retained");
        }
    auto invalid=planFlexiblePlacementForItem({0,0,0},FlexibleFace::Up,
        {std::numeric_limits<float>::quiet_NaN(),.5f},FlexibleRotation,FlexibleHorizontal::South,true);
    check(!invalid.valid,"invalid hit never revived by stair override");
    auto boundary=planFlexiblePlacementForItem({0,kFlexibleMaxY,0},FlexibleFace::Up,{.5f,.5f},
        FlexibleRotation,FlexibleHorizontal::South,true);
    check(!boundary.valid,"world boundary remains invalid");
    std::printf("Floor stair checks: %d; failures: %d; baseline wrong-support cases: %d\n",checks,failures,baselineWrongSupport);
    return failures ? 1 : 0;
}
