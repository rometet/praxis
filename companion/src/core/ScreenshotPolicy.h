#pragma once
// Goal 3 — Screenshot to Clipboard: pure policy shared by the keybind gate and
// the platform capture path. No Bedrock, ImGui, or Windows headers here so the
// unit test target can include it directly.
#include <array>
#include <cstdint>
#include <vector>

namespace praxis::companion::screenshot {

// F1 opens the companion menu and F3 is the vanilla debug toggle; both stay
// reserved so a screenshot bind can never shadow them.
inline constexpr unsigned menuKeyVk = 0x70;  // F1
inline constexpr unsigned debugKeyVk = 0x72; // F3

// Windows virtual-key codes occupy 0x01..0xFE; 0x00 and the 0xFF terminator
// are not bindable keys.
inline constexpr bool validScreenshotKey(unsigned virtualKey) noexcept {
    if (virtualKey == 0 || virtualKey > 0xFE) return false;
    return virtualKey != menuKeyVk && virtualKey != debugKeyVk;
}

// BGRA8888 frame handed to the DIB packer. The platform capture path fills
// width/height (pixels) and bgra with top-down rows; the packer flips only.
struct ScreenshotFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> bgra;
};

inline constexpr std::uint64_t maxScreenshotPixels = 16384ull * 16384ull;

inline constexpr bool validScreenshotFrame(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0 || height == 0) return false;
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= maxScreenshotPixels;
}

// Row bytes for a 32bpp BGRA image with no packing surprises (stride == width*4).
inline constexpr std::uint64_t screenshotRowStride(std::uint32_t width) noexcept {
    return static_cast<std::uint64_t>(width) * 4ull;
}

// CF_DIB wants the pixel data only (BITMAPINFOHEADER + pixels, no file header),
// rows bottom-up, and negative-height is avoided for maximum viewer compat.
// Padding from the packer is safe at 32bpp but the arithmetic stays 8-byte safe.
inline constexpr std::uint64_t screenshotDibSize(std::uint32_t width, std::uint32_t height) noexcept {
    return 40ull + screenshotRowStride(width) * static_cast<std::uint64_t>(height);
}

// Validates a frame and, when valid, emits a bottom-up 32bpp BI_RGB DIB ready
// for SetClipboardData(CF_DIB, ...). Returns false (buffer cleared) on any
// invalid size or truncated bgra payload.
inline bool packClipboardDib(ScreenshotFrame const& frame, std::vector<std::uint8_t>& outDib) {
    outDib.clear();
    if (!validScreenshotFrame(frame.width, frame.height)) return false;
    std::uint64_t const rowStride = screenshotRowStride(frame.width);
    std::uint64_t const expected = rowStride * static_cast<std::uint64_t>(frame.height);
    if (frame.bgra.size() != expected) return false;
    outDib.resize(static_cast<std::size_t>(screenshotDibSize(frame.width, frame.height)));
    auto* header = reinterpret_cast<std::uint8_t*>(outDib.data());
    // BITMAPINFOHEADER, 40 bytes, reused field-for-field.
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        header[at] = static_cast<std::uint8_t>(v & 0xFF);
        header[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        header[at + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        header[at + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    auto put16 = [&](std::size_t at, std::uint16_t v) {
        header[at] = static_cast<std::uint8_t>(v & 0xFF);
        header[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    };
    put32(0, 40);
    put32(4, frame.width);
    put32(8, frame.height); // positive height => bottom-up rows follow
    put16(12, 1);           // biPlanes (uint16)
    put16(14, 32);          // biBitCount (uint16)
    put32(16, 0);           // biCompression = BI_RGB
    put32(20, static_cast<std::uint32_t>(expected)); // biSizeImage (bounded above by maxScreenshotPixels*4)
    std::uint8_t const* src = frame.bgra.data();
    std::uint8_t* dstBase = outDib.data() + 40;
    for (std::uint32_t row = 0; row < frame.height; ++row) {
        // Bottom-up: first stored row is the LAST source row.
        std::uint8_t const* srcRow = src + static_cast<std::size_t>(rowStride) * row;
        std::uint8_t* dstRow = dstBase + static_cast<std::size_t>(rowStride) * (frame.height - 1 - row);
        for (std::uint64_t x = 0; x < rowStride; x += 4) {
            dstRow[x + 0] = srcRow[x + 0]; // B
            dstRow[x + 1] = srcRow[x + 1]; // G
            dstRow[x + 2] = srcRow[x + 2]; // R
            dstRow[x + 3] = srcRow[x + 3]; // A (viewers mostly ignore CF_DIB alpha)
        }
    }
    return true;
}

} // namespace praxis::companion::screenshot
