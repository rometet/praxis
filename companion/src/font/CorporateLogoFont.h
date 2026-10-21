#pragma once

#include <cstddef>
#include <vector>

namespace CorporateLogoFont {

struct FontBlob {
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    explicit operator bool() const {
        return data != nullptr && size > 0;
    }
};

struct GlyphRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct GlyphLine {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct DigitGlyph {
    float advance = 0.0f;
    std::vector<GlyphRect> rects;
    std::vector<GlyphLine> outline;
};

FontBlob resourceData();
bool configureImGuiDefault(float pixelSize = 18.0f);
const DigitGlyph* digitGlyph(char digit);

} // namespace CorporateLogoFont
