#include "CorporateLogoFont.h"

#include "resources/ResourceIds.h"

#include <Windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

namespace CorporateLogoFont {
namespace {

void moduleAnchor() {}

struct PixelRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

struct PixelLine {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

struct RawDigitGlyph {
    float advancePixels = 0.0f;
    std::vector<PixelRect> rects;
    std::vector<PixelLine> outline;
};

struct DigitCache {
    std::array<DigitGlyph, 10> glyphs{};
    bool ready = false;
};

std::vector<PixelRect> rasterRuns(
    const unsigned char* bitmap,
    int width,
    int height,
    int xOffset,
    int yOffset) {
    constexpr unsigned char kCoverageThreshold = 64;
    std::vector<PixelRect> completed;
    std::vector<PixelRect> active;

    for (int row = 0; row < height; ++row) {
        std::vector<std::pair<int, int>> rowRuns;
        int column = 0;
        while (column < width) {
            while (column < width &&
                   bitmap[row * width + column] < kCoverageThreshold) {
                ++column;
            }
            if (column >= width) break;
            const int start = column;
            while (column < width &&
                   bitmap[row * width + column] >= kCoverageThreshold) {
                ++column;
            }
            rowRuns.emplace_back(xOffset + start, xOffset + column);
        }

        std::vector<PixelRect> next;
        next.reserve(rowRuns.size());
        std::vector<bool> consumed(active.size(), false);
        const int currentY = yOffset + row;
        for (const auto& [runStart, runEnd] : rowRuns) {
            bool extended = false;
            for (std::size_t index = 0; index < active.size(); ++index) {
                if (!consumed[index] &&
                    active[index].x0 == runStart &&
                    active[index].x1 == runEnd &&
                    active[index].y1 == currentY) {
                    PixelRect rect = active[index];
                    rect.y1 = currentY + 1;
                    next.push_back(rect);
                    consumed[index] = true;
                    extended = true;
                    break;
                }
            }
            if (!extended) {
                next.push_back(
                    PixelRect{runStart, currentY, runEnd, currentY + 1});
            }
        }

        for (std::size_t index = 0; index < active.size(); ++index) {
            if (!consumed[index]) completed.push_back(active[index]);
        }
        active = std::move(next);
    }

    completed.insert(completed.end(), active.begin(), active.end());
    return completed;
}

std::vector<PixelLine> rasterOutline(
    const unsigned char* bitmap,
    int width,
    int height,
    int xOffset,
    int yOffset) {
    constexpr unsigned char kCoverageThreshold = 64;
    auto filled = [&](int row, int column) {
        return row >= 0 && row < height &&
               column >= 0 && column < width &&
               bitmap[row * width + column] >= kCoverageThreshold;
    };

    std::vector<PixelLine> edges;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (!filled(row, column)) continue;
            const int x = xOffset + column;
            const int y = yOffset + row;
            if (!filled(row, column - 1)) {
                edges.push_back({x, y, x, y + 1});
            }
            if (!filled(row, column + 1)) {
                edges.push_back({x + 1, y, x + 1, y + 1});
            }
            if (!filled(row - 1, column)) {
                edges.push_back({x, y, x + 1, y});
            }
            if (!filled(row + 1, column)) {
                edges.push_back({x, y + 1, x + 1, y + 1});
            }
        }
    }

    std::sort(
        edges.begin(), edges.end(),
        [](const PixelLine& left, const PixelLine& right) {
            const bool leftHorizontal = left.y0 == left.y1;
            const bool rightHorizontal = right.y0 == right.y1;
            if (leftHorizontal != rightHorizontal) {
                return leftHorizontal < rightHorizontal;
            }
            if (leftHorizontal) {
                if (left.y0 != right.y0) return left.y0 < right.y0;
                return left.x0 < right.x0;
            }
            if (left.x0 != right.x0) return left.x0 < right.x0;
            return left.y0 < right.y0;
        });

    std::vector<PixelLine> merged;
    merged.reserve(edges.size());
    for (const PixelLine& edge : edges) {
        if (!merged.empty()) {
            PixelLine& previous = merged.back();
            const bool horizontal = edge.y0 == edge.y1;
            const bool previousHorizontal = previous.y0 == previous.y1;
            if (horizontal && previousHorizontal &&
                edge.y0 == previous.y0 && edge.x0 == previous.x1) {
                previous.x1 = edge.x1;
                continue;
            }
            if (!horizontal && !previousHorizontal &&
                edge.x0 == previous.x0 && edge.y0 == previous.y1) {
                previous.y1 = edge.y1;
                continue;
            }
        }
        merged.push_back(edge);
    }
    return merged;
}

DigitCache buildDigitCache() {
    DigitCache cache;
    const FontBlob blob = resourceData();
    if (!blob || blob.size > static_cast<std::size_t>(
                               std::numeric_limits<int>::max())) {
        return cache;
    }

    const int fontOffset = stbtt_GetFontOffsetForIndex(blob.data, 0);
    stbtt_fontinfo font{};
    if (fontOffset < 0 ||
        !stbtt_InitFont(&font, blob.data, fontOffset)) {
        return cache;
    }

    constexpr float kRasterHeight = 32.0f;
    const float scale = stbtt_ScaleForPixelHeight(&font, kRasterHeight);
    if (!std::isfinite(scale) || scale <= 0.0f) return cache;

    std::array<RawDigitGlyph, 10> raw{};
    int globalTop = std::numeric_limits<int>::max();
    int globalBottom = std::numeric_limits<int>::min();
    for (int digit = 0; digit < 10; ++digit) {
        const int codepoint = '0' + digit;
        int advance = 0;
        int leftBearing = 0;
        stbtt_GetCodepointHMetrics(
            &font, codepoint, &advance, &leftBearing);
        (void)leftBearing;
        raw[digit].advancePixels =
            static_cast<float>(advance) * scale;

        int width = 0;
        int height = 0;
        int xOffset = 0;
        int yOffset = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            &font,
            0.0f,
            scale,
            codepoint,
            &width,
            &height,
            &xOffset,
            &yOffset);
        if (!bitmap || width <= 0 || height <= 0) {
            if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
            return cache;
        }

        raw[digit].rects =
            rasterRuns(bitmap, width, height, xOffset, yOffset);
        raw[digit].outline =
            rasterOutline(bitmap, width, height, xOffset, yOffset);
        stbtt_FreeBitmap(bitmap, nullptr);
        if (raw[digit].rects.empty() ||
            raw[digit].outline.empty()) {
            return cache;
        }

        for (const PixelRect& rect : raw[digit].rects) {
            globalTop = std::min(globalTop, rect.y0);
            globalBottom = std::max(globalBottom, rect.y1);
        }
    }

    const float digitHeight =
        static_cast<float>(globalBottom - globalTop);
    if (!std::isfinite(digitHeight) || digitHeight <= 0.0f) {
        return cache;
    }

    for (int digit = 0; digit < 10; ++digit) {
        DigitGlyph& target = cache.glyphs[digit];
        target.advance = raw[digit].advancePixels / digitHeight;
        target.rects.reserve(raw[digit].rects.size());
        target.outline.reserve(raw[digit].outline.size());
        for (const PixelRect& rect : raw[digit].rects) {
            target.rects.push_back({
                static_cast<float>(rect.x0) / digitHeight,
                static_cast<float>(rect.y0 - globalTop) / digitHeight,
                static_cast<float>(rect.x1) / digitHeight,
                static_cast<float>(rect.y1 - globalTop) / digitHeight,
            });
        }
        for (const PixelLine& line : raw[digit].outline) {
            target.outline.push_back({
                static_cast<float>(line.x0) / digitHeight,
                static_cast<float>(line.y0 - globalTop) / digitHeight,
                static_cast<float>(line.x1) / digitHeight,
                static_cast<float>(line.y1 - globalTop) / digitHeight,
            });
        }
    }

    cache.ready = true;
    return cache;
}

DigitCache& digitCache() {
    static DigitCache cache;
    static std::once_flag once;
    std::call_once(once, []() { cache = buildDigitCache(); });
    return cache;
}

} // namespace

FontBlob resourceData() {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&moduleAnchor),
            &module)) return {};

    const HRSRC resource = FindResourceW(
        module,
        MAKEINTRESOURCEW(IDR_CORPORATE_LOGO_FONT),
        MAKEINTRESOURCEW(10));
    if (!resource) return {};

    const HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) return {};

    const DWORD size = SizeofResource(module, resource);
    const auto* bytes =
        static_cast<const unsigned char*>(LockResource(loaded));
    if (!bytes || size == 0) return {};
    return {bytes, static_cast<std::size_t>(size)};
}

bool configureImGuiDefault(float pixelSize) {
    const FontBlob blob = resourceData();
    if (!blob || blob.size > static_cast<std::size_t>(
                               std::numeric_limits<int>::max())) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.OversampleV = 2;
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(blob.data),
        static_cast<int>(blob.size),
        pixelSize,
        &config,
        io.Fonts->GetGlyphRangesJapanese());
    if (!font) return false;

    io.FontDefault = font;
    return true;
}

const DigitGlyph* digitGlyph(char digit) {
    if (digit < '0' || digit > '9') return nullptr;
    DigitCache& cache = digitCache();
    if (!cache.ready) return nullptr;
    return &cache.glyphs[static_cast<std::size_t>(digit - '0')];
}

} // namespace CorporateLogoFont
