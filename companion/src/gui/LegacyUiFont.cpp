#include "gui/LegacyUiFont.h"
#include "resources/ResourceIds.h"
#include "runtime/Runtime.h"
#include "gui/GuiBridge.h"

#include <Windows.h>
#include <imgui.h>

#include <cstddef>
#include <algorithm>
#include <cstring>
#include <limits>

namespace praxis::companion {
namespace {
ImGuiContext* sharedContext{};
ImFontAtlas* sharedAtlas{};
ImFont* sharedFont{};

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
    // LHolo can rebuild its atlas after Praxis has unregistered and unloaded.
    // Give the atlas an owned copy, rather than borrowing DLL resource memory.
    // Glyph ranges also point into this DLL's statically linked ImGui. Keep
    // their owned copy in the same allocation the atlas releases for FontData.
    std::size_t rangeCount{};
    if (ranges) while (ranges[rangeCount]) ++rangeCount;
    ++rangeCount;
    auto const rangeOffset = (blob.size + alignof(ImWchar) - 1) & ~(alignof(ImWchar) - 1);
    auto const allocationSize = rangeOffset + rangeCount * sizeof(ImWchar);
    auto* copied = ImGui::MemAlloc(allocationSize);
    if (!copied) return nullptr;
    std::memcpy(copied, blob.data, blob.size);
    auto* copiedRanges = reinterpret_cast<ImWchar*>(static_cast<unsigned char*>(copied) + rangeOffset);
    if (ranges) std::memcpy(copiedRanges, ranges, rangeCount * sizeof(ImWchar));
    else copiedRanges[0] = 0;
    config.FontDataOwnedByAtlas = true;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.MergeMode = merge;
    config.PixelSnapH = false;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        copied,
        static_cast<int>(blob.size),
        pixels,
        &config,
        copiedRanges);
}
ImFont* latinWithJapanese(int latinId, float pixels) noexcept {
    auto& io = ImGui::GetIO();
    ImFont* base = addFont(resourceData(latinId), pixels, io.Fonts->GetGlyphRangesDefault(), false);
    if (!base) return nullptr;
    if (!addFont(resourceData(IDR_NOTO_SANS_JP_FONT), pixels, io.Fonts->GetGlyphRangesJapanese(), true))
        return nullptr;
    return base;
}

ImFont* appendLegacyUiFont(LegacyUiFontFamily family, float pixels) noexcept {
    switch (family) {
    case LegacyUiFontFamily::NotoSansInter:
        return latinWithJapanese(IDR_INTER_FONT, pixels);
    case LegacyUiFontFamily::IbmPlexSans:
        return latinWithJapanese(IDR_IBM_PLEX_SANS_FONT, pixels);
    case LegacyUiFontFamily::CorporateLogo:
    default:
        return addFont(resourceData(IDR_CORPORATE_LOGO_FONT), pixels,
                       ImGui::GetIO().Fonts->GetGlyphRangesJapanese(), false);
    }
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
    sharedContext = nullptr;
    sharedAtlas = nullptr;
    sharedFont = nullptr;
    io.Fonts->Clear();

    io.FontDefault = appendLegacyUiFont(family, pixelSize);
    bool const configured = io.FontDefault != nullptr;
    if (!configured) io.FontDefault = io.Fonts->AddFontDefault();
    return configured;
}

void initializeSharedLegacyUiFont(void* context) noexcept {
    auto* previous = ImGui::GetCurrentContext();
    auto* requested = static_cast<ImGuiContext*>(context);
    if (!requested) return;
    ImGui::SetCurrentContext(requested);
    sharedContext = requested;
    sharedAtlas = ImGui::GetIO().Fonts;
    sharedFont = nullptr;
    try {
        auto const config = runtimeConfigSnapshot();
        auto const family = static_cast<LegacyUiFontFamily>(std::clamp(config.guiStyle.fontFamily, 0, 2));
        sharedFont = appendLegacyUiFont(family, 18.0f);
        if (!sharedFont) logGuiError("LHolo bridge: embedded Praxis 18px font registration failed");
    } catch (...) {
        logGuiError("LHolo bridge: embedded Praxis font registration failed");
    }
    ImGui::SetCurrentContext(previous);
}

bool sharedLegacyUiFontReady(void* context) noexcept {
    return context && context == sharedContext && sharedFont
        && ImGui::GetIO().Fonts == sharedAtlas;
}

LegacyUiScope::LegacyUiScope()
    : savedStyle_(ImGui::GetStyle()), savedFontGlobalScale_(ImGui::GetIO().FontGlobalScale) {
    ImGuiStyle praxisStyle{};
    ImGui::StyleColorsDark(&praxisStyle);
    ImGui::GetStyle() = praxisStyle;
    ImGui::GetIO().FontGlobalScale = 1.0f;
    ImGui::PushFont(sharedLegacyUiFontReady(ImGui::GetCurrentContext())
                       ? sharedFont : ImGui::GetIO().FontDefault);
}

LegacyUiScope::~LegacyUiScope() {
    ImGui::GetStyle() = savedStyle_;
    ImGui::GetIO().FontGlobalScale = savedFontGlobalScale_;
    ImGui::PopFont();
}

} // namespace praxis::companion
