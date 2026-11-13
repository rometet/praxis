#include "map/DeathHistory.h"
#include <cassert>
#include <cmath>
using namespace praxis::companion::map;
int main() {
    Session world{"local-world", 1, 0, true};
    Session other{"another-world", 2, 0, true};
    DeathHistory h;
    assert(h.entries(world).empty());
    assert(!h.record({}, {1,"death",1,2,3,0}, 5));
    assert(h.record(world, {1,"death",-10.5,64.25,11,0}, 100));
    assert(h.entries(world).size()==1);
    assert(h.entries(world)[0].x==-10.5);
    assert(h.entries(world)[0].unixSeconds==100);
    assert(!h.record(world, {1,"bad",NAN,2,3,0}, 101));
    assert(!h.record(world, {1,"bad",1,2,3,1}, 101));
    assert(h.entries(world).size()==1);
    for (std::size_t i=0;i<DeathHistory::capacity+7;++i)
        assert(h.record(world,{1,"death",double(i),64,3,0},200+i));
    auto entries=h.entries(world);
    assert(entries.size()==DeathHistory::capacity);
    assert(entries.front().x==double(DeathHistory::capacity+6));
    assert(entries.back().x==7.0);
    assert(h.entries(other).empty());
    assert(h.record(other,{1,"death",2,70,5,0},400));
    assert(h.entries(other).size()==1);
    assert(h.entries(world).empty());
    h.clear();
    assert(h.entries(other).empty());
}
