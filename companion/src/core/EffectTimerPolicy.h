#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace praxis::companion {
struct EffectTimerOptions {
    bool showAmplifier = true;
    bool showRemaining = true;
    bool showAmbient = false;
    bool sortByRemaining = true;
    bool compact = false;
    float x = 12.0f, y = 140.0f, scale = 1.0f;
    std::uint32_t textColor = 0xFFF2F2F2;
    bool operator==(EffectTimerOptions const&) const = default;
};
inline bool validEffectTimerOptions(EffectTimerOptions const& o) noexcept {
    auto position = [](float v) { return std::isfinite(v) && v >= 0 && v <= 16384; };
    return position(o.x) && position(o.y) && std::isfinite(o.scale) && o.scale >= .5f && o.scale <= 3.0f;
}
struct EffectEntry {
    std::string name;
    int amplifier{};
    int durationTicks{};
    bool ambient{};
    bool operator==(EffectEntry const&) const = default;
};
inline std::string effectDurationText(int ticks) {
    if (ticks < 0) return "∞";
    int seconds = ticks / 20;
    if (seconds < 0) return "0:00";
    char text[32]{};
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    return text;
}
inline std::vector<std::string> effectTimerLines(std::vector<EffectEntry> entries, EffectTimerOptions const& o) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](EffectEntry const& e) {
        return e.name.empty() || e.name.size() > 128 || e.durationTicks == 0;
    }), entries.end());
    if (entries.size() > 16) entries.resize(16);
    if (o.sortByRemaining) {
        std::stable_sort(entries.begin(), entries.end(), [](EffectEntry const& a, EffectEntry const& b) {
            auto rank = [](EffectEntry const& e) { return e.durationTicks < 0 ? UINT32_MAX : static_cast<std::uint32_t>(e.durationTicks); };
            return rank(a) < rank(b);
        });
    }
    std::vector<std::string> lines;
    lines.reserve(entries.size());
    for (auto const& e : entries) {
        if (e.ambient && !o.showAmbient) continue;
        std::string line = e.name;
        if (o.showAmplifier && e.amplifier > 0) line += " " + std::to_string(e.amplifier + 1);
        if (o.showRemaining) {
            if (o.compact) line += " " + effectDurationText(e.durationTicks);
            else line += ": " + effectDurationText(e.durationTicks);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}
struct EffectHudLayout { float x{}, y{}, width{}, height{}, scale{}; };
} // namespace praxis::companion
