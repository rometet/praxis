#pragma once
class IClientInstance;
namespace praxis::companion::packs {
bool ensureSharedNativeCompletionQueue(IClientInstance& client) noexcept;
// Optional fresh-title backend; failure preserves ordinary mod startup.
bool installTitlePackSwitch() noexcept;
bool stopTitlePackSwitch() noexcept;
bool resourcePackImageVerified() noexcept;
}
