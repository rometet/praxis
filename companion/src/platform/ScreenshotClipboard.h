#pragma once
// Screenshot to Clipboard — narrow Win32 publishing adapter. Packs a validated
// ScreenshotFrame into a CF_DIB and hands it to the clipboard, following the
// BEI src/ui/widgets/Clipboard.cpp Win32 pattern (LGPL-3.0-or-later,
// upstream 410ecfde; see THIRD_PARTY_NOTICES.md). No capture path here; the
// caller supplies the actual foreground game window and owns the decision to
// publish. The clipboard is never touched except on this explicit call.
#include "core/ScreenshotPolicy.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace praxis::companion::screenshot {

// Publishes frame as CF_DIB owned by `owner`. The DIB is packed and filled
// BEFORE the clipboard is opened; OpenClipboard is attempted once (no sleeps,
// safe for the render thread). Fails closed without touching the clipboard
// when the frame is invalid or `owner` is null, invisible, not in the current
// process, or not the actual foreground window.
bool publishScreenshotToClipboard(HWND owner, ScreenshotFrame const& frame) noexcept;

} // namespace praxis::companion::screenshot
