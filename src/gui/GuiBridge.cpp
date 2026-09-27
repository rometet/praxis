#include "gui/GuiBridge.h"
#include "runtime/Runtime.h"
#include "render/Renderer.h"
#include "core/Settings.h"
#include "gui/LegacyUiFont.h"

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"

#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace praxis::companion {
namespace {
using DrawFn = void (*)(void*) noexcept;
using StateFn = void (*)(bool) noexcept;
using HudNeededFn = bool (*)() noexcept;
using RegisterFn = bool (__cdecl *)(void*, unsigned, DrawFn, DrawFn, HudNeededFn, StateFn,
                                   unsigned, std::size_t, std::size_t, std::size_t) noexcept;
using UnregisterFn = bool (__cdecl *)(void*) noexcept;
using RegisteredFn = bool (__cdecl *)(void*) noexcept;
using CloseFn = void (__cdecl *)() noexcept;
using InputCapturedFn = bool (__cdecl *)() noexcept;

std::mutex bridgeMutex;
std::atomic<HMODULE> bridgeModule{};
std::atomic<HMODULE> bridgeOwner{};
UnregisterFn unregisterFn{};
std::atomic<CloseFn> closeFn{};
std::atomic<InputCapturedFn> inputCapturedFn{};
std::atomic<RegisteredFn> registeredFn{};
std::atomic<unsigned> bridgeReaders{};
std::atomic<ULONGLONG> nextBridgeAttempt{};
bool registered{};

struct ModuleCard {
    Feature feature;
    char const* name;
    char const* description;
    bool misc;
};
constexpr std::array cards{
    ModuleCard{Feature::NoRender, "NoRender", "描画要素を個別に非表示", false},
    ModuleCard{Feature::ChunkBorder, "ChunkBorders", "チャンク・リージョン境界", false},
    ModuleCard{Feature::Fullbright, "Fullbright", "ゲームプレイ中の明るさ", false},
    ModuleCard{Feature::RedstoneOverlay, "RedstoneOverlay", "信号強度を表示", false},
    ModuleCard{Feature::LightOverlay, "LightOverlay", "明るさを表示", false},
    ModuleCard{Feature::Zoom, "Zoom", "FOVとカメラ操作", false},
    ModuleCard{Feature::ShulkerPreview, "ShulkerPreview", "中身のプレビュー", true},
};
static_assert(cards.size() == featureCount);

bool* enabledField(RuntimeConfig& config, Feature feature) noexcept {
    switch (feature) {
    case Feature::NoRender: return &config.noRender.enabled;
    case Feature::ChunkBorder: return &config.chunkBorder.enabled;
    case Feature::Fullbright: return &config.fullbright;
    case Feature::ShulkerPreview: return &config.shulkerPreview;
    case Feature::RedstoneOverlay: return &config.redstoneOverlay;
    case Feature::LightOverlay: return &config.lightOverlay;
    case Feature::Zoom: return &config.zoom;
    default: return nullptr;
    }
}

bool backendSelectable(Feature feature) noexcept {
    // Persist requests on the title screen; RuntimePolicy gates their effect
    // until each native backend becomes available in a world.
    return static_cast<unsigned>(feature) < featureCount;
}

bool matches(char const* name, char const* description, char const* search) {
    if (!search || !*search) return true;
    auto contains = [search](std::string_view text) {
        for (std::size_t start = 0; start < text.size(); ++start) {
            std::size_t i = 0;
            while (search[i] && start + i < text.size()
                   && std::tolower(static_cast<unsigned char>(text[start + i]))
                       == std::tolower(static_cast<unsigned char>(search[i]))) ++i;
            if (!search[i]) return true;
        }
        return false;
    };
    return contains(name) || contains(description);
}

std::array<float, 4> colorFloats(std::uint32_t abgr) {
    return {static_cast<float>(abgr & 255) / 255.0f,
            static_cast<float>((abgr >> 8) & 255) / 255.0f,
            static_cast<float>((abgr >> 16) & 255) / 255.0f,
            static_cast<float>((abgr >> 24) & 255) / 255.0f};
}
std::uint32_t packedAbgr(std::array<float, 4> const& c) {
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i)
        result |= static_cast<std::uint32_t>(std::lround(std::clamp(c[i], 0.0f, 1.0f) * 255.0f)) << (i * 8);
    return result;
}

ImVec4 styleColor(std::uint32_t abgr, float opacity = 1.0f) {
    auto const value = colorFloats(abgr);
    return {value[0], value[1], value[2], value[3] * opacity};
}

void applyPraxisStyle(GuiStyleOptions const& options) {
    auto& style = ImGui::GetStyle();
    style.WindowRounding = options.roundingRadius;
    style.ChildRounding = std::max(0.0f, options.roundingRadius - 2.0f);
    style.FrameRounding = options.roundingSmoothness;
    style.PopupRounding = std::min(options.roundingRadius, 12.0f);
    style.ScrollbarRounding = options.roundingSmoothness;
    style.GrabRounding = options.roundingSmoothness;
    style.WindowBorderSize = options.outlineSize;
    style.PopupBorderSize = options.outlineSize;
    style.FrameBorderSize = std::min(options.outlineSize, 2.0f);
    style.Colors[ImGuiCol_WindowBg] = styleColor(options.background, options.backgroundOpacity);
    style.Colors[ImGuiCol_ChildBg] = styleColor(options.background, options.backgroundOpacity * 0.78f);
    style.Colors[ImGuiCol_Border] = styleColor(options.accentOutline, options.backgroundOpacity);
    style.Colors[ImGuiCol_Button] = styleColor(options.accentOutline, 0.72f);
    style.Colors[ImGuiCol_ButtonHovered] = styleColor(options.accent, 0.72f);
    style.Colors[ImGuiCol_ButtonActive] = styleColor(options.accent, 0.90f);
    style.Colors[ImGuiCol_Header] = styleColor(options.accentOutline, 0.72f);
    style.Colors[ImGuiCol_HeaderHovered] = styleColor(options.accent, 0.64f);
    style.Colors[ImGuiCol_HeaderActive] = styleColor(options.accent, 0.78f);
    style.Colors[ImGuiCol_CheckMark] = styleColor(options.accent);
    style.Colors[ImGuiCol_SliderGrab] = styleColor(options.accent);
    style.Colors[ImGuiCol_SliderGrabActive] = styleColor(options.accent, 0.85f);
}

void drawStyleDetails(GuiStyleOptions& options, bool& changed) {
    changed |= ImGui::SliderFloat("横幅", &options.widthRatio, 0.35f, 1.0f);
    changed |= ImGui::SliderFloat("高さ", &options.heightRatio, 0.35f, 1.0f);
    changed |= ImGui::SliderFloat("背景の不透明度", &options.backgroundOpacity, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("角の丸み", &options.roundingRadius, 0.0f, 32.0f);
    changed |= ImGui::SliderFloat("操作部の丸み", &options.roundingSmoothness, 0.0f, 16.0f);
    changed |= ImGui::SliderFloat("枠線の太さ", &options.outlineSize, 0.0f, 6.0f);
    auto accent = colorFloats(options.accent);
    if (ImGui::ColorEdit4("アクセント色", accent.data())) {
        options.accent = packedAbgr(accent); changed = true;
    }
    auto outline = colorFloats(options.accentOutline);
    if (ImGui::ColorEdit4("枠線色", outline.data())) {
        options.accentOutline = packedAbgr(outline); changed = true;
    }
    auto background = colorFloats(options.background);
    if (ImGui::ColorEdit4("背景色", background.data())) {
        options.background = packedAbgr(background); changed = true;
    }
}

void drawDetails(RuntimeConfig& config, Feature feature, bool& changed) {
    switch (feature) {
    case Feature::NoRender:
        ImGui::TextUnformatted("描画パス");
        for (std::size_t i = 0; i < kRenderPassCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            changed |= ImGui::Checkbox(kRenderPassLabels[i].data(), &config.noRender.passes[i]);
            ImGui::PopID();
        }
        break;
    case Feature::ChunkBorder: {
        auto& c = config.chunkBorder;
        changed |= ImGui::SliderInt("Render Distance (chunks)", &c.renderDistance, 1, 12);
        changed |= ImGui::SliderInt("Height Above", &c.heightAbove, 4, 128);
        changed |= ImGui::SliderInt("Height Below", &c.heightBelow, 2, 96);
        changed |= ImGui::SliderFloat("Line Width", &c.lineWidth, 0.5f, 5.0f);
        changed |= ImGui::SliderInt("Line Budget", &c.lineBudget, 256, 8192);
        changed |= ImGui::Checkbox("Show Region Borders", &c.showRegions);
        changed |= ImGui::Checkbox("Show Y Coordinates", &c.showYCoordinates);
        changed |= ImGui::Checkbox("Show HUD Info", &c.showHud);
        auto chunk = colorFloats(c.chunkColor);
        if (ImGui::ColorEdit4("Chunk Border Color", chunk.data())) {
            c.chunkColor = packedAbgr(chunk); changed = true;
        }
        auto region = colorFloats(c.regionColor);
        if (ImGui::ColorEdit4("Region Border Color", region.data())) {
            c.regionColor = packedAbgr(region); changed = true;
        }
        break;
    }
    case Feature::Fullbright:
        changed |= ImGui::SliderFloat("Brightness", &config.brightness, 1.0f, 25.0f, "%.1f");
        ImGui::TextDisabled("ワールドの読み込み完了後に適用");
        break;
    case Feature::LightOverlay: {
        auto& l = config.lightOptions;
        changed |= ImGui::SliderInt("表示範囲", &l.range, 1, 16);
        changed |= ImGui::SliderInt("湧き判定の上限", &l.spawnThreshold, 0, 15);
        changed |= ImGui::SliderInt("1回の検査数", &l.scanBudget, 1, 64);
        changed |= ImGui::SliderInt("最大表示数", &l.drawBudget, 16, 2048);
        changed |= ImGui::Checkbox("すべての光レベルを表示", &l.showAllLevels);
        changed |= ImGui::Checkbox("一番上の面だけ表示", &l.onlyTopSurface);
        changed |= ImGui::Checkbox("空からの光を無視", &l.ignoreSkyLight);
        changed |= ImGui::Checkbox("数値を表示", &l.numericDisplay);
        break;
    }
    case Feature::RedstoneOverlay: {
        auto& r = config.redstoneOptions;
        changed |= ImGui::SliderInt("表示範囲", &r.range, 1, 16);
        changed |= ImGui::SliderInt("1回の検査数", &r.scanBudget, 1, 64);
        changed |= ImGui::SliderInt("最大表示数", &r.drawBudget, 16, 2048);
        changed |= ImGui::Checkbox("信号強度を表示", &r.showValues);
        ImGui::TextDisabled("Dust / comparator: 0–15; repeater / torch: OFF/ON");
        break;
    }
    case Feature::ShulkerPreview: {
        auto& p = config.shulkerOptions;
        changed |= ImGui::Checkbox("ホットバーのツールチップに表示", &p.showInHotbar);
        changed |= ImGui::Checkbox("スロット番号を表示", &p.showSlotNumbers);
        changed |= ImGui::Checkbox("アイテム数を表示", &p.showItemCount);
        changed |= ImGui::SliderFloat("プレビュー倍率", &p.previewScale, 0.75f, 2.0f, "%.2f");
        auto color = colorFloats(p.backgroundColor);
        if (ImGui::ColorEdit4("背景色", color.data())) {
            p.backgroundColor = packedAbgr(color); changed = true;
        }
        ImGui::TextDisabled("インベントリのシュルカーボックスにカーソルを合わせると表示");
        break;
    }
    case Feature::Zoom: {
        auto& z = config.zoomOptions;
        changed |= ImGui::Checkbox("Hold", &z.hold);
        if (ImGui::InputInt("Zoom key (VK)", &config.zoomKey, 0, 0)) {
            config.zoomKey = std::clamp(config.zoomKey, 1, 255);
            changed = true;
        }
        if (ImGui::SliderFloat("Target FOV", &z.targetFov, 5.0f, 60.0f, "%.0f")) {
            config.zoomMultiplier = z.targetFov / 70.0f; changed = true;
        }
        changed |= ImGui::Checkbox("Animation", &z.animation);
        if (z.animation) changed |= ImGui::SliderFloat("Animation speed", &z.animationSpeed, 1.0f, 30.0f);
        changed |= ImGui::Checkbox("Scroll adjust", &z.scrollAdjust);
        if (z.scrollAdjust) changed |= ImGui::SliderFloat("Scroll sensitivity", &z.scrollSensitivity, 0.5f, 10.0f);
        changed |= ImGui::Checkbox("Camera sensitivity correction", &z.adjustCameraSensitivity);
        changed |= ImGui::Checkbox("Smooth camera", &z.smoothCamera);
        if (z.smoothCamera)
            changed |= ImGui::SliderFloat("Camera smoothing multiplier", &z.cameraSmoothingMultiplier, 0.1f, 5.0f);
        break;
    }
    default:
        ImGui::TextDisabled("26.51.5 native backend unavailable");
        break;
    }
}

bool drawCardIcon(char const* id, bool favorite, bool active = false) {
    ImVec2 const size{30.0f, 27.0f};
    ImVec2 const start = ImGui::GetCursorScreenPos();
    bool const pressed = ImGui::InvisibleButton(id, size);
    bool const hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    ImU32 const background = ImGui::GetColorU32(
        hovered ? ImGuiCol_ButtonHovered : active ? ImGuiCol_ButtonActive : ImGuiCol_Button);
    ImU32 const foreground = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, background, 5.0f);
    ImVec2 const center{start.x + size.x * 0.5f, start.y + size.y * 0.5f};
    if (!favorite) {
        draw->AddCircle(center, 5.0f, foreground, 12, 1.8f);
        draw->AddCircleFilled(center, 1.8f, foreground, 10);
        for (int spoke = 0; spoke < 8; ++spoke) {
            float const angle = static_cast<float>(spoke) * 3.14159265f / 4.0f;
            draw->AddLine({center.x + std::cos(angle) * 6.0f, center.y + std::sin(angle) * 6.0f},
                          {center.x + std::cos(angle) * 8.0f, center.y + std::sin(angle) * 8.0f},
                          foreground, 1.8f);
        }
    } else {
        ImVec2 points[10];
        for (int point = 0; point < 10; ++point) {
            float const radius = point % 2 == 0 ? 8.0f : 3.6f;
            float const angle = -3.14159265f * 0.5f
                + static_cast<float>(point) * 3.14159265f / 5.0f;
            points[point] = {center.x + std::cos(angle) * radius,
                             center.y + std::sin(angle) * radius};
        }
        if (active) draw->AddConvexPolyFilled(points, 10, foreground);
        else draw->AddPolyline(points, 10, foreground, ImDrawFlags_Closed, 1.7f);
    }
    return pressed;
}

void drawProfiles(RuntimeConfig& config, bool& changed) {
    static char profileName[49]{};
    static std::string selected;
    static std::string status;
    ImGui::TextUnformatted("Config Manager");
    ImGui::Separator();
    ImGui::TextUnformatted("現在の設定: config.json");
    if (ImGui::Button("現在の設定を保存", {180.0f, 32.0f}))
        status = flushRuntimeConfig() ? "保存しました" : "保存できませんでした";
    ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputText("設定名", profileName, sizeof(profileName));
    if (ImGui::Button("名前を付けて保存", {180.0f, 32.0f})) {
        if (changed) { updateRuntimeConfig(config); changed = false; }
        status = saveRuntimeProfile(profileName) ? "設定を保存しました" : "設定名または保存先を確認してください";
    }
    ImGui::SeparatorText("保存済み設定");
    ImGui::BeginChild("##saved_configs", {0.0f, 180.0f}, ImGuiChildFlags_Borders);
    for (auto const& name : listRuntimeProfiles())
        if (ImGui::Selectable(name.c_str(), selected == name)) selected = name;
    ImGui::EndChild();
    bool const hasSelection = !selected.empty();
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("読み込む", {110.0f, 31.0f})) {
        bool const loaded = loadRuntimeProfile(selected);
        status = loaded ? "設定を読み込みました" : "設定を読み込めませんでした";
        if (loaded) { config = runtimeConfigSnapshot(); changed = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button("削除", {110.0f, 31.0f})) {
        status = deleteRuntimeProfile(selected) ? "設定を削除しました" : "設定を削除できませんでした";
        selected.clear();
    }
    if (!hasSelection) ImGui::EndDisabled();
    if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
}

void drawLegacyPraxis(void* context) noexcept {
    ImGuiContext* const previousContext = ImGui::GetCurrentContext();
    ImGuiStyle savedStyle{};
    bool styleSaved = false;
    try {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
        savedStyle = ImGui::GetStyle();
        styleSaved = true;

        static int selected = -1; // -1 modules, -2 client, -3 configs, -4 keybinds, -5 about, -6 schematic
        static int categoryFilter = -1;
        static bool favoritesOnly = false;
        static char search[128]{};

        auto config = runtimeConfigSnapshot();
        GuiStyleOptions canonical{};
        canonical.fontFamily = config.guiStyle.fontFamily;
        applyPraxisStyle(canonical);
        bool changed = false;

        auto const display = ImGui::GetIO().DisplaySize;
        float const maxWidth = std::max(160.0f, display.x - 24.0f);
        float const maxHeight = std::max(140.0f, display.y - 24.0f);
        float const minWidth = std::min(720.0f, maxWidth);
        float const minHeight = std::min(440.0f, maxHeight);
        ImGui::SetNextWindowSize({
            std::clamp(display.x * canonical.widthRatio, minWidth, maxWidth),
            std::clamp(display.y * canonical.heightRatio, minHeight, maxHeight)
        }, ImGuiCond_Appearing);
        if (display.x > 0.0f && display.y > 0.0f) {
            ImGui::SetNextWindowSizeConstraints(
                {std::min(540.0f, maxWidth), std::min(360.0f, maxHeight)},
                {maxWidth, maxHeight});
            ImGui::SetNextWindowPos(
                {display.x * 0.5f, display.y * 0.5f},
                ImGuiCond_Appearing,
                {0.5f, 0.5f});
        }
        ImGui::SetNextWindowBgAlpha(canonical.backgroundOpacity);

        if (!ImGui::Begin(
                "Praxis Client###praxis_client",
                nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
            ImGui::End();
            ImGui::GetStyle() = savedStyle;
            ImGui::SetCurrentContext(previousContext);
            return;
        }

        auto pageButton = [&](int page, char const* label) {
            bool const active = selected == page || (page == -1 && selected >= 0);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label, {-1.0f, 36.0f})) selected = page;
            if (active) ImGui::PopStyleColor();
        };
        auto categoryLabel = [](int category) {
            switch (category) {
            case 0: return "描画";
            case 1: return "HUD";
            case 2: return "プレイヤー";
            case 3: return "ワールド";
            case 4: return "その他";
            default: return "その他";
            }
        };
        float const sidebarWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.22f, 136.0f, 194.0f);
        ImGui::BeginChild("##praxis_sidebar", {sidebarWidth, 0.0f}, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("PRAXIS");
        ImGui::TextDisabled("CLIENT");
        ImGui::Separator();

        pageButton(-1, "Modules");
        pageButton(-6, "Schematic");
        pageButton(-2, "Client Settings");
        pageButton(-3, "Config Manager");
        pageButton(-4, "Keybinds");
        pageButton(-5, "About");

        if (selected == -1) {
            ImGui::Spacing();
            ImGui::SeparatorText("カテゴリ");
            auto categoryButton = [&](int category, char const* label) {
                bool const active = categoryFilter == category;
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(label, {-1.0f, 31.0f})) categoryFilter = category;
                if (active) ImGui::PopStyleColor();
            };
            categoryButton(-1, "すべて");
            categoryButton(0, "描画");
            categoryButton(1, "HUD");
            categoryButton(2, "プレイヤー");
            categoryButton(3, "ワールド");
            categoryButton(4, "その他");
            ImGui::Spacing();
            ImGui::Checkbox("お気に入りのみ", &favoritesOnly);
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##praxis_content", {0.0f, 0.0f});
        if (selected == -2) {
            ImGui::TextUnformatted("Client Settings");
            ImGui::SeparatorText("フォント");
            auto family = static_cast<LegacyUiFontFamily>(std::clamp(config.guiStyle.fontFamily, 0, 2));
            char const* preview = legacyUiFontDisplayName(family);
            ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("UIフォント", preview)) {
                for (int value = 0; value < 3; ++value) {
                    auto candidate = static_cast<LegacyUiFontFamily>(value);
                    bool const current = candidate == family;
                    if (ImGui::Selectable(legacyUiFontDisplayName(candidate), current)) {
                        config.guiStyle.fontFamily = value;
                        changed = true;
                    }
                    if (current) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextColored({1.0f, 0.72f, 0.28f, 1.0f}, "フォント変更は次回起動時に反映されます。");
            ImGui::TextWrapped("表示確認: Praxis Client / 日本語テスト / ABC xyz / 0123456789 / !?#");
            ImGui::SeparatorText("画面表示");
            ImGui::TextDisabled("GUI外観は旧Praxis pre-releaseのcanonical設定を使用します。");
        } else if (selected == -3) {
            drawProfiles(config, changed);
        } else if (selected == -4) {
            ImGui::TextUnformatted("Keybinds");
            ImGui::Separator();
            for (auto const& card : cards) {
                ImGui::Text("%s:", card.name);
                ImGui::SameLine();
                if (card.feature == Feature::Zoom) {
                    ImGui::PushID("zoom_bind");
                    if (ImGui::InputInt("VK", &config.zoomKey, 0, 0)) {
                        config.zoomKey = std::clamp(config.zoomKey, 1, 255);
                        changed = true;
                    }
                    ImGui::PopID();
                } else {
                    ImGui::TextDisabled("なし");
                }
                ImGui::Separator();
            }
            ImGui::TextDisabled("GUI: F10");
        } else if (selected == -5) {
            ImGui::TextUnformatted("Praxis Client");
            ImGui::TextDisabled("Minecraft Bedrock 26.51.5 / LeviLamina 26.51.5");
            ImGui::Separator();
            ImGui::TextWrapped("PraxisCompanion utility runtime. UI rendering is ported from the completed legacy Praxis ClickGUI.");
        } else if (selected == -6) {
            ImGui::TextUnformatted("Schematic");
            ImGui::Separator();
            ImGui::TextDisabled("設計図機能はPraxisCompanionには含まれていません。");
            ImGui::TextWrapped("LHoloを導入している場合はLHolo側のメニューから設計図を操作してください。");
        } else if (selected == -1) {
            ImGui::TextUnformatted("機能一覧");
            ImGui::SameLine();
            ImGui::TextDisabled("カードを選択すると詳細設定を開きます");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##module_search", "機能名・説明を検索", search, sizeof(search));
            ImGui::Spacing();

            std::vector<std::size_t> filtered;
            for (std::size_t i = 0; i < cards.size(); ++i) {
                auto const& card = cards[i];
                int const category = card.misc ? 4 : 0;
                bool const favorite = (config.favoriteModules & (1u << i)) != 0;
                if (categoryFilter >= 0 && category != categoryFilter) continue;
                if (favoritesOnly && !favorite) continue;
                if (!matches(card.name, card.description, search)) continue;
                filtered.push_back(i);
            }
            std::stable_sort(filtered.begin(), filtered.end(), [&](std::size_t a, std::size_t b) {
                bool const af = (config.favoriteModules & (1u << a)) != 0;
                bool const bf = (config.favoriteModules & (1u << b)) != 0;
                if (af != bf) return af > bf;
                return std::string_view(cards[a].name) < std::string_view(cards[b].name);
            });
            if (filtered.empty()) {
                ImGui::TextDisabled("条件に一致する機能はありません。");
            } else {
                ImGui::BeginChild("##module_cards", {0.0f, 0.0f});
                float const available = ImGui::GetContentRegionAvail().x;
                float const spacing = ImGui::GetStyle().ItemSpacing.x;
                constexpr float kMinimumCardWidth = 260.0f;
                int columns = std::max(1, static_cast<int>((available + spacing) / (kMinimumCardWidth + spacing)));
                columns = std::min(columns, 3);
                float const cardWidth =
                    (available - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);

                for (std::size_t visibleIndex = 0; visibleIndex < filtered.size(); ++visibleIndex) {
                    std::size_t const i = filtered[visibleIndex];
                    auto const& card = cards[i];
                    if (visibleIndex % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(i));

                    ImVec2 const size{cardWidth, 184.0f};
                    ImVec2 const topLeft = ImGui::GetCursorScreenPos();
                    bool const hovered = ImGui::IsMouseHoveringRect(
                        topLeft, {topLeft.x + size.x, topLeft.y + size.y});
                    ImVec4 cardBackground = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
                    if (hovered) {
                        cardBackground.x = std::min(1.0f, cardBackground.x + 0.045f);
                        cardBackground.y = std::min(1.0f, cardBackground.y + 0.045f);
                        cardBackground.z = std::min(1.0f, cardBackground.z + 0.08f);
                    }
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBackground);
                    ImGui::BeginChild("##module_card", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
                    float const titleHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 2.0f;
                    ImVec2 const titleTopLeft = ImGui::GetCursorScreenPos();
                    ImGui::BeginChild(
                        "##module_title", {0.0f, titleHeight}, ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextWrapped("%s", card.name);
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImVec2 const titleBottomRight{
                        titleTopLeft.x + ImGui::GetContentRegionAvail().x,
                        titleTopLeft.y + titleHeight};
                    if (ImGui::IsMouseHoveringRect(titleTopLeft, titleBottomRight)
                        && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", card.name);

                    ImGui::TextDisabled("%s", categoryLabel(card.misc ? 4 : 0));
                    ImGui::BeginChild("##description", {0.0f, 49.0f});
                    ImGui::TextWrapped("%s", card.description);
                    ImGui::EndChild();
                    ImGui::Separator();

                    bool cardControlActivated = false;
                    if (drawCardIcon("##settings", false)) {
                        cardControlActivated = true;
                        selected = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("設定を開く");
                    ImGui::SameLine();

                    bool const favorite = (config.favoriteModules & (1u << i)) != 0;
                    if (drawCardIcon("##favorite", true, favorite)) {
                        cardControlActivated = true;
                        config.favoriteModules ^= (1u << i);
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(favorite ? "お気に入りから外す" : "お気に入りに追加");
                    ImGui::SameLine();
                    bool* enabled = enabledField(config, card.feature);
                    bool const isEnabled = enabled && *enabled;
                    if (isEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    ImGui::SetCursorPosX(std::max(
                        ImGui::GetCursorPosX(),
                        ImGui::GetWindowContentRegionMax().x - 52.0f - 5.0f));
                    if (ImGui::Button(isEnabled ? "ON" : "OFF", {52.0f, 27.0f})) {
                        cardControlActivated = true;
                        if (enabled) {
                            *enabled = !*enabled;
                            changed = true;
                        }
                    }
                    if (isEnabled) ImGui::PopStyleColor();

                    ImGui::EndChild();
                    ImGui::PopStyleColor();

                    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !cardControlActivated)
                        selected = static_cast<int>(i);
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        } else if (selected >= 0 && selected < static_cast<int>(cards.size())) {
            auto const& card = cards[static_cast<std::size_t>(selected)];
            if (ImGui::Button("< 機能一覧へ戻る")) {
                selected = -1;
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", categoryLabel(card.misc ? 4 : 0));
                ImGui::Separator();
                ImGui::TextWrapped("%s", card.name);
                ImGui::TextWrapped("%s", card.description);
                ImGui::Spacing();

                bool* enabled = enabledField(config, card.feature);
                bool const isEnabled = enabled && *enabled;
                if (isEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(
                        isEnabled ? "有効 (クリックで無効)" : "無効 (クリックで有効)",
                        {190.0f, 34.0f})) {
                    if (enabled) {
                        *enabled = !*enabled;
                        changed = true;
                    }
                }
                if (isEnabled) ImGui::PopStyleColor();

                ImGui::SameLine();
                bool const favorite = (config.favoriteModules & (1u << selected)) != 0;
                if (ImGui::Button(
                        favorite ? "お気に入り済み" : "お気に入りに追加",
                        {150.0f, 34.0f})) {
                    config.favoriteModules ^= (1u << selected);
                    changed = true;
                }

                ImGui::Text("キー割り当て: %s",
                            card.feature == Feature::Zoom ? "Zoom設定のVK" : "なし");
                ImGui::SeparatorText("機能設定");
                drawDetails(config, card.feature, changed);
            }
        }

        ImGui::EndChild();
        ImGui::End();

        if (changed) updateRuntimeConfig(config);
        ImGui::GetStyle() = savedStyle;
        ImGui::SetCurrentContext(previousContext);
    } catch (...) {
        if (styleSaved) ImGui::GetStyle() = savedStyle;
        ImGui::SetCurrentContext(previousContext);
    }
}


void drawPraxis(void* context) noexcept {
    drawLegacyPraxis(context);
    return;
    ImGuiContext* const previousContext = ImGui::GetCurrentContext();
    ImGuiStyle savedStyle{};
    bool styleSaved = false;
    try {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
        savedStyle = ImGui::GetStyle();
        styleSaved = true;
        static int category = -1;
        static int selected = -1;
        static bool favoritesOnly = false;
        static char search[96]{};
        auto config = runtimeConfigSnapshot();
        applyPraxisStyle(config.guiStyle);
        bool changed = false;
        auto display = ImGui::GetIO().DisplaySize;
        float const maxWidth = std::max(160.0f, display.x - 24.0f);
        float const maxHeight = std::max(140.0f, display.y - 24.0f);
        ImGui::SetNextWindowSize({std::clamp(display.x * config.guiStyle.widthRatio,
                                             std::min(720.0f, maxWidth), maxWidth),
                                  std::clamp(display.y * config.guiStyle.heightRatio,
                                             std::min(440.0f, maxHeight), maxHeight)}, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::SetNextWindowBgAlpha(config.guiStyle.backgroundOpacity);
        bool open = true;
        if (ImGui::Begin("Praxis Client###praxis_companion", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
            float const sidebar = std::clamp(ImGui::GetContentRegionAvail().x * 0.22f, 136.0f, 194.0f);
            ImGui::BeginChild("##praxis_sidebar", {sidebar, 0}, ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("PRAXIS");
            ImGui::TextDisabled("CLIENT");
            ImGui::Separator();
            if (ImGui::Button("Modules", {-1, 36})) selected = -1;
            if (ImGui::Button("Client Settings", {-1, 36})) selected = -2;
            if (ImGui::Button("Config Manager", {-1, 36})) selected = -3;
            if (ImGui::Button("Keybinds", {-1, 36})) selected = -4;
            if (ImGui::Button("About", {-1, 36})) selected = -5;
            ImGui::SeparatorText("カテゴリ");
            if (ImGui::Button("すべて", {-1, 31})) category = -1;
            if (ImGui::Button("描画", {-1, 31})) category = 0;
            if (ImGui::Button("その他", {-1, 31})) category = 1;
            ImGui::Spacing();
            ImGui::Checkbox("お気に入りのみ", &favoritesOnly);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##praxis_content", {0, 0});
            if (selected == -2) {
                ImGui::TextUnformatted("Client Settings");
                ImGui::SeparatorText("画面表示");
                drawStyleDetails(config.guiStyle, changed);
            } else if (selected == -3) {
                drawProfiles(config, changed);
            } else if (selected == -4) {
                ImGui::TextUnformatted("Keybinds");
                ImGui::Separator();
                for (auto const& card : cards) {
                    ImGui::Text("%s:", card.name);
                    ImGui::SameLine();
                    if (card.feature == Feature::Zoom) {
                        ImGui::PushID("zoom_bind");
                        if (ImGui::InputInt("VK", &config.zoomKey, 0, 0)) {
                            config.zoomKey = std::clamp(config.zoomKey, 1, 255);
                            changed = true;
                        }
                        ImGui::PopID();
                    } else ImGui::TextDisabled("なし");
                    ImGui::Separator();
                }
                ImGui::TextDisabled("GUI: F10 / LHolo: Insert");
            } else if (selected == -5) {
                ImGui::TextUnformatted("Praxis Client");
                ImGui::TextDisabled("Minecraft Bedrock 26.51.5 / LeviLamina 26.51.5");
                ImGui::Separator();
                ImGui::TextWrapped("7つのutility機能を単体で利用できます。LHoloとの併用時は共有GUI bridgeを使用します。");
            } else if (selected < 0) {
                ImGui::TextUnformatted("機能一覧");
                ImGui::SameLine();
                ImGui::TextDisabled("カードを選択すると詳細設定を開きます");
                ImGui::InputTextWithHint("##module_search", "機能名・説明を検索", search, sizeof(search));
                ImGui::Separator();
                float const cardWidth = std::max(190.0f, (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f);
                int shown = 0;
                for (std::size_t i = 0; i < cards.size(); ++i) {
                    auto const& card = cards[i];
                    if (category >= 0 && static_cast<int>(card.misc) != category) continue;
                    bool const isFavorite = (config.favoriteModules & (1u << i)) != 0;
                    if (favoritesOnly && !isFavorite) continue;
                    if (!matches(card.name, card.description, search)) continue;
                    ImGui::PushID(static_cast<int>(i));
                    if (shown++ % 2) ImGui::SameLine();
                    ImGui::BeginChild("##card", {cardWidth, 184.0f}, ImGuiChildFlags_Borders);
                    ImGui::TextUnformatted(card.name);
                    ImGui::TextDisabled("%s", card.misc ? "その他" : "描画");
                    ImGui::TextWrapped("%s", card.description);
                    bool const selectable = backendSelectable(card.feature);
                    if (!selectable) ImGui::BeginDisabled();
                    if (auto* enabled = enabledField(config, card.feature))
                        changed |= ImGui::Checkbox("有効", enabled);
                    if (!selectable) ImGui::EndDisabled();
                    ImGui::Separator();
                    if (drawCardIcon("##settings", false)) selected = static_cast<int>(i);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("設定を開く");
                    ImGui::SameLine();
                    if (drawCardIcon("##favorite", true, isFavorite)) {
                        config.favoriteModules ^= 1u << i;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("お気に入りを切り替える");
                    ImGui::SameLine();
                    if (ImGui::Button("詳細", {62.0f, 27.0f})) selected = static_cast<int>(i);
                    ImGui::EndChild();
                    ImGui::PopID();
                }
            } else if (selected < static_cast<int>(cards.size())) {
                auto const& card = cards[static_cast<std::size_t>(selected)];
                if (ImGui::Button("← 戻る")) selected = -1;
                ImGui::Separator();
                ImGui::TextUnformatted(card.name);
                ImGui::TextWrapped("%s", card.description);
                bool const selectable = backendSelectable(card.feature);
                if (!selectable) ImGui::BeginDisabled();
                if (auto* enabled = enabledField(config, card.feature))
                    changed |= ImGui::Checkbox("有効", enabled);
                if (!selectable) ImGui::EndDisabled();
                ImGui::SeparatorText("設定");
                drawDetails(config, card.feature, changed);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        if (changed) updateRuntimeConfig(config);
        if (!open) closePraxisGui();
        ImGui::GetStyle() = savedStyle;
        ImGui::SetCurrentContext(previousContext);
    } catch (...) {
        closePraxisGui();
        if (styleSaved) ImGui::GetStyle() = savedStyle;
        ImGui::SetCurrentContext(previousContext);
    }
}

void onVisibility(bool visible) noexcept {
    Renderer::get().setGuiOpen(visible);
    if (!visible) flushRuntimeConfig();
    logGuiInfo(visible ? "LHolo bridge: F10 OPEN" : "LHolo bridge: F10 CLOSE");
}
bool hudNeeded() noexcept { return frameHudNeeded.load(std::memory_order_acquire); }
void drawChunkHud(void* context) noexcept {
    ImGuiContext* const previousContext = ImGui::GetCurrentContext();
    try {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
        auto current = settings();
        if (!current->on(Feature::ChunkBorder) || !current->chunkBorder.showHud
            || !chunkHudPlayerSeen.load(std::memory_order_acquire)) {
            ImGui::SetCurrentContext(previousContext);
            return;
        }
        ImGui::SetNextWindowPos({10, 300}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("Chunks##praxis_overlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::TextColored({0.3f, 0.9f, 0.6f, 1.0f}, "Chunk Borders");
        ImGui::Text("Render Distance: %d", current->chunkBorder.renderDistance);
        ImGui::Text("Current Chunk: %d / %d", chunkHudX.load(), chunkHudZ.load());
        ImGui::Text("In-Chunk: %d / %d", chunkHudInX.load(), chunkHudInZ.load());
        if (current->chunkBorder.showYCoordinates)
            ImGui::Text("Y Span: %d - %d", chunkHudY.load() - current->chunkBorder.heightBelow,
                        chunkHudY.load() + current->chunkBorder.heightAbove);
        ImGui::End();
        ImGui::SetCurrentContext(previousContext);
    } catch (...) { ImGui::SetCurrentContext(previousContext); }
}
} // namespace

void drawPraxisContent(void* context) noexcept { drawPraxis(context); }
void drawChunkHudContent(void* context) noexcept { drawChunkHud(context); }
bool chunkHudNeeded() noexcept { return hudNeeded(); }

bool lholoBridgeCompatible() noexcept {
    try {
        auto mod = ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
        if (!mod || mod->getType() != ll::mod::NativeModManagerName || !mod->isEnabled()) return false;
        auto provider = std::static_pointer_cast<ll::mod::NativeMod>(mod);
        if (!provider->getHandle()) return false;
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(provider->getHandle()), &module)) return false;
        bool const compatible = GetProcAddress(module, "lholo_register_companion_gui_v2")
            && GetProcAddress(module, "lholo_unregister_companion_gui_v2")
            && GetProcAddress(module, "lholo_companion_gui_registered_v2")
            && GetProcAddress(module, "lholo_close_companion_gui_v2")
            && GetProcAddress(module, "lholo_menu_input_captured_v2");
        FreeLibrary(module);
        return compatible;
    } catch (...) { return false; }
}

void quiesceBridgeReaders() noexcept {
    registeredFn.store(nullptr, std::memory_order_release);
    inputCapturedFn.store(nullptr, std::memory_order_release);
    while (bridgeReaders.load(std::memory_order_acquire) != 0) Sleep(0);
}

void releaseLocalBridge() noexcept {
    registered = false;
    bridgeOwner.store(nullptr, std::memory_order_release);
    closeFn.store(nullptr, std::memory_order_release);
    unregisterFn = nullptr;
    Renderer::get().setGuiOpen(false);
    if (auto module = bridgeModule.exchange(nullptr, std::memory_order_acq_rel)) FreeLibrary(module);
}

bool startLholoBridge() noexcept {
    if (!runtimeReady.load(std::memory_order_acquire)) return false;
    bridgeReaders.fetch_add(1, std::memory_order_acquire);
    auto checkRegistered = registeredFn.load(std::memory_order_acquire);
    auto owner = bridgeOwner.load(std::memory_order_acquire);
    bool const active = checkRegistered && owner && checkRegistered(owner);
    bridgeReaders.fetch_sub(1, std::memory_order_release);
    if (active) return true;
    if (GetTickCount64() < nextBridgeAttempt.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(bridgeMutex);
    if (!runtimeReady.load(std::memory_order_acquire)) return false;
    auto const currentRegistration = registeredFn.load(std::memory_order_acquire);
    if (registered && currentRegistration && currentRegistration(bridgeOwner.load(std::memory_order_acquire)))
        return true;
    if (registered) {
        quiesceBridgeReaders();
        if (unregisterFn) unregisterFn(bridgeOwner.load(std::memory_order_acquire));
        releaseLocalBridge();
    }
    auto const now = GetTickCount64();
    if (now < nextBridgeAttempt.load(std::memory_order_acquire)) return false;
    nextBridgeAttempt.store(now + 500, std::memory_order_release);
    auto mod = ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
    if (!mod || mod->getType() != ll::mod::NativeModManagerName || !mod->isEnabled()) return false;
    auto provider = std::static_pointer_cast<ll::mod::NativeMod>(mod);
    if (!provider->getHandle()) return false;
    HMODULE module{};
    owner = GetModuleHandleW(L"PraxisCompanion.dll");
    if (!owner || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(provider->getHandle()), &module)) return false;
    auto registerFn = reinterpret_cast<RegisterFn>(GetProcAddress(module, "lholo_register_companion_gui_v2"));
    auto unregister = reinterpret_cast<UnregisterFn>(GetProcAddress(module, "lholo_unregister_companion_gui_v2"));
    auto isRegistered = reinterpret_cast<RegisteredFn>(GetProcAddress(module, "lholo_companion_gui_registered_v2"));
    auto close = reinterpret_cast<CloseFn>(GetProcAddress(module, "lholo_close_companion_gui_v2"));
    auto inputCaptured = reinterpret_cast<InputCapturedFn>(GetProcAddress(module, "lholo_menu_input_captured_v2"));
    if (!registerFn || !unregister || !isRegistered || !close || !inputCaptured
        || !registerFn(owner, VK_F10, drawPraxis, drawChunkHud, hudNeeded, onVisibility,
                       IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImDrawVert))) {
        FreeLibrary(module);
        return false;
    }
    bridgeModule.store(module, std::memory_order_release);
    bridgeOwner.store(owner, std::memory_order_release);
    unregisterFn = unregister;
    closeFn.store(close, std::memory_order_release);
    inputCapturedFn.store(inputCaptured, std::memory_order_release);
    registeredFn.store(isRegistered, std::memory_order_release);
    registered = true;
    nextBridgeAttempt.store(0, std::memory_order_release);
    logGuiInfo("LHolo bridge registration PASS");
    return true;
}

bool stopLholoBridge() noexcept {
    std::lock_guard lock(bridgeMutex);
    if (!registered) return true;
    auto checkRegistered = registeredFn.load(std::memory_order_acquire);
    auto inputCaptured = inputCapturedFn.load(std::memory_order_acquire);
    quiesceBridgeReaders();
    bool const removed = unregisterFn && unregisterFn(bridgeOwner.load(std::memory_order_acquire));
    if (!removed) {
        registeredFn.store(checkRegistered, std::memory_order_release);
        inputCapturedFn.store(inputCaptured, std::memory_order_release);
    }
    if (!removed) return false;
    releaseLocalBridge();
    return true;
}

bool lholoBridgeInputCaptured() noexcept {
    if (Renderer::get().isGuiOpen()) return true;
    bridgeReaders.fetch_add(1, std::memory_order_acquire);
    auto fn = inputCapturedFn.load(std::memory_order_acquire);
    bool const captured = fn && fn();
    bridgeReaders.fetch_sub(1, std::memory_order_release);
    return captured;
}
void closeLholoBridge() noexcept {
    if (auto close = closeFn.load(std::memory_order_acquire)) close();
}
} // namespace praxis::companion
