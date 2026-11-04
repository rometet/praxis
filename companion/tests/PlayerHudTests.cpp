#include "core/PlayerHudPolicy.h"
#include <limits>
#include <set>
#include <tuple>
#include <stdexcept>
#include <iostream>
using namespace praxis::companion;
int main(){
    unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("Player HUD policy failed");};
    try{
        check(validPlayerHudOptions({}));PlayerHudOptions o;o.offhandScale=0;check(!validPlayerHudOptions(o));o={};o.foodX=std::numeric_limits<float>::quiet_NaN();check(!validPlayerHudOptions(o));
        check(!exactFoodValue({5.f,FoodSource::Unknown},20));
        check(foodText({0.f,FoodSource::Unknown},20)=="Unknown");
        check(foodText({0.f,FoodSource::ServerObserved},20)=="0.00");
        check(!exactFoodValue({21.f,FoodSource::ServerObserved},20));
        check(!exactFoodValue({std::numeric_limits<float>::infinity(),FoodSource::AuthoritativeLocal},20));
        check(validHitboxOptions({}));HitboxOptions h;h.maxActors=257;check(!validHitboxOptions(h));
        HitboxBounds b{{-1,2,3},{0,4,5}};check(validHitboxBounds(b));
        auto edges=hitboxEdges(b);std::set<std::tuple<float,float,float,float,float,float>> unique;
        for(auto const& [a,z]:edges){check((a.x!=z.x)+(a.y!=z.y)+(a.z!=z.z)==1);unique.emplace(a.x,a.y,a.z,z.x,z.y,z.z);}
        check(unique.size()==12);b.max=b.min;check(!validHitboxBounds(b));
        std::cout<<checks<<" food unknown/real zero, invalid values, hitbox budget and edge checks PASS\n";return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
