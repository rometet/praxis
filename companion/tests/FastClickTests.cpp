#include "core/FastClickPolicy.h"
#include "core/FastBlockPlacementPolicy.h"
#include "core/FastRightItemPolicy.h"
#include "core/RuntimePolicy.h"
#include <iostream>
using namespace praxis::companion;
int main() {
    unsigned n{};
    auto test=[&](bool v,char const* label){++n;if(!v){std::cerr<<"FAIL "<<label<<"\n";throw 1;}};
    try {
        test(!fastRightExtraUseSupported("minecraft:experience_bottle"),"XP-bottle no-op guarded");
        test(fastRightExtraUseSupported("minecraft:bone_meal"),"bone meal remains eligible");
        test(fastRightExtraUseSupported("minecraft:egg"),"other item behavior preserved");
        test(fastPlacementSafeNetworkScope(false,false),"FBP local single-player permitted");
        test(!fastPlacementSafeNetworkScope(true,false),"FBP remote dedicated multiplayer veto");
        test(!fastPlacementSafeNetworkScope(false,true),"FBP session multiplayer veto");
        test(!fastPlacementSafeNetworkScope(true,true),"FBP multiplayer signals combined veto");
        test(fastPlacementCompetingBlockRepeat(false,true,true),
            "offline FastRightClick advanced mode owns placement");
        test(!fastPlacementCompetingBlockRepeat(false,true,false),
            "offline no advanced mode does not conflict");
        test(!fastPlacementCompetingBlockRepeat(false,false,true),
            "offline disabled FastRightClick cannot conflict");
        test(!fastPlacementCompetingBlockRepeat(true,true,true),
            "BDS FastRightClick advanced checkbox cannot veto native FBP");
        test(!fastPlacementCompetingBlockRepeat(true,false,true),
            "BDS fast block trial independent from disabled FastRightClick");
        FastPlacementRemoteFence remoteFence{};
        test(!remoteFence.reserve(0,1,0,0,0,1),"remote no generation");
        test(!remoteFence.reserve(1,0,0,0,0,1),"remote no native tick");
        test(remoteFence.reserve(1,10,1,2,3,1),"remote first native placement");
        test(!remoteFence.reserve(1,10,2,2,3,1),"remote same tick blocked");
        test(!remoteFence.reserve(1,11,2,2,3,1),"remote 1 tick blocked");
        test(!remoteFence.reserve(1,12,2,2,3,1),"remote 2 ticks blocked");
        test(!remoteFence.reserve(1,13,1,2,3,1),"remote same support face blocked");
        test(remoteFence.reserve(1,13,2,2,3,1),"remote distinct support after 3 ticks");
        test(!remoteFence.reserve(1,13,2,2,4,1),"remote repeated tick blocked");
        test(remoteFence.reserve(2,1,2,2,3,1),"remote new world resets");
        remoteFence.reset();
        test(remoteFence.reserve(3,1,2,2,3,1),"remote manual reset");
        test(fastPlacementEligibleHeld("minecraft:stone","",false,true,false),
            "remote native block item with null mBlock but block planter accepted");
        test(fastPlacementEligibleHeld("minecraft:unusual_item_name","minecraft:stone",true,false,false),
            "remote native mBlock name resolves valid cube");
        test(!fastPlacementEligibleHeld("minecraft:stone","",false,false,false),
            "nonblock item cannot masquerade as a block");
        test(!fastPlacementEligibleHeld("minecraft:stone","minecraft:stone",true,true,true),
            "empty stack excluded");
        test(fastPlacementFullCube("minecraft:gray_concrete"),"FBP gray concrete full cube");
        test(fastPlacementFullCube("minecraft:dark_oak_planks"),"FBP dark oak plank");
        test(fastPlacementFullCube("minecraft:black_wool"),"FBP wool full cube");
        test(!fastPlacementFullCube("minecraft:stone_brick_stairs"),"FBP stairs not accelerated");
        test(!fastPlacementFullCube("minecraft:stone_brick_wall"),"FBP walls not accelerated");
        test(!fastPlacementFullCube("minecraft:oak_log"),"FBP oriented logs excluded");
        test(!fastPlacementFullCube("minecraft:observer"),"FBP machine excluded");
        test(!fastPlacementFullCube("minecraft:white_glazed_terracotta"),"FBP oriented glazed terracotta excluded");
        test(!fastPlacementFullCube("minecraft:blue_concrete_powder"),"FBP gravity concrete powder excluded");
        test(!fastPlacementFullCube("minecraft:basalt"),"FBP oriented basalt excluded");
        test(fastPlacementFullCube("minecraft:iron_block"),"BDS tested held iron block admitted");
        test(fastPlacementEligibleHeld("minecraft:iron_block","minecraft:iron_block",true,false,false),
            "BDS item admission accepts the exact tested iron block");
        test(fastPlacementFullCube("minecraft:gold_block"),"BDS gold storage block admitted");
        test(fastPlacementFullCube("minecraft:diamond_block"),"BDS diamond storage block admitted");
        test(fastPlacementFullCube("minecraft:netherite_block"),"BDS netherite storage block admitted");
        test(fastPlacementFullCube("minecraft:raw_copper_block"),"BDS raw copper cube admitted");
        test(fastPlacementFullCube("minecraft:deepslate_iron_ore"),"BDS deep ore cube admitted");
        test(!fastPlacementFullCube("minecraft:command_block"),"remote command block not admitted");
        test(!fastPlacementFullCube("minecraft:structure_block"),"remote structure block not admitted");
        test(!fastPlacementFullCube("minecraft:hay_block"),"remote oriented hay excluded");
        test(!fastPlacementFullCube("minecraft:bone_block"),"remote oriented bone excluded");
        test(!fastPlacementFullCube("minecraft:honey_block"),"remote honey excluded");
        test(!fastPlacementFullCube("minecraft:piston"),"remote piston excluded");
        test(fastPlacementFullCube("minecraft:stone"), "FBP stone eligible");
        test(fastPlacementFullCube("minecraft:glass"), "FBP glass eligible");
        test(!fastPlacementFullCube("minecraft:piston"), "FBP piston excluded");
        test(!fastPlacementFullCube("minecraft:oak_stairs"), "FBP stairs excluded");
        test(!fastPlacementFullCube("minecraft:oak_slab"), "FBP slab excluded");
        test(!fastPlacementFullCube("minecraft:oak_trapdoor"), "FBP trapdoor excluded");
        FastPlacementAdmission fbp{true,true,true,true,true,true,true,false,false,false};
        test(fastPlacementAdmitted(fbp), "FBP fullcube native admitted");
        auto denied=fbp;denied.creative=false;
        test(!fastPlacementAdmitted(denied), "FBP survival not yet proven");
        denied=fbp;denied.modifierHeld=true;
        test(!fastPlacementAdmitted(denied), "FBP Flexible placement isolated");
        denied=fbp;denied.competing=true;
        test(!fastPlacementAdmitted(denied), "FBP conflicting owner isolated");
        denied=fbp;denied.interactive=true;
        test(!fastPlacementAdmitted(denied), "FBP interactive support isolated");
        denied=fbp;denied.held=false;
        test(!fastPlacementAdmitted(denied), "FBP physical mouse required");
        FastPlacementBudget fbpBudget{};
        test(fbpBudget.consume(1,2), "FBP first tick native attempt");
        test(fbpBudget.consume(1,2), "FBP second tick native attempt within cap");
        test(!fbpBudget.consume(1,2), "FBP max2 never exceeded");
        test(fbpBudget.consume(1,3), "FBP new native tick reopens budget");
        fbpBudget.reset();
        test(!fbpBudget.consume(1,0), "FBP no zero tick");
        FastClickOptions def{};
        test(validFastClickOptions(def),"default valid");
        test(def.clicksPerTick==1&&def.tickInterval==1,"left click conservative");
        test(validFastClickOptions(FastClickOptions{64,1},64),"right 64 is accepted");
        test(!validFastClickOptions(FastClickOptions{65,1},64),"right 65 is rejected");
        test(!validFastClickOptions(FastClickOptions{9,1},8),"left over eight is rejected");
        test(FastClickCadence{}.due(1,1,true,1,64)==0,"first click remains vanilla at max");
        for(int c=-1;c<=66;++c)for(int i=-1;i<=12;++i) {
            FastClickOptions cfg{c,i,false,false};
            test(validFastClickOptions(cfg,64)==(c>=1&&c<=64&&i>=1&&i<=10),
                "right range 1-64 and tick interval 1-10");
            test(validFastClickOptions(cfg,8)==(c>=1&&c<=8&&i>=1&&i<=10),
                "left range remains 1-8");
        }
        FastClickGate gate{true,true,true,true,true,false,false,false,true,false,false,false};
        gate.itemUsable=true;
        test(fastClickAdmitted(FastClickDecision::RepeatRight,gate),"nonblock item on target allowed");
        auto blockItem=gate;blockItem.blockItem=true;
        test(!fastClickAdmitted(FastClickDecision::RepeatRight,blockItem),"block placement excluded by default");
        blockItem.allowBlockPlacement=true;
        test(fastClickAdmitted(FastClickDecision::RepeatRight,blockItem),"block placement requires explicit opt-in");
        auto airItem=gate;airItem.blockHit=false;
        test(fastClickAdmitted(FastClickDecision::RepeatRight,airItem),"nonblock item air use");
        airItem.airUseAllowed=false;
        test(!fastClickAdmitted(FastClickDecision::RepeatRight,airItem),"air use optional");
        auto holdUse=gate;holdUse.longUseAction=true;
        test(!fastClickAdmitted(FastClickDecision::RepeatRight,holdUse),"long-use charging / food protected");
        test(fastClickAdmitted(FastClickDecision::RepeatLeft,gate),"creative break allowed");
        for(int idx=0;idx<9;++idx){
            auto g=gate;
            switch(idx){
            case 0:g.featureEnabled=false;break;
            case 1:g.gameplay=false;break;
            case 2:g.focused=false;break;
            case 3:g.held=false;break;
            case 4:g.blockItem=true;break;
            case 5:g.itemUsable=false;break;
            case 6:g.interactive=true;break;
            case 7:g.placementOwnerBusy=true;break;
            case 8:g.protectedTool=true;break;
            }
            test(!fastClickAdmitted(FastClickDecision::RepeatRight,g),"right safe gate");
        }
        auto interactive=gate;interactive.interactive=true;interactive.allowInteractive=true;
        test(fastClickAdmitted(FastClickDecision::RepeatRight,interactive),"interactive opt-in");
        auto survival=gate;survival.creative=false;
        test(!fastClickAdmitted(FastClickDecision::RepeatLeft,survival),"survival vanilla");
        survival.allowSurvival=true;
        test(fastClickAdmitted(FastClickDecision::RepeatLeft,survival),"survival opt-in");
        test(!fastClickAdmitted(FastClickDecision::NoAction,gate),"no action");
        for(unsigned interval=1;interval<=10;++interval) {
            FastClickCadence c{};
            test(c.due(3,1,true,interval,8)==0,"first click vanilla");
            for(std::uint64_t tick=2;tick<42;++tick) {
                test(c.due(3,tick,true,interval,8)==((tick-1)%interval==0?8u:0u),"tick throttle");
                test(c.due(3,tick,true,interval,8)==0,"same tick dedup");
            }
            test(c.due(3,42,false,interval,8)==0,"released");
            test(c.due(3,43,true,interval,8)==0,"new press");
            test(c.due(4,44,true,interval,8)==0,"world change");
        }
        {
            FastClickCadence c;
            test(c.due(10,100,true,1,64)==0,"first 64 click tick is vanilla");
            test(c.due(10,101,true,1,64)==64,"64 extra native uses per tick");
            test(c.due(10,102,true,1,65)==64,"native burst clamp is 64");
            test(c.due(10,102,true,1,64)==0,"same tick stays deduplicated");
        }
        RuntimeConfig config{};
        test(featureCount==31,"31 persisted feature slots");
        test(!config.fastRightClick&&!config.fastLeftClick,"default off");
        test(config.fastRightClickOptions.clicksPerTick==8,"new right default eight");
        test(config.fastLeftClickOptions.clicksPerTick==1,"left default preserved");
        test(validRuntimeConfig(config),"default valid runtime config");
        config.fastLeftClickOptions.clicksPerTick=9;
        test(!validRuntimeConfig(config),"bad options rejected");
        config.fastLeftClickOptions.clicksPerTick=4;
        config.fastRightClickOptions.clicksPerTick=64;
        test(validRuntimeConfig(config),"high-throughput right config accepted");
        config.fastRightClickOptions.clicksPerTick=65;
        test(!validRuntimeConfig(config),"high-throughput right overflow rejected");
        config.fastRightClickOptions.clicksPerTick=16;
        config.fastRightClick=true;config.fastLeftClick=true;
        auto s=runtimeSettings(config,kAllRenderPasses,featureRegistryBit(29)|featureRegistryBit(30));
        test(s.on(Feature::FastRightClick)&&s.on(Feature::FastLeftClick),"separate module gates");
        test(!s.on(Feature::FlexibleBlockPlacement)&&!s.on(Feature::AccurateBlockPlacement),"other placement modes unchanged");
        std::cout<<"Fast clicks: "<<n<<" PASS\n";
        return 0;
    } catch(...) {return 1;}
}
