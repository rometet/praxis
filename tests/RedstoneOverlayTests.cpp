#include "core/RedstoneOverlayPolicy.h"
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;
int main() {
    int checks{};
    auto require=[&](bool condition,char const* message){
        ++checks;if(!condition)throw std::runtime_error(message);
    };
    try {
        require(isRedstoneDust("minecraft:redstone_wire"),"namespaced wire classification");
        require(isRedstoneDust("redstone_wire"),"plain wire classification");
        require(!isRedstoneDust("minecraft:repeater"),"repeater not labelled as dust");
        require(!isRedstoneDust("minecraft:redstone_block"),"power block not labelled as dust");
        require(isRedstoneComparator("minecraft:powered_comparator"),"powered comparator classification");
        require(isRedstoneComparator("minecraft:unpowered_comparator"),"unpowered comparator classification");
        require(!isRedstoneComparator("minecraft:repeater"),"repeater not labelled as comparator");
        require(validRedstonePower(0),"wire power zero");
        require(validRedstonePower(15),"wire power fifteen");
        require(!validRedstonePower(-1)&&!validRedstonePower(16),"invalid power rejected");
        RedstoneOverlayOptions options;
        RedstoneSample before{-17,64,-1,0};
        RedstoneSample after=before;after.power=15;
        require(before!=after,"live power mutation changes cached sample");
        require(redstoneInRange(before,-17,64,-1,options),"current position kept");
        require(redstoneInRange(before,-1,68,15,options),"inclusive range kept");
        require(!redstoneInRange(before,0,64,-1,options),"horizontal prune");
        require(!redstoneInRange(before,-17,69,-1,options),"vertical prune");
        std::cout<<"Redstone overlay checks: "<<checks<<" PASS\n";
        return 0;
    } catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
