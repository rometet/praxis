#include "platform/ScreenshotClipboard.h"

#include <cstring>

namespace praxis::companion::screenshot {

bool publishScreenshotToClipboard(HWND owner, ScreenshotFrame const& frame) noexcept {
    try {
        // Pack and fully fill the DIB before any clipboard interaction, so a
        // rejected frame or an allocation failure can never disturb the
        // user's clipboard contents.
        std::vector<std::uint8_t> dib;
        if (!packClipboardDib(frame, dib) || dib.empty()) return false;

        // The owner must be a visible top-level window of this process AND the
        // actual foreground window: screenshots publish only on the caller's
        // explicit in-game action, never for a hidden or background window.
        if (!owner || !IsWindow(owner) || !IsWindowVisible(owner)) return false;
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(owner, &ownerPid);
        if (ownerPid != GetCurrentProcessId()) return false;
        if (GetForegroundWindow() != owner) return false;

        // GMEM_MOVEABLE per the CF_DIB contract. The system owns the memory
        // only after a successful SetClipboardData; otherwise we free it.
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, dib.size());
        if (!mem) return false;
        bool published = false;
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, dib.data(), dib.size());
            GlobalUnlock(mem);
            if (OpenClipboard(owner)) {
                // The clipboard is opened exactly once around this publish;
                // CloseClipboard always runs, on every path below.
                if (EmptyClipboard()) {
                    published = SetClipboardData(CF_DIB, mem) != nullptr;
                }
                CloseClipboard();
            }
        }
        if (!published) GlobalFree(mem);
        return published;
    } catch (...) {
        // Packing allocates; never let an exception escape into the host.
        return false;
    }
}

} // namespace praxis::companion::screenshot
