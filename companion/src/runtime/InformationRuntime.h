#pragma once
#include "core/InformationPolicy.h"
#include "core/BlockStateComparePolicy.h"
#include <memory>
namespace praxis::companion {
bool startInformationRuntime();
bool stopInformationRuntime() noexcept;
void resetInformationRuntime() noexcept;
void configureInformationRuntime(InformationOptions const& options);
std::shared_ptr<InformationOptions const> informationOptions() noexcept;
std::shared_ptr<InformationSnapshot const> informationSnapshot() noexcept;
// Most recent client-visible gameplay target, for the block compare page.
std::shared_ptr<BlockStateSample const> lastInspectedBlockSample() noexcept;
} // namespace praxis::companion
