#pragma once
#include "core/HotkeyPolicy.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace praxis::companion {
inline constexpr std::size_t recipeFavoriteLimit = 8;
inline constexpr std::size_t tradeFavoriteLimit = 8;
inline constexpr std::size_t recipeGridSize = 9;
struct RecipeFavorite {
    std::string name;
    // Empty slot = no ingredient. Order is row-major in the crafting grid.
    std::array<std::string, recipeGridSize> grid{};
    std::string result;
    bool operator==(RecipeFavorite const&) const = default;
};
struct TradeFavorite {
    std::string sellItem;
    std::string buyA;
    std::string buyB;
    int buyCountA{};
    int buyCountB{};
    bool operator==(TradeFavorite const&) const = default;
};
struct ItemScrollerOptions {
    bool recipeFavorites{};
    bool tradeFavorites{};
    bool requireConfirmation = true;
    int applyKey = 0;
    unsigned applyModifiers = anyModifiers;
    int maxSteps = 18;
    std::array<RecipeFavorite, recipeFavoriteLimit> recipes{};
    std::array<TradeFavorite, tradeFavoriteLimit> trades{};
    std::size_t selectedRecipe{};
    std::size_t selectedTrade{};
    bool operator==(ItemScrollerOptions const&) const = default;
};
inline bool validScrollerText(std::string_view text, std::size_t maximum) noexcept {
    if (text.size() > maximum) return false;
    for (unsigned char c : text) if (c < 32 || c == 127) return false;
    return true;
}
inline bool validItemScrollerOptions(ItemScrollerOptions const& o) noexcept {
    if (!(o.applyKey >= 0 && o.applyKey <= 255) || !validModifiers(o.applyModifiers)
        || o.maxSteps < 1 || o.maxSteps > 36 || o.selectedRecipe >= recipeFavoriteLimit
        || o.selectedTrade >= tradeFavoriteLimit) return false;
    for (auto const& r : o.recipes) {
        if (!validScrollerText(r.name, 64) || !validScrollerText(r.result, 96)) return false;
        for (auto const& slot : r.grid) if (!validScrollerText(slot, 96)) return false;
    }
    for (auto const& t : o.trades)
        if (!validScrollerText(t.sellItem, 96) || !validScrollerText(t.buyA, 96) || !validScrollerText(t.buyB, 96))
            return false;
    return true;
}
inline bool emptyRecipe(RecipeFavorite const& r) noexcept {
    if (!r.result.empty()) return false;
    for (auto const& slot : r.grid) if (!slot.empty()) return false;
    return true;
}
// A plan only fills empty grid slots from matching player stacks. Existing
// contents, non-matching items and unavailable counts fail the whole plan.
struct RecipeMove { int playerSlot{-1}; int gridSlot{-1}; };
inline std::vector<RecipeMove> planRecipeApply(RecipeFavorite const& recipe,
    std::array<std::string, 36> const& playerItems, std::array<int, 36> const& playerCounts,
    std::array<std::string, recipeGridSize> const& gridItems,
    std::array<bool, recipeGridSize> const& gridLocked, std::size_t maximumSteps) {
    std::vector<RecipeMove> moves;
    if (recipe.result.empty() || maximumSteps == 0) return moves;
    for (std::size_t g = 0; g < recipeGridSize; ++g) {
        auto const& wanted = recipe.grid[g];
        if (wanted.empty()) continue;
        auto const& existing = gridItems[g];
        if (!existing.empty() && existing != wanted) return {};
        if (existing == wanted) continue;
        if (gridLocked[g]) return {};
        int source = -1;
        for (int p = 0; p < 36; ++p)
            if (playerItems[p] == wanted && playerCounts[p] > 0) { source = p; break; }
        if (source < 0) return {};
        moves.push_back({source, static_cast<int>(g)});
        if (moves.size() > maximumSteps) return {};
    }
    return moves;
}
struct ConfirmationState {
    bool armed{};
    std::uint64_t armedAt{};
    bool press(std::uint64_t now, std::uint32_t windowMs = 5000) noexcept {
        if (armed && now >= armedAt && now - armedAt <= windowMs) { armed = false; return true; }
        armed = true;
        armedAt = now;
        return false;
    }
    void clear() noexcept { *this = {}; }
};
} // namespace praxis::companion
