#include "gui/LegacyUiFont.h"
#include "resources/ResourceIds.h"

#include <Windows.h>
#include <imgui.h>

#include <cstddef>
#include <limits>

namespace praxis::companion {
namespace {

struct FontBlob {
    unsigned char const* data{};
    std::size_t size{};
    explicit operator bool() const noexcept { return data && size; }
};

void moduleAnchor() {}

FontBlob resourceData(int id) noexcept {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&moduleAnchor), &module)) return {};
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource) return {};
    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) return {};
    DWORD const size = SizeofResource(module, resource);
    auto const* bytes = static_cast<unsigned char const*>(LockResource(loaded));
    return bytes && size ? FontBlob{bytes, static_cast<std::size_t>(size)} : FontBlob{};
}

ImFont* addFont(FontBlob blob, float pixels, ImWchar const* ranges, bool merge) noexcept {
    if (!blob || blob.size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return nullptr;
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.MergeMode = merge;
    config.PixelSnapH = false;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(blob.data),
        static_cast<int>(blob.size),
        pixels,
        &config,
        ranges);
}
bool latinWithJapanese(int latinId, float pixels) noexcept {
    auto& io = ImGui::GetIO();
    ImFont* base = addFont(resourceData(latinId), pixels, io.Fonts->GetGlyphRangesDefault(), false);
    if (!base) return false;
    if (!addFont(resourceData(IDR_NOTO_SANS_JP_FONT), pixels, io.Fonts->GetGlyphRangesJapanese(), true))
        return false;
    io.FontDefault = base;
    return true;
}

} // namespace

char const* legacyUiFontDisplayName(LegacyUiFontFamily family) noexcept {
    switch (family) {
    case LegacyUiFontFamily::NotoSansInter: return "Noto Sans JP + Inter";
    case LegacyUiFontFamily::IbmPlexSans: return "IBM Plex Sans";
    case LegacyUiFontFamily::CorporateLogo:
    default: return "Corporate Logo";
    }
}
bool configureLegacyUiFont(LegacyUiFontFamily family, float pixelSize) noexcept {
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();

    bool configured = false;
    switch (family) {
    case LegacyUiFontFamily::NotoSansInter:
        configured = latinWithJapanese(IDR_INTER_FONT, pixelSize);
        break;
    case LegacyUiFontFamily::IbmPlexSans:
        configured = latinWithJapanese(IDR_IBM_PLEX_SANS_FONT, pixelSize);
        break;
    case LegacyUiFontFamily::CorporateLogo:
    default:
        if (auto* font = addFont(
                resourceData(IDR_CORPORATE_LOGO_FONT),
                pixelSize,
                io.Fonts->GetGlyphRangesJapanese(),
                false)) {
            io.FontDefault = font;
            configured = true;
        }
        break;
    }

    if (!configured) io.FontDefault = io.Fonts->AddFontDefault();
    return io.FontDefault != nullptr;
}

} // namespace praxis::companion
