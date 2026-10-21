#pragma once
namespace praxis::companion::packs {
// Optional fresh-title backend; failure preserves ordinary mod startup.
bool installTitlePackSwitch() noexcept;
bool stopTitlePackSwitch() noexcept;
bool resourcePackImageVerified() noexcept;
}
