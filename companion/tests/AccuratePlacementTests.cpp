#include "core/AccuratePlacementPolicy.h"
#include "core/RuntimePolicy.h"
#include <iostream>
#include <cmath>
using namespace praxis::companion;
int main() {
    unsigned checks{};
    auto check=[&](bool value,char const* name) {
        ++checks;
        if(!value) { std::cerr << "FAILED: " << name << '\n'; throw 1; }
    };
    try {
        check(accurateBlockKind("minecraft:piston")==AccurateBlockKind::Piston,"piston");
        check(accurateBlockKind("minecraft:sticky_piston")==AccurateBlockKind::Piston,"sticky");
        check(accurateBlockKind("minecraft:observer")==AccurateBlockKind::Observer,"observer");
        check(accurateBlockKind("minecraft:dispenser")==AccurateBlockKind::Facing6,"dispenser");
        check(accurateBlockKind("minecraft:dropper")==AccurateBlockKind::Facing6,"dropper");
        for(auto name:{"minecraft:furnace","minecraft:blast_furnace","minecraft:smoker",
                "minecraft:chest","minecraft:trapped_chest","minecraft:ender_chest","minecraft:barrel"})
            check(accurateBlockKind(name)==AccurateBlockKind::HorizontalFacing,"horizontal");
        for(auto name:{"minecraft:stone","minecraft:oak_stairs","minecraft:hopper",
                "minecraft:oak_trapdoor","minecraft:oak_log","minecraft:slab"})
            check(accurateBlockKind(name)==AccurateBlockKind::Unsupported,"unsupported is vanilla");
        for(unsigned f=0;f<6;++f) {
            auto face=static_cast<FlexibleFace>(f);
            for(auto kind:{AccurateBlockKind::Piston,AccurateBlockKind::Observer,AccurateBlockKind::Facing6}) {
                auto into=accurateRotation(kind,face,52.0f,25.0f,true,false);
                auto both=accurateRotation(kind,face,52.0f,25.0f,true,true);
                check(into.valid&&both.valid,"valid all six faces");
                check(into.targetFace==(kind==AccurateBlockKind::Observer
                      ?face:oppositeFlexibleFace(face)),"Into matches intended block facing");
                check(both.targetFace==oppositeFlexibleFace(into.targetFace),"Into Reverse inversion");
                check(std::isfinite(into.yaw)&&std::fabs(into.pitch)<=90.f,"safe vertical angles");
                check(std::isfinite(both.yaw)&&std::fabs(both.pitch)<=90.f,"safe reverse angles");
                if(isFlexibleHorizontal(face))
                    check(into.pitch==0.f,"horizontal aim neutral pitch");
            }
            auto horizontal=accurateRotation(AccurateBlockKind::HorizontalFacing,face,90.f,25.f,true,false);
            check(horizontal.valid==isFlexibleHorizontal(face),"no vertical Into on horizontal-only block");
        }
        auto const floorPiston=accurateRotation(
            AccurateBlockKind::Piston,FlexibleFace::Up,0.f,65.f,true,false);
        auto const floorSticky=accurateRotation(
            AccurateBlockKind::Piston,FlexibleFace::Up,70.f,35.f,true,true);
        auto const ceilingPiston=accurateRotation(
            AccurateBlockKind::Piston,FlexibleFace::Down,0.f,-50.f,true,false);
        auto const floorObserver=accurateRotation(
            AccurateBlockKind::Observer,FlexibleFace::Up,0.f,65.f,true,false);
        check(floorPiston.valid && floorPiston.targetFace==FlexibleFace::Down,
            "REGRESSION piston placed on floor with Into must face DOWN");
        check(floorSticky.valid && floorSticky.targetFace==FlexibleFace::Up,
            "REGRESSION floor piston Into+Reverse faces UP");
        check(ceilingPiston.valid && ceilingPiston.targetFace==FlexibleFace::Up,
            "piston Into on ceiling faces UP");
        check(floorObserver.valid && floorObserver.targetFace==FlexibleFace::Up,
            "observer sensor faces UP and its output points DOWN into floor");
        auto rev=accurateRotation(AccurateBlockKind::Piston,FlexibleFace::Up,90.f,45.f,false,true);
        check(rev.valid&&rev.yaw==-90.f&&rev.pitch==-45.f,"Reverse flips actual player view");
        check(!accurateRotation(AccurateBlockKind::Piston,FlexibleFace::Up,90.f,45.f,false,false).valid,
            "no key never intercepts vanilla");
        check(!accurateRotation(AccurateBlockKind::Unsupported,FlexibleFace::Up,90.f,45.f,true,false).valid,
            "unsupported never intercepts vanilla");
        check(!accurateRotation(AccurateBlockKind::Observer,FlexibleFace::Up,NAN,0,true,false).valid,
            "invalid yaw fails closed");
        check(!accurateRotation(AccurateBlockKind::Observer,FlexibleFace::Up,0,NAN,true,false).valid,
            "invalid pitch fails closed");
        check(!accurateRotation(AccurateBlockKind::Observer,FlexibleFace::Up,0,100,true,false).valid,
            "oversize pitch fails closed");
        RuntimeConfig cfg;
        check(cfg.keybinds.size()==31,"slot 28 retained with appended FastClick slots 29/30");
        check(cfg.accuratePlacementOptions.intoKey=='I'
              && cfg.accuratePlacementOptions.reverseKey=='O',"action key defaults");
        check(validRuntimeConfig(cfg),"old config still valid");
        cfg.accurateBlockPlacement=true;
        auto publication=runtimeSettings(cfg,kAllRenderPasses,featureRegistryBit(28));
        check(publication.on(Feature::AccurateBlockPlacement),"feature published separately");
        check(!publication.on(Feature::FlexibleBlockPlacement),"Flexible off stays off");
        cfg.accuratePlacementOptions.intoKey=0x79;
        check(!validRuntimeConfig(cfg),"reserved hotkey rejected");
        cfg.accuratePlacementOptions.intoKey='I';
        cfg.accuratePlacementOptions.intoModifiers=9;
        check(!validRuntimeConfig(cfg),"invalid modifier rejected");
        std::cout << "Accurate placement policy: " << checks << " PASS\n";
        return 0;
    } catch (...) {return 1;}
}
