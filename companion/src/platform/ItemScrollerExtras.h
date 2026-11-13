#pragma once
#include "core/ItemScrollerPolicy.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
namespace praxis::companion {
struct ItemScrollerObservation {
    std::uint64_t generation{};
    std::string source;
    bool recipeValid{};
    std::array<std::string, recipeGridSize> grid;
    std::array<bool, recipeGridSize> locked;
    std::string result;
    bool tradeValid{};
    std::string tradeSell;
    std::string tradeBuyA;
    std::string tradeBuyB;
    int tradeCountA{};
    int tradeCountB{};
    int tradeIndex{};
};
bool installItemScrollerExtras();
bool removeItemScrollerExtras() noexcept;
void resetItemScrollerExtras() noexcept;
std::shared_ptr<ItemScrollerObservation const> itemScrollerObservation() noexcept;
std::string itemScrollerStatus();
} // namespace praxis::companion
