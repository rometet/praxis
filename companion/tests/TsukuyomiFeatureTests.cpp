#include "core/AutoToolPolicy.h"
#include "core/EffectTimerPolicy.h"
#include "core/AngleSnapPolicy.h"
#include "core/ToggleSneakSprintPolicy.h"
#include "core/DebugScreenPolicy.h"
#include "core/ItemScrollerPolicy.h"
#include "core/PlayerHudPolicy.h"
#include "core/RuntimePolicy.h"
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;

int main() {
    int checks = 0;
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) throw std::runtime_error(message);
    };
    try {
        check(static_cast<unsigned>(Feature::AutoTool) == 15, "AutoTool uses appended persisted slot 15");
        check(static_cast<unsigned>(Feature::EffectTimer) == 16, "EffectTimer slot 16");
        check(static_cast<unsigned>(Feature::AngleSnap) == 17, "AngleSnap slot 17");
        check(static_cast<unsigned>(Feature::ToggleSneakSprint) == 18, "ToggleSneakSprint slot 18");
        check(static_cast<unsigned>(Feature::DebugScreen) == 19, "DebugScreen slot 19");
        check(static_cast<unsigned>(Feature::InventoryHUD) == 23, "retired InventoryHUD slot 23 untouched");
        check(!(ownedFeatureSlotMask & featureRegistryBit(23)), "retired 23 is never owned");
        for (unsigned slot : {15u, 16u, 17u, 18u, 19u})
            check((ownedFeatureSlotMask & featureRegistryBit(slot)) != 0, "new feature slot is owned");
        check(std::string_view(featureNames[15]) == "AutoTool", "stable persisted feature name");

        AutoToolOptions autoOptions;
        std::array<MiningCandidate, 9> tools{};
        tools[0] = {1.0f, 100, 100, false, false};
        tools[1] = {8.0f, 80, 100, false, false};
        tools[2] = {8.0f, 10, 100, true, false};
        check(chooseMiningTool(tools, 0, autoOptions) == 1, "fastest non-protected tool chosen");
        autoOptions.requireImprovement = false;
        check(chooseMiningTool(tools, 1, autoOptions) == 1, "current best tool retained");
        autoOptions.skipLowDurability = false;
        check(chooseMiningTool(tools, 0, autoOptions) == 1, "tie prefers remaining durability over slot 2");
        autoOptions.skipLowDurability = true;
        tools[1].protectedItem = true;
        check(chooseMiningTool(tools, 0, autoOptions) == 0, "no eligible better tool keeps current slot");
        autoOptions.cooldownMs = 5000;
        check(!validAutoToolOptions(autoOptions), "out-of-range cooldown rejected");
        MiningSwitchState mining;
        MiningTargetKey target{1, 64, 2, 0, 1};
        check(mining.observe(target, 100, 250), "first target observation is a switch edge");
        check(!mining.observe(target, 200, 250), "same target never turns into repeated switching");
        check(mining.observe({1, 64, 3, 0, 1}, 1000, 250), "new target after cooldown is a new edge");

        check(effectDurationText(1200) == "1:00", "effect duration uses real ticks");
        check(effectDurationText(-1) == "∞", "infinite effect duration retained");
        EffectTimerOptions effectOptions;
        std::vector<EffectEntry> effects{{"Speed", 0, 200, false}, {"Poison", 1, 100, false}, {"Bad", 0, 0, false}};
        auto lines = effectTimerLines(effects, effectOptions);
        check(lines.size() == 2 && lines[0] == "Poison 2: 0:05" && lines[1] == "Speed: 0:10",
            "sorted newest-first by remaining ticks and amplifier shown");
        effectOptions.compact = true;
        effectOptions.showAmplifier = false;
        lines = effectTimerLines(effects, effectOptions);
        check(lines[0] == "Poison 0:05", "compact format omits extra label");
        effectOptions.x = -1;
        check(!validEffectTimerOptions(effectOptions), "negative effect HUD position rejected");

        AngleSnapOptions angle;
        check(validAngleSnapOptions(angle), "default angle options valid");
        short dx = 5, dy = 7;
        angle.lockYaw = true;
        applyAngleLocks(dx, dy, angle);
        check(dx == 0 && dy == 7, "public writable mouse channel locks yaw only");
        angle.lockYaw = false;
        angle.lockPitch = true;
        dx = 5; dy = 7;
        applyAngleLocks(dx, dy, angle);
        check(dx == 5 && dy == 0, "pitch lock only");
        angle.yawStep = 0;
        check(!validAngleSnapOptions(angle), "zero snap step rejected");

        ToggleSneakSprintOptions toggle;
        check(validToggleSneakSprintOptions(toggle), "default toggle keys valid");
        toggle.sprintToggleKey = toggle.sneakToggleKey;
        toggle.sprintModifiers = toggle.sneakModifiers;
        check(!validToggleSneakSprintOptions(toggle), "identical toggle chords rejected");
        ToggleStates states;
        states.sneak = states.sprint = true;
        states.manualOverride(true, false);
        check(!states.sneak && states.sprint, "manual sneak press returns sneak control to player");
        check(states.consumeKeyEdge('V', true, 1000), "first Down toggles on");
        check(!states.consumeKeyEdge('V', true, 1100), "repeated Down is ignored");
        check(!states.consumeKeyEdge('V', false, 1120), "KeyUp is not a toggle");
        check(!states.consumeKeyEdge('V', true, 1140), "spurious early Down is debounced");
        check(!states.consumeKeyEdge('V', false, 1180), "second KeyUp releases the latch");
        check(states.consumeKeyEdge('V', true, 1400), "next physical press toggles");
        states.releaseIfPhysicallyUp('V', false);
        check(states.consumeKeyEdge('V', true, 1700), "physical key-up recovers missed KeyUp");
        states.clear();
        check(states.consumeKeyEdge('V', true, 2000), "world/GUI reset clears key latch");

        FoodSnapshot food;
        food.hunger = {10.0f, FoodSource::ClientAttribute};
        food.saturation = {2.0f, FoodSource::ClientAttribute};
        food.held = {true, "Bread", 5, 0.6f, false};
        auto restoration = heldFoodRestoration(food, food.held);
        check(restoration && restoration->hunger == 5, "held food hunger restoration from SDK nutrition");
        check(restoration && std::abs(restoration->saturation - 5.0f) < 0.001f, "saturation restoration bounded by hunger gain");
        food.held.present = false;
        check(!heldFoodRestoration(food, food.held), "unknown held food never invents restoration");
        auto foodLines = foodHudLines(food);
        check(foodLines[2] == "Exhaustion: Unknown", "unknown exhaustion stays explicit");

        DebugSnapshot debug;
        debug.serverRateKnown = false;
        debug.serverMetricsAvailable = false;
        DebugScreenOptions debugOptions;
        auto debugLines = debugScreenLines(debug, debugOptions);
        bool sawUnavailable = false;
        for (auto const& line : debugLines) if (line.find("Unavailable") != std::string::npos) sawUnavailable = true;
        check(sawUnavailable, "unavailable debug metrics are explicit");
        debug.serverRateKnown = true;
        debug.observedServerTps = 19.5;
        debug.observedServerMspt = 1000.0 / 19.5;
        debug.serverMetricsAvailable = true;
        debugLines = debugScreenLines(debug, debugOptions);
        bool sawRate = false;
        for (auto const& line : debugLines) if (line.starts_with("Observed server rate: 19.5")) sawRate = true;
        check(sawRate, "sampled server rate is shown as observed");
        auto columns=javaDebugColumns(debug,debugOptions,"10, 64, 20","South (0, 0)",144.0);
        check(!columns.left.empty() && columns.left.front().starts_with("Minecraft Bedrock"),
              "F3 left panel carries Minecraft heading");
        check(!columns.right.empty() && columns.right.front().starts_with("Praxis"),
              "F3 right panel carries diagnostics heading");
        bool worldCoords=false, honestTickInterval=false, unknownServerMspt=false;
        for(auto const& line:columns.left)
            if(line.starts_with("XYZ: 10, 64, 20")) worldCoords=true;
        for(auto const& line:columns.right) {
            if(line.starts_with("Observed tick interval:")) honestTickInterval=true;
            if(line=="Actual server MSPT: Unavailable") unknownServerMspt=true;
        }
        check(worldCoords, "F3 left panel shows SDK-backed world coordinates");
        check(honestTickInterval, "observed tick latency is not mislabelled as actual MSPT");
        check(unknownServerMspt, "unavailable real server MSPT is explicit");
        debugOptions.serverPerformance=false;
        columns=javaDebugColumns(debug,debugOptions,"","",0.0);
        bool hiddenServerRows=true;
        for(auto const& line:columns.right)if(line.starts_with("Server tick:"))hiddenServerRows=false;
        check(hiddenServerRows,"F3 server section can be disabled");

        RecipeFavorite recipe;
        recipe.name = "Torch";
        recipe.result = "minecraft:torch";
        recipe.grid[0] = "minecraft:coal";
        recipe.grid[4] = "minecraft:stick";
        std::array<std::string, 36> playerItems{};
        std::array<int, 36> playerCounts{};
        std::array<std::string, recipeGridSize> gridItems{};
        std::array<bool, recipeGridSize> gridLocked{};
        playerItems[9] = "minecraft:coal"; playerCounts[9] = 4;
        playerItems[10] = "minecraft:stick"; playerCounts[10] = 8;
        auto moves = planRecipeApply(recipe, playerItems, playerCounts, gridItems, gridLocked, 18);
        check(moves.size() == 2 && moves[0].gridSlot == 0 && moves[1].gridSlot == 4,
            "recipe plan maps matching inventory items into grid slots");
        gridItems[0] = "minecraft:dirt";
        check(planRecipeApply(recipe, playerItems, playerCounts, gridItems, gridLocked, 18).empty(),
            "conflicting occupied grid slot fails closed");
        gridItems[0].clear();
        gridLocked[4] = true;
        check(planRecipeApply(recipe, playerItems, playerCounts, gridItems, gridLocked, 18).empty(),
            "locked grid slot fails closed");
        playerCounts[9] = 0;
        gridLocked[4] = false;
        check(planRecipeApply(recipe, playerItems, playerCounts, gridItems, gridLocked, 18).empty(),
            "missing ingredient fails the whole plan");
        ConfirmationState confirmation;
        check(!confirmation.press(1000) && confirmation.armed, "first apply press arms confirmation");
        check(confirmation.press(2000), "second apply press within window executes");
        check(!confirmation.press(10000) && confirmation.armed, "expired confirmation needs a new first press");

        RuntimeConfig config;
        check(validRuntimeConfig(config), "default config with new options valid");
        config.autoTool = true; config.effectTimer = true; config.angleSnap = true;
        config.toggleSneakSprint = true; config.debugScreen = true;
        auto settings = runtimeSettings(config, 0, ownedFeatureSlotMask);
        check(settings.on(Feature::AutoTool) && settings.on(Feature::EffectTimer)
            && settings.on(Feature::AngleSnap) && settings.on(Feature::ToggleSneakSprint)
            && settings.on(Feature::DebugScreen), "available feature slots publish enabled modules");
        check(!settings.on(Feature::InventoryHUD), "retired slot never publishes enabled state");
        config.toggleSneakSprintOptions.sneakToggleKey = config.toggleSneakSprintOptions.sprintToggleKey;
        check(!validRuntimeConfig(config), "invalid toggle chord rejected by config validation");
        std::cout << "Tsukuyomi feature policies: " << checks << " checks PASS; game runtime NOT_RUN\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
