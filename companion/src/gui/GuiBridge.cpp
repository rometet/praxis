#include "gui/GuiBridge.h"
#include "gui/ResourcePackPage.h"
#include "runtime/Runtime.h"
#include "core/WeatherChangerDiagnostics.h"
#include "runtime/InformationRuntime.h"
#include "runtime/ConfigStorage.h"
#include "render/Renderer.h"
#include "render/ShulkerPreview.h"
#include "render/InformationHud.h"
#include "render/PlayerHud.h"
#include "gui/HudTools.h"
#include "gui/JapaneseText.h"
#include "platform/HandRestock.h"
#include "gui/PresentationHints.h"
#include "map/MapRuntime.h"
#include "map/MapGraphics.h"
#include "core/Settings.h"
#include "core/CoexistencePolicy.h"
#include "core/BlockStateComparePolicy.h"
#include "gui/LegacyUiFont.h"
#include "platform/CameraCandidate.h"

#ifndef PRAXIS_GUI_HEADLESS_TEST
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#endif

#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>


namespace praxis::companion {
namespace {
using DrawFn = void (*)(void*) noexcept;
using StateFn = void (*)(bool) noexcept;
using HudNeededFn = bool (*)() noexcept;
using RegisterV2Fn = bool (__cdecl *)(void*, unsigned, DrawFn, DrawFn, HudNeededFn, StateFn,
                                     unsigned, std::size_t, std::size_t, std::size_t) noexcept;
using UnregisterFn = bool (__cdecl *)(void*) noexcept;
using RegisteredFn = bool (__cdecl *)(void*) noexcept;
using CloseFn = void (__cdecl *)() noexcept;
using InputCapturedFn = bool (__cdecl *)() noexcept;
using FontInitializerFn = void (*)(void*) noexcept;
using SetFontInitializerFn = bool (__cdecl *)(void*, FontInitializerFn) noexcept;
using SharedGraphicsFn = void (*)(void*, void*) noexcept;
using GraphicsResetFn = void (*)() noexcept;
using SetGraphicsCallbacksFn = bool (__cdecl *)(void*, SharedGraphicsFn, GraphicsResetFn) noexcept;


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
std::atomic_bool sharedFontRequired{};
std::atomic_bool sharedInitializerAccepted{};
struct LegacyGuiState {
    int selected = -1;
    int categoryFilter = -1;
    bool favoritesOnly{};
    int settingKeybind = -1;
    char search[128]{};
    std::optional<BlockStateSample> pinnedBlock;
};
LegacyGuiState legacyGuiState;
// Owned snapshot captured exactly when F10 opens. Read-only until next opening.
std::atomic<std::shared_ptr<BlockStateSample const>> blockOnMenuOpen;
std::atomic_bool praxisMenuVisible{};
std::atomic_bool openModuleListPending{};

void prepareSharedMapGraphics(void* device, void* deviceContext) noexcept {
    map::setMapGraphics(static_cast<ID3D11Device*>(device), static_cast<ID3D11DeviceContext*>(deviceContext));
}
void resetSharedMapGraphics() noexcept { map::clearMapGraphics(); }

class GuiContextScope {
public:
    explicit GuiContextScope(void* context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    }
    ~GuiContextScope() { ImGui::SetCurrentContext(previous_); }
private:
    ImGuiContext* previous_{};
};

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
    ModuleCard{Feature::TimeChanger, "TimeChanger", "昼の表示に固定", false},
    ModuleCard{Feature::WeatherChanger, "WeatherChanger", "天気の表示を切り替え", false},
    ModuleCard{Feature::Hitbox, "Hitbox", "エンティティの枠を表示", false},
    ModuleCard{Feature::ToolProtection, "ToolProtection", "道具が壊れる前に採掘を停止", true},
    ModuleCard{Feature::WeaponSwitch, "WeaponSwitch", "攻撃時にホットバーの武器を選択", true},
    ModuleCard{Feature::HandRestock, "HandRestock", "使用後にメインハンドを補充", true},
    ModuleCard{Feature::InventorySort, "InventorySort", "カーソルを合わせた領域を整理", true},
    ModuleCard{Feature::InventoryTransfer, "InventoryTransfer", "収納と手持ちの間で移動", true},
    ModuleCard{Feature::Freelook, "Freelook", "体の向きを保って周囲を見回す", false},
    ModuleCard{Feature::FreeCamera, "FreeCamera", "WASD・Space・Shiftでカメラを移動", false},
    ModuleCard{Feature::AntiEffect, "AntiEffect", "炎・ポータルの画面効果を抑制", false},
    ModuleCard{Feature::OffhandSwap, "OffhandSwap", "キー操作でメインとオフハンドを持ち替え", true},
    ModuleCard{Feature::FlexibleBlockPlacement, "FlexibleBlockPlacement", "R/G/Hを押して5領域の配置・回転・隣接を選択", true},
};
static_assert(cards.size() <= featureCount);
constexpr unsigned favoriteSlot(unsigned card) noexcept {
 return favoriteSlotForCard(card,cards[card].feature);
}

int moduleCategory(ModuleCard const& card) noexcept {
 switch(card.feature){
 case Feature::ToolProtection:case Feature::WeaponSwitch:case Feature::HandRestock:
  case Feature::InventorySort:case Feature::InventoryTransfer:case Feature::OffhandSwap:return 2;
 default:return card.misc?4:0;
 }
}
bool* enabledField(RuntimeConfig& config, Feature feature) noexcept {
    switch (feature) {
    case Feature::OffhandSwap: return &config.offhandSwap;
    case Feature::NoRender: return &config.noRender.enabled;
    case Feature::ChunkBorder: return &config.chunkBorder.enabled;
    case Feature::Fullbright: return &config.fullbright;
    case Feature::ShulkerPreview: return &config.shulkerPreview;
    case Feature::RedstoneOverlay: return &config.redstoneOverlay;
    case Feature::LightOverlay: return &config.lightOverlay;
    case Feature::Zoom: return &config.zoom;
    case Feature::TimeChanger: return &config.timeChanger;
    case Feature::WeatherChanger: return &config.weatherChanger;
    case Feature::ToolProtection: return &config.toolProtection;
    case Feature::WeaponSwitch: return &config.weaponSwitch;
    case Feature::HandRestock: return &config.handRestock;
    case Feature::InventorySort: return &config.inventorySort;
    case Feature::InventoryTransfer: return &config.inventoryTransfer;
    case Feature::Hitbox: return &config.hitboxOptions.enabled;
    case Feature::Freelook: return &config.freelook;
    case Feature::FreeCamera: return &config.freeCamera;
    case Feature::AntiEffect: return &config.antiEffect;
    case Feature::FlexibleBlockPlacement: return &config.flexibleBlockPlacement;
    default: return nullptr;
    }
}

bool backendSelectable(Feature feature) noexcept {
    // Persist requests on the title screen; RuntimePolicy gates their effect
    // until each native backend becomes available in a world.
    return (ownedFeatureSlotMask & featureRegistryBit(static_cast<unsigned>(feature))) != 0;
}

bool modifierSetting(char const* label,unsigned& value) {
 constexpr char const* names[]{"なし","Ctrl","Shift","Ctrl+Shift","Alt","Ctrl+Alt","Shift+Alt","Ctrl+Shift+Alt","修飾キーを問わない (従来)"};
 int index=static_cast<int>(value);
 if(!ImGui::Combo(label,&index,names,9))return false;
 value=static_cast<unsigned>(index);return true;
}
std::string keyName(int vk) {
    if (vk <= 0 || vk >= 256) return "なし";
    wchar_t name[64]{};
    UINT const scanCode = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scanCode << 16);
    switch (vk) {
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
    case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
    case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        lParam |= 1 << 24;
        break;
    default: break;
    }
    if (GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) > 0) {
        char utf8[256]{};
        if(WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof(utf8),nullptr,nullptr)>0)return utf8;
    }
    return "VK " + std::to_string(vk);
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
    // Quiet surfaces by default. Reserve the accent for selected pages
    // and intentional interactions instead of tinting every neutral button.
    style.Colors[ImGuiCol_WindowBg] = styleColor(options.background, options.backgroundOpacity);
    style.Colors[ImGuiCol_ChildBg] = styleColor(options.background, options.backgroundOpacity * 0.74f);
    style.Colors[ImGuiCol_Border] = styleColor(options.accentOutline, options.backgroundOpacity * 0.52f);
    style.Colors[ImGuiCol_Button] = styleColor(options.accentOutline, 0.40f);
    style.Colors[ImGuiCol_ButtonHovered] = styleColor(options.accentOutline, 0.80f);
    style.Colors[ImGuiCol_ButtonActive] = styleColor(options.accent, 0.85f);
    style.Colors[ImGuiCol_Header] = styleColor(options.accentOutline, 0.48f);
    style.Colors[ImGuiCol_HeaderHovered] = styleColor(options.accentOutline, 0.77f);
    style.Colors[ImGuiCol_HeaderActive] = styleColor(options.accent, 0.80f);
    style.Colors[ImGuiCol_Separator] = styleColor(options.accentOutline, 0.48f);
    style.Colors[ImGuiCol_FrameBg] = styleColor(options.background, options.backgroundOpacity * 0.82f);
    style.Colors[ImGuiCol_FrameBgHovered] = styleColor(options.accentOutline, 0.55f);
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

#ifdef PRAXIS_GUI_HEADLESS_TEST
struct ToolPercentLabelProbe {
    ImGuiWindow* window{};
    ImGuiID widgetId{};
    float visibleWidth{}, legacyWidth{}, availableWidth{}, labelRight{}, clipRight{};
} toolPercentLabelProbe;
#endif
void drawPreviewSettings(RuntimeConfig& config, bool& changed);
void drawDetails(RuntimeConfig& config, Feature feature, bool& changed) {
    switch (feature) {
    case Feature::AntiEffect: {
        auto& o=config.antiEffectOptions;auto available=availableRenderPasses.load();
        ImGui::TextWrapped("初期はOFFです。炎・ポータルの表示だけを抑制します。ゲームの効果状態は維持します。");
        auto effect=[&](char const* label,RenderPass pass,bool& value) {
            bool supported=(available&passBit(pass))!=0;
            ImGui::BeginDisabled(!supported);changed |= ImGui::Checkbox(label,&value);ImGui::EndDisabled();
            if(!supported)ImGui::TextDisabled("現在の描画経路では未対応");
        };
        effect("炎の表示を抑制",RenderPass::FireOverlay,o.fire);
        effect("ポータルの表示を抑制",RenderPass::PortalOverlay,o.portal);
        ImGui::TextDisabled("暗闇 (Darkness): 未対応");
        ImGui::TextDisabled("盲目 (Blindness): 未対応");
        ImGui::TextDisabled("吐き気 (Nausea): 未対応");
        ImGui::TextWrapped("3効果を個別に抑制する安全な描画契約を確認できていません。NoRenderでも同じ表示を隠している場合は、その設定をOFFにするまで抑制が続きます。");
        break;
    }
    case Feature::NoRender: {
        ImGui::TextUnformatted("描画パス");
        RenderMask const available = availableRenderPasses.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < kRenderPassCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto const pass = static_cast<RenderPass>(i);
            bool const supported = (available & passBit(pass)) != 0;
            if (!supported) ImGui::BeginDisabled();
            changed |= ImGui::Checkbox(renderPassDisplayNames[i], &config.noRender.passes[i]);
            if (!supported) ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("未対応");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("26.51.5で安全な抑止経路を確認できていないため無効です。");
            }
            ImGui::PopID();
        }
        break;
    }
    case Feature::ChunkBorder: {
        auto& c = config.chunkBorder;
        changed |= ImGui::SliderInt("表示距離 (チャンク)", &c.renderDistance, 1, 12);
        changed |= ImGui::SliderInt("上の高さ", &c.heightAbove, 4, 128);
        changed |= ImGui::SliderInt("下の高さ", &c.heightBelow, 2, 96);
        changed |= ImGui::SliderFloat("線の太さ", &c.lineWidth, 0.5f, 5.0f);
        changed |= ImGui::SliderInt("最大線数", &c.lineBudget, 256, 8192);
        changed |= ImGui::Checkbox("リージョン境界", &c.showRegions);
        changed |= ImGui::Checkbox("高さを表示", &c.showYCoordinates);
        ImGui::TextWrapped("チャンク情報HUDは左の「HUD」で設定できます。");
        auto chunk = colorFloats(c.chunkColor);
        if (ImGui::ColorEdit4("チャンク境界の色", chunk.data())) {
            c.chunkColor = packedAbgr(chunk); changed = true;
        }
        auto region = colorFloats(c.regionColor);
        if (ImGui::ColorEdit4("リージョン境界の色", region.data())) {
            c.regionColor = packedAbgr(region); changed = true;
        }
        break;
    }
    case Feature::Hitbox: {
        auto& o=config.hitboxOptions;
        changed |= ImGui::Checkbox("半透明",&o.transparent);
        changed |= ImGui::Checkbox("未分類も表示",&o.showUnclassified);
        changed |= ImGui::SliderFloat("表示距離",&o.range,8,192,"%.0f");
        changed |= ImGui::SliderInt("最大表示数",&o.maxActors,8,256);
        auto color=colorFloats(o.color);
        if(ImGui::ColorEdit4("枠の色",color.data())) {o.color=packedAbgr(color);changed=true;}
        break;
    }
    case Feature::Fullbright:
        changed |= ImGui::SliderFloat("明るさ", &config.brightness, 1.0f, 25.0f, "%.1f");
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
        disabledTextWrapped("粉・コンパレーター: 0〜15 / リピーター・トーチ: 切・入");
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
        disabledTextWrapped("インベントリのシュルカーボックスにカーソルを合わせると表示");
        ImGui::SeparatorText("バンドル・ツールチップ");
        drawPreviewSettings(config, changed);
        break;
    }
    case Feature::TimeChanger:
        ImGui::TextUnformatted("表示時刻: 昼 (6000)");
        ImGui::TextWrapped("無効にするとサーバーの実際の時刻表示に戻ります。");
        break;
    case Feature::WeatherChanger: {
        int selected=static_cast<int>(config.weatherOptions.mode);
        if (ImGui::Combo("表示天気", &selected, "晴れ\0雨\0雪\0")) {
            config.weatherOptions.mode=static_cast<unsigned>(std::clamp(selected,0,2)); changed=true;
        }
        if (config.weatherOptions.mode!=static_cast<unsigned>(WeatherMode::Clear))
            changed |= ImGui::SliderFloat("雨・雪の密度", &config.weatherOptions.intensity, 1.f, 30.f, "%.1f");
        ImGui::TextWrapped("無効にすると現在の天気表示に戻ります。「NoRender」の「Weather」が有効な間は非表示が続きます。");
        if (ImGui::TreeNode("実機確認用の診断")) {
            auto d = weatherChangerDiagnosticSnapshot();
            ImGui::Text("描画の範囲: 対象 %llu / 対象外 %llu",
                static_cast<unsigned long long>(d.acceptedRenderScopes),
                static_cast<unsigned long long>(d.rejectedRenderScopes));
            ImGui::Text("表示の生成: 呼出 %llu / 対象内 %llu / 適用 %llu",
                static_cast<unsigned long long>(d.viewCalls),
                static_cast<unsigned long long>(d.viewsInScope),
                static_cast<unsigned long long>(d.viewApplications));
            ImGui::Text("天気の更新: 呼出 %llu / 対象内 %llu",
                static_cast<unsigned long long>(d.weatherMoveCalls),
                static_cast<unsigned long long>(d.weatherMovesInScope));
            ImGui::Text("空の更新: 呼出 %llu / 対象内 %llu",
                static_cast<unsigned long long>(d.skyMoveCalls),
                static_cast<unsigned long long>(d.skyMovesInScope));
            ImGui::Text("雨しぶき: 呼出 %llu / 対象 %llu / 非表示 %llu",
                static_cast<unsigned long long>(d.rainUpdateCalls),
                static_cast<unsigned long long>(d.rainUpdatesInScope),
                static_cast<unsigned long long>(d.rainSplashPositionsRemoved));
            auto trial=weatherResultTrial->snapshot();
            ImGui::BeginDisabled(trial.pending);
            if (ImGui::Button("30秒の診断を開始")) resetWeatherChangerDiagnostics();
            ImGui::EndDisabled();
            if(trial.pending && trial.end==WeatherTrialEnd::None && ImGui::Button("診断を停止"))
                weatherResultTrial->stop(WeatherTrialEnd::UserStopped);
            ImGui::Text("診断 #%llu: %s / 保存 %u/%u / flush %s",
                static_cast<unsigned long long>(trial.session),trial.session?weatherTrialEndName(trial.end):"OFF",
                trial.flushedReports,weatherTrialReportLimit,trial.lastFlushSucceeded?"OK":"未確認");
            ImGui::Text("最新の記録: 生成 %llu / Sky入力 %llu / 競合drop %llu",
                static_cast<unsigned long long>(trial.producerCalls),static_cast<unsigned long long>(trial.skyInputCalls),
                static_cast<unsigned long long>(trial.dropped));
            if(trial.producerSeen) {
                auto const& p=trial.producer;
                ImGui::Text("mode %u / ON %d / weatherApplied %d / skyApplied %d",
                    p.key.requested.mode,p.key.effectiveOn,p.applied,p.skyApplied);
                ImGui::Text("native rain %.3f snow %.3f -> owned rain %.3f snow %.3f / draw %d",
                    p.native.rain,p.native.snow,p.owned.rain,p.owned.snow,p.owned.draw);
                ImGui::Text("資源の有無: mesh %d texture %d RainMat %d SnowMat %d / count %d",
                    p.resources.mesh,p.resources.textureClient,p.resources.rainMaterial,p.resources.snowMaterial,p.resources.particleCount);
            }
            ImGui::TextWrapped("開始した時だけ config フォルダの親の weather-trial.log に毎秒記録します。終了理由・保存状態はここにも残ります。skyApplied=0 は native の空を保持します。資源の有無・Sky入力は Weather の最終描画成功を示しません。");
            ImGui::TreePop();
        }
        break;
    }
    case Feature::Zoom: {
        auto& z = config.zoomOptions;
        changed |= ImGui::Checkbox("押している間だけ", &z.hold);
        if (ImGui::InputInt("ズームキー (番号)", &config.zoomKey, 0, 0)) {
            config.zoomKey = std::clamp(config.zoomKey, 1, 255);
            changed = true;
        }
        if (ImGui::SliderFloat("視野角", &z.targetFov, 5.0f, 60.0f, "%.0f")) {
            config.zoomMultiplier = z.targetFov / 70.0f; changed = true;
        }
        changed |= ImGui::Checkbox("なめらかに切替", &z.animation);
        if (z.animation) changed |= ImGui::SliderFloat("切替速度", &z.animationSpeed, 1.0f, 30.0f);
        changed |= ImGui::Checkbox("ホイールで倍率調整", &z.scrollAdjust);
        if (z.scrollAdjust) changed |= ImGui::SliderFloat("ホイール感度", &z.scrollSensitivity, 0.5f, 10.0f);
        changed |= ImGui::Checkbox("感度補正", &z.adjustCameraSensitivity);
        changed |= ImGui::Checkbox("視点をなめらかに", &z.smoothCamera);
        if (z.smoothCamera)
            changed |= ImGui::SliderFloat("視点の補正倍率", &z.cameraSmoothingMultiplier, 0.1f, 5.0f);
        break;
    }
    case Feature::Freelook:
    case Feature::FreeCamera: {
        bool const free=feature==Feature::FreeCamera;
        auto& o=free?config.freeCameraOptions:config.freelookOptions;
        auto const cameraSlot=static_cast<unsigned>(feature);
        auto const& flexible=config.flexiblePlacementOptions;
        if(config.flexibleBlockPlacement
            && cameraSlot<config.keybinds.size()
            && cameraFlexiblePlacementHotkeyConflict(
                {static_cast<unsigned>(config.keybinds[cameraSlot]),
                 cameraHotkeyModifiers(config.keybindModifiers[cameraSlot]),1},
                true,
                {static_cast<unsigned>(flexible.rotationKey),flexible.rotationModifiers,1},
                {static_cast<unsigned>(flexible.offsetKey),flexible.offsetModifiers,1},
                {static_cast<unsigned>(flexible.adjacentKey),flexible.adjacentModifiers,1})) {
            ImGui::TextWrapped("Hotkey conflict: Flexible Block Placement takes priority. "
                "Assign a different hotkey to this camera mode to enable it.");
        }
        if(free)
            ImGui::TextWrapped("起動キーを押すたびに開始・終了します。長押しは不要です。");
        else {
            changed |= ImGui::Checkbox("押す間だけ", &o.hold);
            ImGui::TextWrapped("OFFにすると起動キーを押すたびに開始・終了します。");
        }
        ImGui::TextWrapped("視点の感度");
        ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::SliderFloat("##cameraSensitivity", &o.degreesPerPixel, .01f, 1.f, "%.2f");
        if(free) {
            ImGui::TextWrapped("移動速度");
            ImGui::SetNextItemWidth(-1.f);
            changed |= ImGui::SliderFloat("##cameraFlightSpeed", &o.flightSpeed, .25f, 16.f, "%.2f ブロック/秒");
            ImGui::TextWrapped("ホイール移動距離");
            ImGui::SetNextItemWidth(-1.f);
            changed |= ImGui::SliderFloat("##cameraWheelDistance", &o.wheelDistance, .1f, 4.f, "%.2f ブロック");
            ImGui::TextWrapped("WASDで前後左右、Spaceで上、Shiftで下へ移動します。開始前に移動キーを離してください。移動距離に上限はありません。地形は読み込み済みの範囲だけ表示されます。");
        }
        ImGui::TextWrapped("起動キーと修飾キーは「キー割当」で設定します。移動キー・F5・F10・F11や他の操作と競合する起動キーは使用できません。二つのカメラモードは同時には動作しません。");
        ImGui::TextWrapped("チャット・GUI・フォーカス喪失・ワールド変更で終了します。保存されるのは設定で、カメラの稼働状態は保存されません。");
        break;
    }
    case Feature::InventoryTransfer: {
        auto& o=config.inventoryTransferOptions;
        disabledTextWrapped("レシピ本操作・まとめクラフト・まとめ交易・レシピと交易のお気に入りは未対応です。通常の収納移動は使用できます。");
        changed |= ImGui::Checkbox("ホイール: 1個",&o.wheelOne);
        changed |= ImGui::Checkbox("Shift+ホイール: スタック",&o.wheelStack);
        changed |= ImGui::Checkbox("Ctrl+左ドラッグ: 1個",&o.dragOne);
        changed |= ImGui::Checkbox("Shift+左ドラッグ: スタック",&o.dragStack);
        changed |= ImGui::Checkbox("ホットバーを保持",&o.preserveHotbar);
        changed |= ImGui::Checkbox("整理の除外スロットを保持",&o.respectSortExclusions);
        changed |= ImGui::Checkbox("ホイール方向を反転",&o.reverseWheel);
        changed |= ImGui::Checkbox("まとめ移動で1個残す",&o.leaveOne);
        ImGui::TextWrapped("1操作の移動上限");ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::SliderInt("##transferMaxSteps",&o.maxSteps,1,256);
        changed |= ImGui::Checkbox("所持品画面を除外",&o.excludeInventoryGui);
        changed |= ImGui::Checkbox("収納画面を除外",&o.excludeStorageGui);
        ImGui::TextWrapped("同種まとめ移動・全体移動・取消の実行キーは「キー割当」で設定します。カーソル側の領域から反対側へ移動します。同種は識別名が同じ物を対象にし、アイテムデータの異なるスタックは混ぜません。上限で終了し、残りを再開するにはもう一度キーを押します。送信済みの要求は取消後も応答を待ちます。");
        ImGui::TextWrapped("試験機能・初期は無効。通常の収納画面でアイテムにカーソルを合わせます。ホイール上で収納へ、下で手持ちへ同じアイテムを移動。Shiftでスタック単位。Ctrl+左ドラッグは各スロットから1個、Shift+左ドラッグはスタックを反対側へ移動します。Ctrlを優先し、同じスロットは1回だけ処理。左ボタンを離すとドラッグ終了。文字入力・手に持ったスタック・画面終了・移動失敗時は停止します。");
        break;
    }
    case Feature::InventorySort: {
        auto& o=config.inventorySortOptions;
        changed |= ImGui::Checkbox("ホットバーを保持", &o.preserveHotbar);
        changed |= ImGui::Checkbox("同じスタックをまとめる", &o.mergeStacks);
        constexpr char const* orders[]{"識別名","個数の多い順","SDKのアイテムID","SDKのカテゴリ","SDKのレア度の高い順"};
        ImGui::TextWrapped("並べる順番");ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::Combo("##sortMethod",&o.sortMethod,orders,5);
        changed |= ImGui::Checkbox("シュルカーを末尾に並べる",&o.shulkerLast);
        changed |= ImGui::Checkbox("バンドルを末尾に並べる",&o.bundleLast);
        ImGui::TextWrapped("1操作の整理上限");ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::SliderInt("##sortMaxSteps",&o.maxSteps,1,256);
        changed |= ImGui::Checkbox("所持品画面を除外",&o.excludeInventoryGui);
        changed |= ImGui::Checkbox("収納画面を除外",&o.excludeStorageGui);
        ImGui::Text("整理実行キー: %s (番号%d)",keyName(o.actionKey).c_str(),o.actionKey);
        auto exclusions=[&](char const* label,auto& slots) {
            if(!ImGui::CollapsingHeader(label))return;
            disabledTextWrapped("チェックしたスロットは動かしません。番号は1からで、手持ちの1〜9はホットバーです。");
            int const columns=std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x/56.f),1,9);
            for(int i=0;i<static_cast<int>(slots.size());++i){
                ImGui::PushID(label);ImGui::PushID(i);
                auto text=std::to_string(i+1);changed|=ImGui::Checkbox(text.c_str(),&slots[i]);
                ImGui::PopID();ImGui::PopID();if(i%columns!=columns-1&&i+1<static_cast<int>(slots.size()))ImGui::SameLine();
            }
        };
        exclusions("手持ちの除外",o.excludedInventory);
        exclusions("収納の除外",o.excludedStorage);
        ImGui::TextWrapped("試験機能・初期は無効。「キー割当」で整理の実行キーを設定します。ON/OFF用キーとは別です。所持品や収納の整理したい領域にカーソルを合わせて押します。SDKの識別名・個数・ID・カテゴリ・レア度順に並べます。固定・装備・作業・出力スロットは対象外。計画が上限を超える場合は実行しません。途中の内容変更・失敗・画面終了で停止します。");
        break;
    }
    case Feature::OffhandSwap: {
        ImGui::TextWrapped("直近の操作: %s",handRestockStatus());
        ImGui::TextWrapped("初期は無効。「キー割当」のオフハンド持ち替えキーはON/OFFではなく1回の実行です。ゲーム操作中に押すと、選択中の手持ちとオフハンドをnative controllerで交換します。空の片手も対象です。");
        ImGui::TextWrapped("WASD・Space・Shiftはカメラの移動キーのため持ち替えには使えません。F10・F11と他の操作キーとの重複も実行を止めます。");
        ImGui::TextWrapped("サーバーが許可するアイテムと操作だけが対象です。nativeのオフハンド用スロットを確認できない場合は未対応として停止します。応答前の再送はしません。GUI・カーソル保持・フォーカス喪失・内容変更・world変更で取消します。自動補充の除外設定は明示的な持ち替えには適用しません。");
        break;
    }
    case Feature::HandRestock: {
        ImGui::TextWrapped("直近の操作: %s",handRestockStatus());
        auto& o = config.handRestockOptions;
        changed |= ImGui::SliderInt("補充する残数", &o.threshold, 0, 16);
        changed |= ImGui::SliderInt("使用後の待ち時間", &o.quietMs, 150, 1000, "%d ms");
        changed |= ImGui::Checkbox("他のホットバーからも補充", &o.fromHotbar);
        changed |= ImGui::Checkbox("空のバケツ・瓶・ボウルを交換", &o.replaceRemainders);
        changed |= ImGui::Checkbox("オフハンドの使用後も補充",&o.offhand);
        if(ImGui::CollapsingHeader("補充の除外スロット")){
            disabledTextWrapped("番号1〜9はホットバー。補充先と予備の両方から除外します。");
            int const columns=std::clamp(int(ImGui::GetContentRegionAvail().x/56.f),1,9);
            for(int i=0;i<36;++i){ImGui::PushID(i);auto label=std::to_string(i+1);changed|=ImGui::Checkbox(label.c_str(),&o.excludedInventory[i]);ImGui::PopID();if(i%columns!=columns-1&&i<35)ImGui::SameLine();}
        }
        if(ImGui::CollapsingHeader("補充の除外アイテム")){
            disabledTextWrapped("SDKの識別名を指定します。例: minecraft:golden_carrot。空欄は未使用です。");
            for(int i=0;i<8;++i){char value[129]{};sprintf_s(value,"%s",o.excludedItems[i].c_str());ImGui::PushID(i);ImGui::SetNextItemWidth(-1.f);
                if(ImGui::InputText("##restockExcludedItem",value,sizeof(value))){o.excludedItems[i]=value;changed=true;}ImGui::PopID();}
            if(!validHandRestockOptions(o))disabledTextWrapped("識別名を確認してください。無効な設定は保存できません。");
        }
        ImGui::TextWrapped("試験機能・初期は無効。成功した使用と単一消費を確認した後に、メインハンドと有効にしたオフハンドを補充します。所持品の同じ物から個数が多い予備を優先。手動の移動・持ち替え・ドロップ・選択変更・GUIで観測を破棄します。原因不明の減少だけでは補充しません。トーテム発動など使用callbackのない消費は未対応です。");
        break;
    }
    case Feature::WeaponSwitch: {
        auto& o = config.weaponSwitchOptions;
        constexpr char const* priorities[]{"基礎攻撃力順", "剣を優先", "斧を優先", "メイスを優先"};
        changed |= ImGui::Combo("武器の優先順", &o.priority, priorities, 4);
        changed |= ImGui::Checkbox("攻撃後に戻す (処理順は未確認)", &o.switchBack);
        changed |= ImGui::Checkbox("斧も候補にする", &o.includeAxes);
        changed |= ImGui::Checkbox("トライデントも候補にする", &o.includeTridents);
        changed |= ImGui::Checkbox("耐久値の低い武器を除外", &o.skipLowDurability);
        ImGui::TextWrapped("初期は無効。ONにして攻撃すると、ホットバーの武器から選択します。同じ優先度なら現在の武器、耐久値、若いスロット番号の順。道具の破損防止が有効なら、選んだ武器も攻撃前に確認します。");
        break;
    }
    case Feature::ToolProtection: {
        auto& o = config.toolProtectionOptions;
        changed |= ImGui::SliderInt("耐久値の下限", &o.remaining, 1, 100);
        // An empty label hashes to its current seed: retain the exact previous
        // slider ID while drawing a shorter visible label at the same spacing.
        ImGui::PushOverrideID(ImGui::GetID("耐久率の下限 (%)"));
        changed |= ImGui::SliderInt("", &o.percent, 0, 25, "%d%%");
#ifdef PRAXIS_GUI_HEADLESS_TEST
        toolPercentLabelProbe.widgetId=ImGui::GetCurrentContext()->LastItemData.ID;
#endif
        ImGui::PopID();
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted("耐久率下限(%)");
#ifdef PRAXIS_GUI_HEADLESS_TEST
        toolPercentLabelProbe.window=ImGui::GetCurrentWindow();
        toolPercentLabelProbe.visibleWidth=ImGui::GetItemRectSize().x;
        toolPercentLabelProbe.legacyWidth=ImGui::CalcTextSize("耐久率の下限 (%)").x;
        toolPercentLabelProbe.labelRight=ImGui::GetItemRectMax().x;
        toolPercentLabelProbe.clipRight=ImGui::GetCurrentWindow()->InnerClipRect.Max.x;
        toolPercentLabelProbe.availableWidth=toolPercentLabelProbe.clipRight-ImGui::GetItemRectMin().x;
#endif
        ImGui::TextDisabled("下限で停止 (強制解除なし)");
        changed |= ImGui::Checkbox("攻撃も保護", &o.protectAttacks);
        ImGui::TextWrapped("初期は無効。ONにすると、設定した残り耐久値・耐久率の下限で採掘を止めます。攻撃の保護は上の設定で選択。押し直しても下限は解除されません。再開は道具を交換するか、この機能をOFFにします。道具の使用操作や一度に複数の耐久値を使う操作は対象外です。");
        break;
    }
    case Feature::FlexibleBlockPlacement: {
        auto& o = config.flexiblePlacementOptions;
        ImGui::SeparatorText("Flexible Block Placement - 3 modes");
        ImGui::BulletText("Rotation (hold key %c): choose block facing using the 5-way overlay",
            o.rotationKey >= 'A' && o.rotationKey <= 'Z' ? o.rotationKey : '?');
        ImGui::BulletText("Offset (hold key %c): place diagonally or with a 1-block gap",
            o.offsetKey >= 'A' && o.offsetKey <= 'Z' ? o.offsetKey : '?');
        ImGui::BulletText("Adjacent (hold key %c): place next to the clicked block",
            o.adjacentKey >= 'A' && o.adjacentKey <= 'Z' ? o.adjacentKey : '?');
        ImGui::TextWrapped("Hold two or three keys together to combine modes. "
            "Use the key-binding page to customize these keys. "
            "The green target outline shows the actual destination.");
        ImGui::TextWrapped("有効中に、指定した3つのキーを押しながらブロック面を狙います。中央と上下左右の5領域を選び、通常のサーバー権威配置を一度だけ呼び出します。");
        ImGui::TextWrapped("HOLD キーと修飾キーは上の「キー割当」画面で、割当ボタンから入力してください。ここでは VK 数値を直接編集しません。");
        ImGui::TextDisabled("既定: P=機能ON/OFF、R=回転、G=オフセット、H=隣接。UI・空手・食べ物・破壊中は介入しません。");
        ImGui::TextDisabled("ログ/ホッパー/ピストン等の状態はバニラの配置判定に委ね、支持面・到達距離・空きセルが確認できない場合は変換しません。");
        break;
    }
    default:
        ImGui::TextDisabled("26.51.5ではこの機能を準備できません");
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

void drawProfiles(RuntimeConfig& config, bool& changed,std::uint64_t& revision,std::uint32_t& bindingEdits) {
    static char profileName[49]{};
    static std::string selected;
    static std::string status;
    ImGui::TextUnformatted("保存設定");
    ImGui::Separator();
    ImGui::TextUnformatted("現在の設定: config.json");
    if (ImGui::Button("現在の設定を保存", {180.0f, 32.0f}))
        status = flushRuntimeConfig() ? "保存しました" : "保存できませんでした";
    ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputText("設定名", profileName, sizeof(profileName));
    if (ImGui::Button("名前を付けて保存", {180.0f, 32.0f})) {
        if (changed) {
            updateRuntimeConfigIfRevision(config,revision,bindingEdits);
            auto view=runtimeConfigView();config=std::move(view.config);revision=view.revision;
            changed=false;bindingEdits=0;
        }
        status = saveRuntimeProfile(profileName) ? "保存を受け付けました" : "設定名または保存先を確認してください";
    }
    ImGui::TextWrapped("%s", storageStatusJa(configStorageStatus()).c_str());
    ImGui::SeparatorText("保存済み設定");
    ImGui::BeginChild("##saved_configs", {0.0f, 180.0f}, ImGuiChildFlags_Borders);
    for (auto const& name : listRuntimeProfiles())
        if (ImGui::Selectable(name.c_str(), selected == name)) selected = name;
    ImGui::EndChild();
    bool const hasSelection = !selected.empty();
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("読み込む", {110.0f, 31.0f})) {
        bool const loaded = loadRuntimeProfile(selected);
        status = loaded ? "読込を受け付けました" : "設定を読み込めませんでした";
        if (loaded) {
            auto view=runtimeConfigView();config=std::move(view.config);revision=view.revision;
            changed=false;bindingEdits=0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("削除", {110.0f, 31.0f})) {
        status = deleteRuntimeProfile(selected) ? "削除を受け付けました" : "設定を削除できませんでした";
        selected.clear();
    }
    if (!hasSelection) ImGui::EndDisabled();
    if (!status.empty()) disabledTextWrapped(status.c_str());
}

void drawPreviewSettings(RuntimeConfig& config, bool& changed) {
    auto& o=config.informationOptions;
    changed |= ImGui::Checkbox("バンドルの中身", &o.bundlePreview);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("初期は無効。インベントリのバンドルにカーソルを合わせると中身を表示します。Shift限定やホットバー表示は下の設定で選べます。");
    changed |= ImGui::Checkbox("耐久値の説明を追加", &o.durabilityInformation);
    changed |= ImGui::Checkbox("詳細ツールチップ", &o.advancedTooltips);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("通常の所持品ツールチップの下にID・補助値・個数・耐久値を追加します。中身プレビューが有効な箱では既存プレビューを優先します。");
    if (ImGui::CollapsingHeader("バンドルの表示")) {
        changed |= ImGui::Checkbox("ホットバーでも表示", &o.bundleShowInHotbar);
        changed |= ImGui::Checkbox("空きスロットも表示", &o.bundleShowEmpty);
        changed |= ImGui::Checkbox("Shiftを押した時だけ表示", &o.bundleRequireShift);
        changed |= ImGui::Checkbox("個数を表示", &o.bundleShowItemCount);
        changed |= ImGui::Checkbox("スロット番号を表示", &o.bundleShowSlotNumbers);
        changed |= ImGui::SliderFloat("バンドル倍率", &o.bundleScale, .75f, 2);
    }
}
#ifdef PRAXIS_GUI_HEADLESS_TEST
std::vector<std::string> hudSettingsTestSections;
#endif
void drawHudSettings(RuntimeConfig& config, bool& changed, int& selected) {
#ifdef PRAXIS_GUI_HEADLESS_TEST
    hudSettingsTestSections.clear();
#endif
    static char hudSearch[128]{};
    auto& o=config.informationOptions;
    auto& player=o.playerHud;
    ImGui::TextUnformatted("HUD");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##hud_search", "HUD名・説明を検索", hudSearch, sizeof(hudSearch));
    if(ImGui::Button("HUD配置を開く")) selected=-9;
    if(ImGui::IsItemHovered())ImGui::SetTooltip("HUDの位置と倍率をまとめて調整します");
    ImGui::SeparatorText("クイック切替");
    changed |= ImGui::Checkbox("情報HUDを表示", &o.infoHud);
    changed |= ImGui::Checkbox("装備の耐久値HUDを表示", &o.durabilityHud);
    changed |= ImGui::Checkbox("オフハンドHUDを表示", &player.offhandHud);
    changed |= ImGui::Checkbox("満腹度HUDを表示", &player.foodHud);
    changed |= ImGui::Checkbox("チャンク情報HUDを表示", &config.chunkBorder.showHud);
    ImGui::Spacing();
    ImGui::SeparatorText("詳細設定");
    bool found=false;
    auto section=[&](char const* title, char const* description) {
        if(!matches(title,description,hudSearch)) return false;
        found=true;
#ifdef PRAXIS_GUI_HEADLESS_TEST
        hudSettingsTestSections.emplace_back(title);
#endif
        if(*hudSearch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
        return ImGui::CollapsingHeader(title);
    };
    if(section("情報HUD", "Information 座標 方角 FPS 見ている対象 ブロック メモリ 文字色")) {
        if (ImGui::CollapsingHeader("表示項目")) {
            changed |= ImGui::Checkbox("取得不可も表示", &o.showUnavailable);
            for (unsigned i=0;i<o.lines.size();++i)
                changed |= ImGui::Checkbox(informationDisplayNames[i], &o.lines[i]);
        }
        if (ImGui::CollapsingHeader("位置・外観##information")) {
            changed |= ImGui::SliderFloat("HUD X", &o.x, 0, 4096);
            changed |= ImGui::SliderFloat("HUD Y", &o.y, 0, 2160);
            changed |= ImGui::SliderFloat("HUD倍率", &o.scale, .5f, 3);
            auto color = colorFloats(o.textColor);
            if (ImGui::ColorEdit4("HUD文字色", color.data())) {
                o.textColor=packedAbgr(color); changed=true;
            }
        }
        if (ImGui::CollapsingHeader("詳細##information")) {
            // The sampler is separate from the always-visible HUD.
            changed |= ImGui::Checkbox("ブロック比較用の状態取得",&o.inspector.blockState);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("比較データはF10のブロック比較ページだけに表示します");
        }
    }
    if(section("装備・耐久HUD", "Equipment Armor Durability 装備の耐久値")) {
        if (ImGui::CollapsingHeader("装備の表示")) {
            changed |= ImGui::SliderFloat("装備X", &o.durabilityX, 0, 4096);
            changed |= ImGui::SliderFloat("装備Y", &o.durabilityY, 0, 2160);
            changed |= ImGui::Combo("表示形式", &o.durabilityMode, "バー+数値\0数値\0バー\0");
            changed |= ImGui::Checkbox("無傷の装備も表示", &o.durabilityShowFull);
        }
    }
    if(section("オフハンドHUD", "Offhand 左手 個数 耐久値 空スロット")) {
        if(ImGui::CollapsingHeader("オフハンドの表示")) {
            changed |= ImGui::Checkbox("空でも表示", &player.offhandShowEmpty);
            changed |= ImGui::Checkbox("個数", &player.offhandShowCount);
            changed |= ImGui::Checkbox("耐久値", &player.offhandShowDurability);
            changed |= ImGui::SliderFloat("オフハンドX", &player.offhandX,0,4096);
            changed |= ImGui::SliderFloat("オフハンドY", &player.offhandY,0,2160);
            changed |= ImGui::SliderFloat("オフハンド倍率", &player.offhandScale,.5f,3);
        }
    }
    if(section("満腹度HUD", "Food Hunger Saturation 隠し満腹度 消耗度")) {
        ImGui::TextDisabled("未確認の値は「不明」と表示");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("隠し満腹度は値の変化を確認するまで不明です。消耗度は未対応です");
        if(ImGui::CollapsingHeader("満腹度HUDの位置")) {
            changed |= ImGui::SliderFloat("満腹度X", &player.foodX,0,4096);
            changed |= ImGui::SliderFloat("満腹度Y", &player.foodY,0,2160);
            changed |= ImGui::SliderFloat("満腹度倍率", &player.foodScale,.5f,3);
        }
    }
    if(section("チャンク情報HUD", "Chunks ChunkBorders チャンク座標 高さ")) {
        ImGui::TextDisabled("ChunkBordersを有効にしたとき表示");
    }
    if(section("設計図・材料HUD / HUD配置", "Schematic Materials LHolo HUD Layout Editor 位置 倍率")) {
        ImGui::TextDisabled("設計図と材料の表示切替はLHoloで設定");
        if(ImGui::Button("設計図・材料のHUD配置を開く")) selected=-9;
    }
    if(!found) ImGui::TextDisabled("条件に一致するHUDはありません。");
}
void drawLegacyPraxis(void* context) noexcept {
    try {
        GuiContextScope contextScope(context);
        if (sharedFontRequired.load(std::memory_order_acquire) && !sharedLegacyUiFontReady(context)) return;
        LegacyUiScope legacyScope;

        // Host callbacks can run on the input thread; consume their intent only
        // here on the drawing thread. Internal pages keep their current route.
        if (openModuleListPending.exchange(false, std::memory_order_acq_rel)) {
            legacyGuiState.selected = -1;
            legacyGuiState.settingKeybind = -1;
        }

        auto& selected = legacyGuiState.selected; // negative values select pages; nonnegative values select utility details
        auto& categoryFilter = legacyGuiState.categoryFilter;
        auto& favoritesOnly = legacyGuiState.favoritesOnly;
        auto& settingKeybind = legacyGuiState.settingKeybind;
        auto& search = legacyGuiState.search;

        auto view=runtimeConfigView();
        auto config=std::move(view.config);auto revision=view.revision;
        std::uint32_t bindingEdits{};
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
            return;
        }

        auto pageButton = [&](int page, char const* label) {
            bool const active = selected == page || (page == -1 && selected >= 0);
            ImGui::PushStyleColor(ImGuiCol_Button, active
                ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)
                : ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
            if (ImGui::Button(label, {-1.0f, 31.0f})) selected = page;
            ImGui::PopStyleColor();
            if (active) {
                auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                // A three-pixel accent rail provides an unobtrusive selection cue.
                ImGui::GetWindowDrawList()->AddRectFilled(
                    {a.x+2,a.y+5},{a.x+5,b.y-5},
                    ImGui::GetColorU32(ImGuiCol_CheckMark),2.0f);
            }
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
        float longestPageLabel{};
        for (char const* label : {"機能", "設計図", "地図", "HUD", "HUD配置",
                                 "表示設定", "保存", "キー割当", "カメラ", "リソパ", "ブロック比較", "互換性", "情報"})
            longestPageLabel = std::max(longestPageLabel, ImGui::CalcTextSize(label).x);
        auto const& style = ImGui::GetStyle();
        float const labelWidth = longestPageLabel + 2 * style.FramePadding.x
            + 2 * style.WindowPadding.x + style.ScrollbarSize;
        float const sidebarWidth = std::max(labelWidth,
            std::clamp(ImGui::GetContentRegionAvail().x * 0.20f, 136.0f, 180.0f));
        ImGui::BeginChild("##praxis_sidebar", {sidebarWidth, 0.0f}, ImGuiChildFlags_Borders);
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),"PRAXIS");
        ImGui::SameLine();ImGui::TextDisabled("CLIENT");
        ImGui::Separator();
        ImGui::TextDisabled("メイン");
        pageButton(-1, "機能");
        pageButton(-8, "HUD");
        pageButton(-12, "リソパ");
        pageButton(-13, "ブロック比較");
        pageButton(-7, "地図");
        pageButton(-6, "設計図");
        ImGui::Spacing();
        ImGui::TextDisabled("ツール");
        pageButton(-11, "カメラ");
        pageButton(-14, "互換性");
        ImGui::Spacing();
        ImGui::TextDisabled("設定");
        pageButton(-9, "HUD配置");
        pageButton(-4, "キー割当");
        pageButton(-3, "保存");
        pageButton(-2, "表示設定");
        pageButton(-5, "情報");

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
            categoryButton(2, "プレイヤー");
            categoryButton(3, "ワールド");
            categoryButton(4, "その他");
            ImGui::Spacing();
            ImGui::Checkbox("お気に入りのみ", &favoritesOnly);
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##praxis_content", {0.0f, 0.0f});
        if (selected == -12) {
            drawResourcePackPage();
        } else if (selected == -14) {
            ImGui::TextUnformatted("互換性");
            ImGui::SeparatorText("検出状況");
            bool const lamiumLoaded=GetModuleHandleW(L"Lamium.dll")!=nullptr;
            ImGui::TextUnformatted("Lamium");
            ImGui::SameLine();
            ImGui::TextColored(lamiumLoaded?ImVec4{.34f,.84f,.55f,1.f}:ImVec4{.68f,.70f,.74f,1.f},
                "%s",lamiumLoaded?"検出":"未検出");
            unsigned activeOverlaps=0;
            for(auto feature:lamiumOverlapCandidates) {
                auto* enabled=enabledField(config,feature);
                if(enabled&&*enabled)++activeOverlaps;
            }
            ImGui::TextDisabled("Praxisの重複候補: %u 件",activeOverlaps);
            if(lamiumLoaded&&activeOverlaps&&ImGui::Button("Praxisの重複機能をOFF")) {
                for(auto feature:lamiumOverlapCandidates) {
                    auto* enabled=enabledField(config,feature);
                    if(enabled&&mayDisablePraxisOverlap(lamiumLoaded,feature,*enabled)) {
                        *enabled=false;changed=true;
                    }
                }
            }
        } else if (selected == -13) {
            ImGui::TextUnformatted("ブロック比較");
            ImGui::SeparatorText("状態");
            auto& inspector=config.informationOptions.inspector;
            if(!inspector.blockState) {
                if(ImGui::Button("比較機能をON")) {
                    inspector.blockState=true;changed=true;
                }
            }
            auto const generation=worldGeneration.load();
            auto& reference=legacyGuiState.pinnedBlock;
            if(reference&&reference->generation!=generation)reference.reset();
            auto current=blockOnMenuOpen.load();
            if(current&&current->generation!=generation)current.reset();
            auto shortLabel=[](BlockStateSample const* sample) {
                if(!sample)return std::string("未取得");
                std::string name=sample->label.substr(0,sample->label.find(" @ "));
                if(name.starts_with("minecraft:"))name.erase(0,10);
                return name;
            };
            auto const currentName=shortLabel(current.get());
            auto const referenceName=shortLabel(reference?&*reference:nullptr);
            ImGui::TextDisabled("対象");
            ImGui::SameLine(88.f);
            ImGui::TextWrapped("%s",currentName.c_str());
            if(ImGui::IsItemHovered()&&current)ImGui::SetTooltip("%s",current->label.c_str());
            ImGui::TextDisabled("基準");
            ImGui::SameLine(88.f);
            ImGui::TextWrapped("%s",referenceName.c_str());
            if(ImGui::IsItemHovered()&&reference)ImGui::SetTooltip("%s",reference->label.c_str());
            ImGui::Separator();
            if(reference) {
                auto const result=compareBlockStates(&*reference,current.get());
                if(result==BlockStateComparison::Same)
                    ImGui::TextColored({0.3f,0.9f,0.5f,1.0f},"比較結果: 一致");
                else if(result==BlockStateComparison::Different)
                    ImGui::TextColored({1.0f,0.65f,0.25f,1.0f},"比較結果: 異なる");
                else ImGui::TextDisabled("比較結果: 対象なし");
            } else ImGui::TextDisabled("まだ基準を保存していません。");
            ImGui::Spacing();
            if(current&&ImGui::Button("このブロックを基準にする"))reference=*current;
            if(reference) {
                if(current&&ImGui::GetContentRegionAvail().x>130.f)ImGui::SameLine();
                if(ImGui::Button("基準を消去"))reference.reset();
            }
            ImGui::TextDisabled("対象の更新: F10を閉じて開き直す");
        } else if (selected == -2) {
            ImGui::TextUnformatted("表示設定");
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
            disabledTextWrapped("画面の外観は従来のPraxis設定を使用します。");
        } else if (selected == -3) {
            drawProfiles(config,changed,revision,bindingEdits);
        } else if (selected == -8) {
            drawHudSettings(config, changed, selected);
        } else if(selected==-9) {
            drawHudLayoutEditor(config,changed);
        } else if (selected == -11) {
            ImGui::TextUnformatted("カメラ");
            auto status=cameraStatusJa(cameraCandidateStatus());
            ImGui::TextWrapped("状態: %s", status.c_str());
            for(auto feature : {Feature::Freelook, Feature::FreeCamera}) {
                ImGui::PushID(static_cast<int>(feature));
                ImGui::SeparatorText(featureDisplayName(featureNames[static_cast<unsigned>(feature)]));
                auto* enabled=enabledField(config,feature);
                changed |= ImGui::Checkbox("有効", enabled);
                auto id=static_cast<unsigned>(feature);
                auto const modifiers=config.keybindModifiers[id];
                auto const prefix=modifiers==anyModifiers?std::string_view{"任意キー不要 + "}:hotkeyModifierPrefix(modifiers);
                ImGui::TextWrapped("起動キー: %s%s", prefix.data(), keyName(config.keybinds[id]).c_str());
                drawDetails(config,feature,changed);
                ImGui::PopID();
            }
        } else if (selected == -4) {
            drawUnifiedHotkeys(config,changed,bindingEdits);
        } else if (selected == -5) {
            ImGui::TextUnformatted("Praxis Client");
            disabledTextWrapped("Minecraft Bedrock 26.51.5 / LeviLamina 26.51.5");
            ImGui::Separator();
            ImGui::TextWrapped("Praxisの便利機能を設定する画面です。F10で開閉します。");
        } else if (selected == -6) {
            ImGui::TextUnformatted("設計図");
            ImGui::Separator();
            disabledTextWrapped("設計図機能はPraxisCompanionには含まれていません。");
            ImGui::TextWrapped("LHoloを導入している場合はLHolo側のメニューから設計図を操作してください。");
        } else if (selected == -7) {
            ImGui::TextUnformatted("地図");
            ImGui::Separator();
            drawMapSettings();
        } else if (selected == -1) {
            ImGui::TextUnformatted("機能");
            unsigned onCount=0;
            for(auto const& card:cards) {
                auto* enabled=enabledField(config,card.feature);
                if(enabled&&*enabled)++onCount;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%u / %zu ON",onCount,cards.size());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##module_search", "機能名・説明を検索", search, sizeof(search));
            ImGui::Spacing();

            std::vector<std::size_t> filtered;
            for (std::size_t i = 0; i < cards.size(); ++i) {
                auto const& card = cards[i];
                int const category = moduleCategory(card);
                bool const favorite = (config.favoriteModules & (1u << favoriteSlot(i))) != 0;
                if (categoryFilter >= 0 && category != categoryFilter) continue;
                if (favoritesOnly && !favorite) continue;
                if (!matches(card.name, card.description, search)
                    && !matches(featureDisplayName(card.name), card.description, search)) continue;
                filtered.push_back(i);
            }
            std::stable_sort(filtered.begin(), filtered.end(), [&](std::size_t a, std::size_t b) {
                bool const af = (config.favoriteModules & (1u << favoriteSlot(a))) != 0;
                bool const bf = (config.favoriteModules & (1u << favoriteSlot(b))) != 0;
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

                    ImVec2 const size{cardWidth, 176.0f};
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
                    ImGui::TextWrapped("%s", featureDisplayName(card.name));
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImVec2 const titleBottomRight{
                        titleTopLeft.x + ImGui::GetContentRegionAvail().x,
                        titleTopLeft.y + titleHeight};
                    if (ImGui::IsMouseHoveringRect(titleTopLeft, titleBottomRight)
                        && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", featureDisplayName(card.name));

                    ImGui::TextDisabled("%s", categoryLabel(moduleCategory(card)));
                    ImGui::BeginChild("##description", {0.0f, 41.0f});
                    ImGui::TextWrapped("%s", card.description);
                    ImGui::EndChild();
                    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s",card.description);
                    ImGui::Separator();

                    bool cardControlActivated = false;
                    if (drawCardIcon("##settings", false)) {
                        cardControlActivated = true;
                        selected = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("設定を開く");
                    ImGui::SameLine();

                    bool const favorite = (config.favoriteModules & (1u << favoriteSlot(i))) != 0;
                    if (drawCardIcon("##favorite", true, favorite)) {
                        cardControlActivated = true;
                        config.favoriteModules ^= (1u << favoriteSlot(i));
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
                ImGui::TextDisabled("%s", categoryLabel(moduleCategory(card)));
                ImGui::Separator();
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
                    "%s",featureDisplayName(card.name));
                ImGui::TextDisabled("%s", card.description);
                ImGui::Spacing();

                bool* enabled = enabledField(config, card.feature);
                bool const isEnabled = enabled && *enabled;
                bool const sharedControlRow = ImGui::GetContentRegionAvail().x
                    >= 190.0f + ImGui::GetStyle().ItemSpacing.x + 150.0f;
                if (isEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(
                        isEnabled ? "ON (クリックでOFF)" : "OFF (クリックでON)",
                        {190.0f, 34.0f})) {
                    if (enabled) {
                        *enabled = !*enabled;
                        changed = true;
                    }
                }
                if (isEnabled) ImGui::PopStyleColor();

                if (sharedControlRow) ImGui::SameLine();
                bool const favorite = (config.favoriteModules & (1u << favoriteSlot(selected))) != 0;
                if (ImGui::Button(
                        favorite ? "お気に入り済み" : "お気に入りに追加",
                        {150.0f, 34.0f})) {
                    config.favoriteModules ^= (1u << favoriteSlot(selected));
                    changed = true;
                }

                auto const moduleBind = keyName(config.keybinds[static_cast<unsigned>(card.feature)]);
                ImGui::Text("キー割り当て: %s", moduleBind.c_str());
                ImGui::SeparatorText("機能設定");
                drawDetails(config, card.feature, changed);
                if(ImGui::CollapsingHeader("使い方")) {
                    if(card.feature==Feature::Freelook||card.feature==Feature::FreeCamera)
                        ImGui::TextWrapped("有効化してから「キー割当」でカメラ操作キーを設定します。");
                    else if(card.feature==Feature::OffhandSwap)
                        ImGui::TextWrapped("有効化してから「キー割当」で実行キーを設定します。");
                    else
                        ImGui::TextWrapped("キー操作で切り替える場合は「キー割当」で設定できます。");
                }
            }
        }

        if (settingKeybind >= 0) {
            ImGui::OpenPopup("キー割り当て");
            if (ImGui::BeginPopupModal("キー割り当て", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                char const* targetName = settingKeybind < static_cast<int>(featureCount)
                    ? featureDisplayName(featureNames[static_cast<unsigned>(settingKeybind)])
                    : "ズーム操作";
                ImGui::Text("%s に割り当てるキーを押してください。", targetName);
                ImGui::TextDisabled("ESCでキャンセルします。");
                for (int key = 0x08; key <= 0xFE; ++key) {
                    if ((GetAsyncKeyState(key) & 0x8000) == 0) continue;
                    if (key == VK_ESCAPE) {
                        settingKeybind = -1;
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if(key==VK_F10||key==VK_F11)continue;
                    // The old per-module editor preserves its original Any modifiers contract.
                    if (settingKeybind < static_cast<int>(featureCount)) {
                        config.keybinds[static_cast<unsigned>(settingKeybind)] = key;
                        config.keybindModifiers[static_cast<unsigned>(settingKeybind)] = anyModifiers;
                    } else {config.zoomKey = key;config.zoomModifiers = anyModifiers;}
                    changed = true;
                    settingKeybind = -1;
                    ImGui::CloseCurrentPopup();
                    break;
                }
                ImGui::EndPopup();
            }
        }

        ImGui::EndChild();
        ImGui::End();

        if (changed) updateRuntimeConfigIfRevision(config,revision,bindingEdits);
    } catch (...) { logGuiError("Praxis GUI drawing failed"); }
}


void drawPraxis(void* context) noexcept { drawLegacyPraxis(context); }

void onVisibility(bool visible) noexcept {
    notifyPraxisGuiVisibility(visible);
    Renderer::get().setGuiOpen(visible);
    if (!visible) flushRuntimeConfig();
    logGuiInfo(visible ? "LHolo bridge: F10 OPEN" : "LHolo bridge: F10 CLOSE");
}
bool hudNeeded() noexcept {
    return frameHudNeeded.load(std::memory_order_acquire)
        || shulkerPreviewNeeded()
        || informationHudNeeded()
        || foodHudNeeded()
        || mapHudNeeded();
}
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
        auto display=ImGui::GetIO().DisplaySize;
        auto sample=informationSnapshot();
        float lx=sample->logicalWidth/display.x,ly=sample->logicalHeight/display.y;
        std::array obstacles{paperDollBounds(lx,ly),durabilityHudBounds(),informationHudBounds()};
        auto const padding=ImGui::GetStyle().WindowPadding;
        std::array<std::string,5> chunkLines;
        char text[128];
        chunkLines[0]="チャンク境界";
        std::snprintf(text,sizeof(text),"表示距離: %d",current->chunkBorder.renderDistance);chunkLines[1]=text;
        std::snprintf(text,sizeof(text),"現在のチャンク: %d / %d",chunkHudX.load(),chunkHudZ.load());chunkLines[2]=text;
        std::snprintf(text,sizeof(text),"チャンク内: %d / %d",chunkHudInX.load(),chunkHudInZ.load());chunkLines[3]=text;
        std::snprintf(text,sizeof(text),"高さ: %d - %d",chunkHudY.load()-current->chunkBorder.heightBelow,chunkHudY.load()+current->chunkBorder.heightAbove);chunkLines[4]=text;
        auto const& transform=informationOptions()->hudLayouts.panels[static_cast<unsigned>(HudId::Chunks)];
        float const chunkScale=transform.custom?transform.scale:1.f;
        unsigned count=current->chunkBorder.showYCoordinates?5:4;
        float width=0;for(unsigned i=0;i<count;++i)width=std::max(width,ImGui::CalcTextSize(chunkLines[i].c_str()).x*chunkScale);
        width+=2*padding.x;
        float height=count*ImGui::GetTextLineHeightWithSpacing()*chunkScale+2*padding.y;
        auto placement=placeHud(transformHud({10,300,width,height},transform,display.x,display.y),display.x,display.y,obstacles);
        ImGui::SetNextWindowPos({placement.x,placement.y},ImGuiCond_Always);
        ImGui::SetNextWindowSize({placement.width,placement.height},ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("Chunks##praxis_overlay", nullptr,
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::SetWindowFontScale(chunkScale);
        ImGui::TextColored({0.3f, 0.9f, 0.6f, 1.0f}, "%s",chunkLines[0].c_str());
        for(unsigned i=1;i<count;++i)ImGui::TextUnformatted(chunkLines[i].c_str());
        ImGui::End();
        ImGui::SetCurrentContext(previousContext);
    } catch (...) { ImGui::SetCurrentContext(previousContext); }
}

} // namespace

void drawPraxisContent(void* context) noexcept { drawPraxis(context); }

void notifyPraxisGuiVisibility(bool visible) noexcept {
    bool const previous = praxisMenuVisible.exchange(visible, std::memory_order_acq_rel);
    if (visible && !previous) {
        openModuleListPending.store(true, std::memory_order_release);
        auto recent=lastInspectedBlockSample();
        if(recent&&recentBlockStateSample(*recent,worldGeneration.load(),GetTickCount64()))
            blockOnMenuOpen.store(std::move(recent));
        else blockOnMenuOpen.store(nullptr);
    }
    if (!visible) {
        blockOnMenuOpen.store(nullptr);
        cancelUnifiedHotkeyCapture();
    }
}
void drawChunkHudContent(void* context) noexcept {
    try {
        GuiContextScope contextScope(context);
        if (sharedFontRequired.load(std::memory_order_acquire) && !sharedLegacyUiFontReady(context)) return;
        LegacyUiScope legacyScope;
        if (informationHudNeeded()) drawInformationHud(context);
        if (foodHudNeeded()) drawFoodHud(context);
        drawChunkHud(context);
        if (shulkerPreviewNeeded()) drawShulkerPreviewContent(context);
        if (mapHudNeeded()) drawMapHud(context);
    } catch (...) { logGuiError("Praxis HUD drawing failed"); }
}
bool chunkHudNeeded() noexcept { return hudNeeded(); }

#ifndef PRAXIS_GUI_HEADLESS_TEST
bool lholoBridgeCompatible() noexcept {
    try {
        auto mod = ll::mod::ModManagerRegistry::getInstance().getMod("LHolo");
        if (!mod || mod->getType() != ll::mod::NativeModManagerName || !mod->isEnabled()) return false;
        auto provider = std::static_pointer_cast<ll::mod::NativeMod>(mod);
        if (!provider->getHandle()) return false;
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(provider->getHandle()), &module)) return false;
        bool const v2 = GetProcAddress(module, "lholo_register_companion_gui_v2")
            && GetProcAddress(module, "lholo_unregister_companion_gui_v2")
            && GetProcAddress(module, "lholo_companion_gui_registered_v2")
            && GetProcAddress(module, "lholo_close_companion_gui_v2")
            && GetProcAddress(module, "lholo_menu_input_captured_v2")
            && GetProcAddress(module, "lholo_set_companion_gui_font_initializer_v2")
            && GetProcAddress(module, "lholo_set_companion_gui_graphics_callbacks_v2");
        FreeLibrary(module);
        return v2;
    } catch (...) { return false; }
}
#endif

void quiesceBridgeReaders() noexcept {
    registeredFn.store(nullptr, std::memory_order_seq_cst);
    inputCapturedFn.store(nullptr, std::memory_order_seq_cst);
    closeFn.store(nullptr, std::memory_order_seq_cst);
    while (bridgeReaders.load(std::memory_order_seq_cst) != 0) Sleep(0);
}

void releaseLocalBridge() noexcept {
    cancelUnifiedHotkeyCapture();
    registered = false;
    bridgeOwner.store(nullptr, std::memory_order_release);
    closeFn.store(nullptr, std::memory_order_release);
    unregisterFn = nullptr;
    sharedFontRequired.store(false, std::memory_order_release);
    sharedInitializerAccepted.store(false, std::memory_order_release);
    Renderer::get().setGuiOpen(false);
    if (auto module = bridgeModule.exchange(nullptr, std::memory_order_acq_rel)) FreeLibrary(module);
}

#ifndef PRAXIS_GUI_HEADLESS_TEST
bool startLholoBridge() noexcept {
    if (!runtimeReady.load(std::memory_order_acquire)) return false;
    bridgeReaders.fetch_add(1, std::memory_order_seq_cst);
    auto checkRegistered = registeredFn.load(std::memory_order_seq_cst);
    auto owner = bridgeOwner.load(std::memory_order_acquire);
    bool const active = checkRegistered && owner && checkRegistered(owner);
    bridgeReaders.fetch_sub(1, std::memory_order_seq_cst);
    if (active && sharedInitializerAccepted.load(std::memory_order_acquire)) return true;
    if (GetTickCount64() < nextBridgeAttempt.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(bridgeMutex);
    if (!runtimeReady.load(std::memory_order_acquire)) return false;
    auto const currentRegistration = registeredFn.load(std::memory_order_acquire);
    if (registered && currentRegistration && currentRegistration(bridgeOwner.load(std::memory_order_acquire))
        && sharedInitializerAccepted.load(std::memory_order_acquire))
        return true;
    if (registered) {
        auto const previousInputCaptured = inputCapturedFn.load(std::memory_order_acquire);
        auto const previousClose = closeFn.load(std::memory_order_seq_cst);
        quiesceBridgeReaders();
        if (!unregisterFn || !unregisterFn(bridgeOwner.load(std::memory_order_acquire))) {
            registeredFn.store(currentRegistration, std::memory_order_seq_cst);
            inputCapturedFn.store(previousInputCaptured, std::memory_order_seq_cst);
            closeFn.store(previousClose, std::memory_order_seq_cst);
            return false;
        }
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

    auto registerV2 = reinterpret_cast<RegisterV2Fn>(
        GetProcAddress(module, "lholo_register_companion_gui_v2"));
    auto unregisterV2 = reinterpret_cast<UnregisterFn>(
        GetProcAddress(module, "lholo_unregister_companion_gui_v2"));
    auto isRegisteredV2 = reinterpret_cast<RegisteredFn>(
        GetProcAddress(module, "lholo_companion_gui_registered_v2"));
    auto closeV2 = reinterpret_cast<CloseFn>(
        GetProcAddress(module, "lholo_close_companion_gui_v2"));
    auto inputCapturedV2 = reinterpret_cast<InputCapturedFn>(
        GetProcAddress(module, "lholo_menu_input_captured_v2"));
    auto setFontInitializer = reinterpret_cast<SetFontInitializerFn>(
        GetProcAddress(module, "lholo_set_companion_gui_font_initializer_v2"));
    auto setGraphicsCallbacks = reinterpret_cast<SetGraphicsCallbacksFn>(
        GetProcAddress(module, "lholo_set_companion_gui_graphics_callbacks_v2"));
    if (!registerV2 || !unregisterV2 || !isRegisteredV2 || !closeV2 || !inputCapturedV2
        || !setFontInitializer || !setGraphicsCallbacks) {
        logGuiError("LHolo bridge: shared Praxis font/graphics provider is unavailable");
        FreeLibrary(module);
        return false;
    }
    sharedFontRequired.store(true, std::memory_order_release);
    if (!registerV2(owner, VK_F10, drawPraxis, drawChunkHudContent, hudNeeded, onVisibility,
                    IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImDrawVert))) {
        sharedFontRequired.store(false, std::memory_order_release);
        FreeLibrary(module);
        return false;
    }
    if (!setFontInitializer(owner, initializeSharedLegacyUiFont)
        || !setGraphicsCallbacks(owner, prepareSharedMapGraphics, resetSharedMapGraphics)) {
        // Keep the module alive if the provider cannot drain the registration.
        if (!unregisterV2(owner)) {
            bridgeModule.store(module, std::memory_order_release);
            bridgeOwner.store(owner, std::memory_order_release);
            unregisterFn = unregisterV2;
            registeredFn.store(isRegisteredV2, std::memory_order_release);
            registered = true;
        } else {
            sharedFontRequired.store(false, std::memory_order_release);
            FreeLibrary(module);
        }
        logGuiError("LHolo bridge: Praxis font/graphics callback registration rejected");
        return false;
    }
    sharedInitializerAccepted.store(true, std::memory_order_release);
    bridgeModule.store(module, std::memory_order_release);
    bridgeOwner.store(owner, std::memory_order_release);
    unregisterFn = unregisterV2;
    closeFn.store(closeV2, std::memory_order_release);
    inputCapturedFn.store(inputCapturedV2, std::memory_order_release);
    registeredFn.store(isRegisteredV2, std::memory_order_release);
    registered = true;
    nextBridgeAttempt.store(0, std::memory_order_release);
    logGuiInfo("LHolo shared-context bridge v2 registered");
    return true;
}
#endif

bool stopLholoBridge() noexcept {
    std::lock_guard lock(bridgeMutex);
    if (!registered) return true;
    auto checkRegistered = registeredFn.load(std::memory_order_acquire);
    auto inputCaptured = inputCapturedFn.load(std::memory_order_acquire);
    auto close = closeFn.load(std::memory_order_seq_cst);
    quiesceBridgeReaders();
    bool const removed = unregisterFn && unregisterFn(bridgeOwner.load(std::memory_order_acquire));
    if (!removed) {
        registeredFn.store(checkRegistered, std::memory_order_seq_cst);
        inputCapturedFn.store(inputCaptured, std::memory_order_seq_cst);
        closeFn.store(close, std::memory_order_seq_cst);
    }
    if (!removed) return false;
    releaseLocalBridge();
    return true;
}

bool lholoBridgeInputCaptured() noexcept {
    if (Renderer::get().isGuiOpen()) return true;
    bridgeReaders.fetch_add(1, std::memory_order_seq_cst);
    auto fn = inputCapturedFn.load(std::memory_order_seq_cst);
    bool const captured = fn && fn();
    bridgeReaders.fetch_sub(1, std::memory_order_seq_cst);
    return captured;
}
bool lholoBridgeRegistered() noexcept {
    bridgeReaders.fetch_add(1, std::memory_order_seq_cst);
    auto fn = registeredFn.load(std::memory_order_seq_cst);
    auto owner = bridgeOwner.load(std::memory_order_seq_cst);
    bool const result = fn && owner && fn(owner);
    bridgeReaders.fetch_sub(1, std::memory_order_seq_cst);
    return result;
}
void closeLholoBridge() noexcept {
    bridgeReaders.fetch_add(1, std::memory_order_seq_cst);
    if (auto close = closeFn.load(std::memory_order_seq_cst)) close();
    bridgeReaders.fetch_sub(1, std::memory_order_seq_cst);
}
} // namespace praxis::companion
