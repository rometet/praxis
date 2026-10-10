#pragma once

namespace praxis::companion::packs {
// Diagnostic-only, explicit environment opt-in. Failure never fails mod startup.
void installPassiveReloadObserver() noexcept;
// Close admission without unhooking or blocking. False retains owner resources.
bool stopPassiveReloadObserver() noexcept;
} // namespace praxis::companion::packs
