// Host tests for the BEI core-layer port (LGPLv3, ported from
// MiracleForest/BarelyEnoughItems @ 410ecfdeb794dd1b7d7844c0e3ea256091b069c8
// under external/bei/source, license bundle in ../../licenses/BarelyEnoughItems/).
// Pure C++ logic only: no LeviLamina, no game process, no network.
#include "core/search/SearchEngine.h"
#include "core/recipe/RecipeIndex.h"
#include "core/model/ItemKey.h"
#include "actions/TransferPlanner.h"

#include <iostream>
#include <stdexcept>

using namespace bei::core;
using namespace bei::actions;

namespace {

std::vector<ItemEntry> sampleItems() {
    std::vector<ItemEntry> items;
    {
        ItemEntry e;
        e.key.id = "minecraft:oak_planks";
        e.displayName = "オークの板";
        e.englishName = "Oak Planks";
        e.tags = {"minecraft:planks"};
        items.push_back(std::move(e));
    }
    {
        ItemEntry e;
        e.key.id = "minecraft:birch_planks";
        e.displayName = "シラカバの板";
        e.englishName = "Birch Planks";
        e.tags = {"minecraft:planks"};
        items.push_back(std::move(e));
    }
    {
        ItemEntry e;
        e.key.id = "minecraft:stick";
        e.displayName = "棒";
        e.englishName = "Stick";
        items.push_back(std::move(e));
    }
    {
        ItemEntry e;
        e.key.id = "minecraft:crafting_table";
        e.displayName = "作業台";
        e.englishName = "Crafting Table";
        items.push_back(std::move(e));
    }
    return items;
}

// TransferPlanner helpers, mirroring upstream tests/actions/TransferPlannerTest.cpp.
IngredientView any(char const* id) { return {{ItemKey{id, ItemKey::ANY_AUX}}, 1}; }
IngredientView exact(char const* id, std::int16_t aux = 0) { return {{ItemKey{id, aux}}, 1}; }

RecipeView shaped(int w, int h, std::vector<IngredientView> inputs) {
    RecipeView r;
    r.category = category::CRAFTING;
    r.width = w;
    r.height = h;
    r.inputs = std::move(inputs);
    return r;
}

RecipeView shapeless(std::vector<IngredientView> inputs) {
    RecipeView r;
    r.category = category::CRAFTING;
    r.shapeless = true;
    r.inputs = std::move(inputs);
    return r;
}

} // namespace

int main() {
    unsigned checks = 0;
    auto require = [&](bool good, char const* why) {
        ++checks;
        if (!good) throw std::runtime_error(why);
    };
    try {
        // --- ItemKey ---
        {
            auto k = ItemKey::parse("minecraft:oak_planks");
            require(k && k->id == "minecraft:oak_planks" && k->aux == 0, "parse plain id");
            require(k->nameSpace() == "minecraft" && k->path() == "oak_planks", "ns/path split");
            auto kAux = ItemKey::parse("minecraft:stone:3");
            require(kAux && kAux->aux == 3, "parse aux");
            auto kw = ItemKey::parse("minecraft:stone:*");
            require(kw && kw->isWildcard(), "parse wildcard");
            require(!ItemKey::parse("minecraft:::").has_value(), "malformed rejected");
            ItemKey wild{"minecraft:planks", ItemKey::ANY_AUX};
            require(wild.matches(ItemKey{"minecraft:planks", 2}), "wildcard aux matches");
            require(!ItemKey{"minecraft:planks", 1}.matches(ItemKey{"minecraft:planks", 2}), "aux mismatch");
        }

        // --- SearchEngine ---
        {
            auto items = sampleItems();
            SearchEngine engine;
            engine.setItems(&items);

            auto ids = [&](std::string_view q) {
                std::vector<std::string> out;
                for (auto i : engine.filter(q)) out.push_back(items[i].key.id);
                return out;
            };

            require(engine.filter("").size() == 4, "empty query returns all");
            require(engine.filter("   ").size() == 4, "blank query returns all");

            // AND of terms across tags/ids/names, with display-name (Japanese) matching.
            auto planks = ids("plank");
            require(planks.size() == 2, "plank matches both by id path");
            require(ids("板").size() == 2, "display name term 板 matches both planks");
            auto onlyOak = ids("板 -birch");
            require(onlyOak.size() == 1 && onlyOak[0] == "minecraft:oak_planks",
                    "display name term (板) with negation");
            require(ids("棒").size() == 1 && ids("棒")[0] == "minecraft:stick", "display name term 棒");

            // OR alternative.
            auto stickOrBirch = ids("stick|birch_planks");
            require(stickOrBirch.size() == 2, "alternatives OR");

            // Namespace prefix.
            require(ids("@birch_planks").empty(), "unknown namespace prefix no match");
            require(ids("@minecraft stick").size() == 1, "namespace+text");

            // Tag prefix.
            require(ids("#planks").size() == 2, "tag substring");

            // Id prefix.
            require(ids("$oak_").size() == 1, "id substring");

            // Pastable full id.
            require(ids("minecraft:stick").size() == 1, "full id term");

            // Quoted literal with space.
            {
                ItemEntry e;
                e.key.id = "minecraft:wool_white";
                e.displayName = "White Wool";
                e.englishName = "White Wool";
                items.push_back(std::move(e));
                engine.setItems(&items);
            }
            require(ids("\"white wool\"").size() == 1 && ids("white wool").size() == 1,
                    "quoted literal same as raw for ascii");

            // Case and diacritic insensitivity via folded English names.
            {
                ItemEntry e;
                e.key.id = "minecraft:cafe_sign";
                e.displayName = "Café Sign";
                e.englishName = "Café Sign";
                items.push_back(std::move(e));
                engine.setItems(&items);
            }
            require(ids("cafe").size() == 1, "diacritic folding");

            // Narrowing cache: results stay consistent as query grows.
            require(ids("plank").size() == 2 && ids("plank #minecraft:planks").size() == 2,
                    "narrowed query consistent");
            require(engine.lastScanCount() <= items.size(), "bounded scans");

            // Direct match on one entry.
            require(engine.matches(items[0], "oak"), "matches(entry, query)");
            require(!engine.matches(items[2], "oak"), "non-match");
        }

        // --- RecipeIndex ---
        {
            CategoryInfo crafting;
            crafting.id = category::CRAFTING;
            crafting.titleKey = "bei.category.crafting";
            crafting.fallbackTitle = "Crafting";
            crafting.layout = LayoutKind::CraftingGrid;
            crafting.workstations = {ItemKey{"minecraft:crafting_table", ItemKey::ANY_AUX}};
            crafting.order = 10;

            CategoryInfo furnace;
            furnace.id = category::FURNACE;
            furnace.titleKey = "bei.category.furnace";
            furnace.fallbackTitle = "Furnace";
            furnace.layout = LayoutKind::Smelting;
            furnace.workstations = {ItemKey{"minecraft:furnace", ItemKey::ANY_AUX}};
            furnace.order = 20;

            auto logs = ItemKey{"minecraft:oak_log", 0};

            RecipeView planks;
            planks.id = "oak_planks";
            planks.category = category::CRAFTING;
            planks.width = 1;
            planks.height = 1;
            planks.inputs = {IngredientView{{logs}, 1}};
            planks.outputs = {IngredientView{{ItemKey{"minecraft:oak_planks", 0}}, 4}};

            RecipeView sticks;
            sticks.id = "sticks";
            sticks.category = category::CRAFTING;
            sticks.width = 1;
            sticks.height = 2;
            sticks.inputs = {IngredientView{{ItemKey{"minecraft:oak_planks", 0}}, 1},
                             IngredientView{{ItemKey{"minecraft:oak_planks", 0}}, 1}};
            sticks.outputs = {IngredientView{{ItemKey{"minecraft:stick", 0}}, 4}};

            RecipeView smelt;
            smelt.id = "charcoal";
            smelt.category = category::FURNACE;
            smelt.inputs = {IngredientView{{logs}, 1}};
            smelt.outputs = {IngredientView{{ItemKey{"minecraft:charcoal", 0}}, 1}};

            RecipeIndex index;
            index.build({crafting, furnace}, {planks, sticks, smelt});
            require(index.size() == 3, "recipes stored");

            // Recipes(what produces oak_planks)
            auto recipesOfPlanks = index.lookup(ItemKey{"minecraft:oak_planks", 0}, LookupMode::Recipes);
            require(recipesOfPlanks.size() == 1 && recipesOfPlanks[0].category == category::CRAFTING,
                    "recipes: planks produced by crafting");
            require(index.recipesInCategory(recipesOfPlanks[0].category).size() == 2,
                    "category contains 2 crafting recipes");

            // Uses(what consumes oak_planks)
            auto usesOfPlanks = index.lookup(ItemKey{"minecraft:oak_planks", 0}, LookupMode::Uses);
            require(index.has(ItemKey{"minecraft:oak_planks", 0}, LookupMode::Uses), "uses has()");
            std::size_t usesTotal = 0;
            for (auto const& g : usesOfPlanks) usesTotal += g.recipes.size();
            require(usesTotal == 1, "uses: sticks only (logs recipe excluded)");

            // Workstation categories list ALL their recipes in Uses (crafting_table).
            auto usesOfTable = index.lookup(ItemKey{"minecraft:crafting_table", ItemKey::ANY_AUX}, LookupMode::Uses);
            require(index.has(ItemKey{"minecraft:crafting_table", ItemKey::ANY_AUX}, LookupMode::Uses),
                    "workstation has() in uses mode");
            require(usesOfTable.size() == 1 && usesOfTable[0].category == category::CRAFTING
                        && usesOfTable[0].recipes.size() == 2,
                    "workstation category lists all its recipes in uses");

            // oak_log is an input of both planks (crafting) and smelt (furnace).
            auto usesOfLogs = index.lookup(logs, LookupMode::Uses);
            std::size_t logsUses = 0;
            for (auto const& g : usesOfLogs) logsUses += g.recipes.size();
            require(logsUses == 2, "logs used by planks(crafting) and smelt(furnace)");

            // Wildcard aux matching.
            require(index.has(ItemKey{"minecraft:oak_planks", ItemKey::ANY_AUX}, LookupMode::Recipes),
                    "wildcard output lookup");
            require(!index.has(ItemKey{"minecraft:oak_log", ItemKey::ANY_AUX}, LookupMode::Recipes), "input-only item has no output recipe");
            require(index.has(ItemKey{"minecraft:oak_log", ItemKey::ANY_AUX}, LookupMode::Uses), "wildcard input lookup");
            require(!index.has(ItemKey{"minecraft:diamond", 0}, LookupMode::Recipes), "unrelated no recipes");

            // Duplicate recipe ids collapse (first wins).
            RecipeView dup = planks;
            dup.outputs[0].alternatives[0].aux = 1;
            RecipeIndex dupIdx;
            dupIdx.build({crafting}, {planks, dup});
            require(dupIdx.size() == 1, "duplicate recipe id dropped");

            // Duplicate categories merge workstations.
            CategoryInfo crafting2 = crafting;
            crafting2.workstations = {ItemKey{"minecraft:crafting_table", 1}};
            RecipeIndex merged;
            merged.build({crafting, crafting2}, {});
            require(merged.categories().size() == 1 && merged.categories()[0].workstations.size() == 2,
                    "duplicate category merged with extra workstations");

            require(index.lookupCount(ItemKey{"minecraft:stick", 0}, LookupMode::Recipes) == 1,
                    "lookupCount recipes for stick");
            require(index.lookupCount(ItemKey{"minecraft:oak_planks", 0}, LookupMode::Uses) == 1,
                    "lookupCount uses for planks");
            require(index.findCategory(category::CRAFTING) != nullptr, "findCategory");
            require(index.categorySize(category::FURNACE) == 1, "categorySize");
        }

        // --- TransferPlanner (bei::actions, game-independent) ---
        {
            // Shaped: keep shape, placed top-left in a larger grid.
            auto shaped2 = shaped(2, 2, {any("a"), any("b"), any("c"), any("d")});
            auto m3 = mapToGrid(shaped2, 3);
            require(m3.fits && m3.cellOfInput == std::vector<int>{0, 1, 3, 4}, "2x2 shaped maps {0,1,3,4} in 3x3");
            auto m2 = mapToGrid(shaped2, 2);
            require(m2.fits && m2.cellOfInput == std::vector<int>{0, 1, 2, 3}, "2x2 shaped maps row-major in 2x2");

            // Shaped: empty border trimmed, so a bottom-right 2x1 fits a 2x2 grid.
            std::vector<IngredientView> bordered(9);
            bordered[5] = any("a");
            bordered[8] = any("a");
            auto trim = mapToGrid(shaped(3, 3, bordered), 2);
            require(trim.fits, "border-trimmed 3x3 fits 2x2");
            require(trim.cellOfInput[5] == 0 && trim.cellOfInput[8] == 2 && trim.cellOfInput[0] == -1,
                    "border trim places remaining inputs top-left");

            // Too large does not fit; gridSize 0 never fits.
            std::vector<IngredientView> ring(9, any("minecraft:oak_planks"));
            ring[4] = {};
            require(!mapToGrid(shaped(3, 3, ring), 2).fits, "3x3 ring does not fit 2x2");
            require(mapToGrid(shaped(3, 3, ring), 3).fits, "3x3 ring fits 3x3");
            require(!mapToGrid(shaped(3, 3, ring), 0).fits, "gridSize 0 never fits");

            // Shapeless: row-by-row fill, empty inputs skipped.
            auto sl = mapToGrid(shapeless({any("a"), {}, any("b"), any("c")}), 2);
            require(sl.fits && sl.cellOfInput == std::vector<int>{0, -1, 1, 2}, "shapeless fills row by row");
            require(!mapToGrid(shapeless(std::vector<IngredientView>(5, any("a"))), 2).fits,
                    "five shapeless inputs do not fit 2x2");

            // Counts are shared: 4 needed, 3 owned -> exactly one input missing.
            auto fourPlanks = shaped(2, 2, std::vector<IngredientView>(4, any("minecraft:oak_planks")));
            std::vector<PoolEntry> pool;
            addToPool(pool, {"minecraft:oak_planks", 0}, 2, 64);
            addToPool(pool, {"minecraft:oak_planks", 0}, 1, 64);
            require(assign(fourPlanks, pool, 1).missing.size() == 1, "4 needed 3 owned: one input missing");
            addToPool(pool, {"minecraft:oak_planks", 0}, 1, 64);
            require(assign(fourPlanks, pool, 1).complete(), "4 needed 4 owned: complete");

            // Wildcard aux matches any kind, but one cell holds one kind only.
            std::vector<PoolEntry> woolPool;
            addToPool(woolPool, {"minecraft:wool", 3}, 1, 64);
            addToPool(woolPool, {"minecraft:wool", 14}, 2, 64);
            auto wool = assign(shapeless({any("minecraft:wool")}), woolPool, 2);
            require(wool.complete() && wool.chosen[0] == ItemKey{"minecraft:wool", 14},
                    "wildcard picks the kind with the most left");
            require(!assign(shapeless({any("minecraft:wool")}), woolPool, 3).complete(),
                    "3 total across kinds is not 3 of one kind");
            require(!assign(shapeless({exact("minecraft:wool", 5)}), woolPool, 1).complete(),
                    "exact aux not matched by other aux kinds");

            // Constrained inputs are matched first.
            RecipeView constrained = shapeless({
                {{ItemKey{"oak", 0}, ItemKey{"birch", 0}}, 1},
                exact("oak"),
            });
            std::vector<PoolEntry> woodPool;
            addToPool(woodPool, {"oak", 0}, 1, 64);
            addToPool(woodPool, {"birch", 0}, 1, 64);
            auto ca = assign(constrained, woodPool, 1);
            require(ca.complete() && ca.chosen[0] == ItemKey{"birch", 0} && ca.chosen[1] == ItemKey{"oak", 0},
                    "constrained-first: flexible input takes birch, exact takes oak");

            // maxSets bounded by item count and max stack size.
            auto oneLog = shapeless({any("log")});
            std::vector<PoolEntry> logPool;
            addToPool(logPool, {"log", 0}, 5, 64);
            require(maxSets(oneLog, logPool) == 5, "maxSets bounded by count");
            auto twoPlanks = shaped(1, 2, {any("plank"), any("plank")});
            std::vector<PoolEntry> plankPool;
            addToPool(plankPool, {"plank", 0}, 9, 64);
            require(maxSets(twoPlanks, plankPool) == 4, "maxSets 9/2 = 4");
            std::vector<PoolEntry> pearlPool;
            addToPool(pearlPool, {"pearl", 0}, 40, 16);
            require(maxSets(shapeless({any("pearl")}), pearlPool) == 16, "maxSets bounded by maxStack");
            require(maxSets(oneLog, {}) == 0, "empty pool: zero sets");

            // Station mapping.
            require(stationFor(category::CRAFTING) == Station::Crafting, "station crafting");
            require(stationFor(category::FURNACE) == Station::Furnace, "station furnace");
            require(stationFor(category::BLAST_FURNACE) == Station::BlastFurnace, "station blast furnace");
            require(stationFor(category::SMOKER) == Station::Smoker, "station smoker");
            require(stationFor(category::SMITHING) == Station::SmithingTable, "station smithing");
            require(stationFor(category::CAMPFIRE) == Station::None, "campfire not transferable");
            require(stationFor(category::BREWING) == Station::None, "brewing not transferable");
            require(stationFor(category::FUEL) == Station::None, "fuel not transferable");
            require(stationFor(category::ANVIL) == Station::None, "anvil not transferable");
            require(stationFor(category::COMPOSTING) == Station::None, "composting not transferable");
            require(stationFor("bei:tag/custom") == Station::None, "unknown category not transferable");

            // Furnace tag round trip.
            for (auto s : {Station::Furnace, Station::BlastFurnace, Station::Smoker}) {
                require(!furnaceTagOf(s).empty() && furnaceStationOf(furnaceTagOf(s)) == s,
                        "furnace tag round trip");
            }
            require(furnaceTagOf(Station::Crafting).empty() && furnaceTagOf(Station::SmithingTable).empty(),
                    "non-furnace stations have no tag");
            require(furnaceStationOf("campfire") == Station::None && furnaceStationOf("") == Station::None,
                    "unknown tags map to no station");

            // Fuel preference order; the smelted ingredient is never burned.
            std::vector<ItemKey> fuels{{"coal", 0}, {"charcoal", 0}, {"log", ItemKey::ANY_AUX}};
            std::vector<PoolEntry> fuelPool;
            addToPool(fuelPool, {"log", 2}, 10, 64);
            addToPool(fuelPool, {"charcoal", 0}, 3, 64);
            require(pickFuel(fuels, fuelPool, {"iron_ore", 0}) == ItemKey{"charcoal", 0},
                    "coal missing: charcoal before logs by preference");
            fuelPool.clear();
            addToPool(fuelPool, {"log", 2}, 10, 64);
            require(pickFuel(fuels, fuelPool, {"iron_ore", 0}) == ItemKey{"log", 2},
                    "wildcard fuel matches concrete aux");
            require(pickFuel(fuels, fuelPool, {"log", 2}).empty(), "ingredient itself never used as fuel");
            addToPool(fuelPool, {"log", 1}, 4, 64);
            require(pickFuel(fuels, fuelPool, {"log", 2}) == ItemKey{"log", 1},
                    "another log variant still burns while smelting this one");
            require(pickFuel({}, fuelPool, {}).empty(), "no fuels: empty pick");

            // Smelting and smithing sets.
            RecipeView smelt;
            smelt.category = category::FURNACE;
            smelt.inputs = {any("raw_iron")};
            std::vector<PoolEntry> orePool;
            addToPool(orePool, {"raw_iron", 0}, 100, 64);
            require(maxSets(smelt, orePool) == 64, "smelting caps at full stack");
            require(assign(smelt, orePool, 1).complete(), "smelting single set complete");

            RecipeView smith;
            smith.category = category::SMITHING;
            smith.inputs = {exact("upgrade"), any("diamond_sword"), exact("netherite_ingot")};
            std::vector<PoolEntry> smithPool;
            addToPool(smithPool, {"upgrade", 0}, 3, 64);
            addToPool(smithPool, {"diamond_sword", 0}, 2, 1);
            addToPool(smithPool, {"netherite_ingot", 0}, 3, 64);
            require(maxSets(smith, smithPool) == 1, "unstackable base limits smithing to one set");
            smithPool.clear();
            addToPool(smithPool, {"diamond_sword", 0}, 1, 1);
            addToPool(smithPool, {"netherite_ingot", 0}, 1, 64);
            require(assign(smith, smithPool, 1).missing == std::vector<int>{0},
                    "missing template reported as input 0");
        }
    } catch (std::exception const& e) {
        std::cerr << "BEI core tests FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "BEI core tests PASS (" << checks << " checks)\n";
    return 0;
}
