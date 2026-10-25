#include "core/FlexiblePlacementClickFence.h"
#include <cstdio>
using namespace praxis::companion;
int main() {
    int checks=0, failures=0;
    auto check=[&](bool ok, char const* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL: %s\n",message); }
    };
    FlexiblePlacementClickFence fence;
    check(!fence.blocks({1,2,3},4,5,true,1000),"inactive fence permits interaction");
    fence.accepted({1,2,3},4,5,1000);
    check(fence.blocks({1,2,3},4,5,true,1001),"same held click is blocked");
    check(fence.blocks({1,2,3},4,5,true,1002),"all repeated callbacks stay blocked");
    check(!fence.blocks({1,2,4},4,5,true,1003),"different cell allowed");
    check(!fence.blocks({1,2,3},8,5,true,1004),"different actor allowed");
    check(fence.blocks({1,2,3},4,5,true,1500),"holding beyond half a second stays protected");
    check(fence.blocks({1,2,3},4,5,true,61000),"long holds cannot toggle the new block later");
    fence.accepted({1,2,3},4,5,1000);
    check(!fence.blocks({1,2,3},4,5,false,1001),"mouse release clears fence");
    check(!fence.blocks({1,2,3},4,5,true,1002),"subsequent deliberate click allowed");
    fence.accepted({1,2,3},4,5,1000);
    check(!fence.blocks({1,2,3},4,6,true,1001),"new world generation clears fence");
    fence.accepted({1,2,3},4,5,1000);
    check(!fence.blocks({1,2,3},4,5,true,999),"clock rollback clears fence");
    fence.accepted({1,2,3},4,5,1000);
    fence.clear();
    check(!fence.blocks({1,2,3},4,5,true,1001),"explicit shutdown clears fence");
    fence.accepted({-200,101,-100},4,5,1000);
    check(fence.blocks({-200,101,-100},4,5,true,1001),"negative coordinates exact match");
    fence.accepted({10,20,30},4,5,1000);
    check(!fence.blocks({-200,101,-100},4,5,true,1001),"new accepted placement replaces old fence");
    check(fence.blocks({10,20,30},4,5,true,1001),"new accepted placement protected");
    std::printf("FlexibleClickFence checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}
