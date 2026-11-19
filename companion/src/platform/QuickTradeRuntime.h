#pragma once
#include "core/QuickTradePolicy.h"
#include <cstdint>
#include <memory>
#include <string>

class ContainerScreenController;
class ContainerManagerController;
namespace praxis::companion {
// Session-only explicit opt-in. Default OFF on every process startup.
QuickTradeOptions quickTradeOptions();
bool configureQuickTrade(QuickTradeOptions options);
std::string quickTradeStatus();
void quickTradeFrame(std::shared_ptr<ContainerScreenController> const& screen,
                     std::shared_ptr<ContainerManagerController> const& manager,
                     std::uintptr_t view, bool tradeView, bool competingJob);
void quickTradeReset() noexcept;
} // namespace praxis::companion
