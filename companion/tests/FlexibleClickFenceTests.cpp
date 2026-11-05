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
    // 26.60 can report first=false on the only usable GameMode callback.
    // Physical RMB ownership, not GameMode's first flag, gates the queue.
    fence.clear();
    check(!fence.claim(false,5,1000),"no physical RMB does not authorize placement");
    check(fence.claim(true,5,1001),"first physical press is admitted");
    check(!fence.claim(true,5,1002),"duplicate callback in same press blocked");
    check(!fence.claim(true,5,100000),"long held repeat is still blocked");
    fence.observe(false,5,100001);
    check(fence.claim(true,5,100002),"new click after release is admitted");
    check(!fence.claim(true,5,100003),"second callback in new click blocked");
    check(fence.claim(true,6,100004),"world generation change invalidates ownership");
    check(fence.claim(true,6,999),"monotonic clock rollback re-arms safely");
    fence.clear();
    check(fence.claim(true,6,100000),"explicit reset allows next press");
    fence.accepted({10,20,30},4,6,100000);
    check(fence.blocks({10,20,30},4,6,true,100001),"accepted click remains fenced");
    check(!fence.claim(true,6,100002),"fenced click cannot submit twice");
    fence.observe(false,6,100003);
    check(!fence.blocks({10,20,30},4,6,true,100004),"release clears interactive fence");
    check(fence.claim(true,6,100005),"next press after accepted placement is admitted");
    std::printf("FlexibleClickFence checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}
