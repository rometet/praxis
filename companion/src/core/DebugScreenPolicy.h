#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace praxis::companion {
struct DebugScreenOptions {
    bool clientPerformance = true;
    bool serverPerformance = true;
    bool lookingAt = true;
    bool worldDiagnostics = true;
    bool showUnavailable = true;
    float x = 12.0f, y = 380.0f, scale = 1.0f;
    std::uint32_t textColor = 0xFFF2F2F2;
    bool operator==(DebugScreenOptions const&) const = default;
};
inline bool validDebugScreenOptions(DebugScreenOptions const& o) noexcept {
    auto position = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 16384.0f; };
    return position(o.x) && position(o.y) && std::isfinite(o.scale) && o.scale >= 0.5f && o.scale <= 3.0f;
}
struct DebugSnapshot {
    std::uint64_t clientTick{};
    std::uint64_t serverTick{};
    double tickDeltaSeconds{};
    double observedServerTps{};
    double observedServerMspt{};
    bool serverRateKnown{};
    bool serverMetricsAvailable{};
    std::string lookingAt;
    std::string levelId;
    std::string dimension;
    std::string worldTime;
    std::string chunk;
    std::string biome;
    std::string light;
    std::string memory;
};
inline std::vector<std::string> debugScreenLines(DebugSnapshot const& s, DebugScreenOptions const& o) {
    std::vector<std::string> out;
    auto add = [&](char const* label, std::string value) {
        if (value.empty()) value = "Unavailable";
        out.emplace_back(std::string(label) + ": " + std::move(value));
    };
    if (o.clientPerformance) {
        add("Client tick", s.clientTick ? std::to_string(s.clientTick) : "");
        add("Client tick delta",
            std::isfinite(s.tickDeltaSeconds) && s.tickDeltaSeconds > 0
                ? (std::to_string(s.tickDeltaSeconds * 1000.0).substr(0, 5) + " ms") : "");
    }
    if (o.serverPerformance) {
        add("Server tick", s.serverTick ? std::to_string(s.serverTick) : "");
        // Only an actually sampled rate is published; never a guessed 20 TPS.
        add("Observed server rate",
            s.serverRateKnown && std::isfinite(s.observedServerTps) && s.observedServerTps > 0
                ? (std::to_string(s.observedServerTps).substr(0, 5) + " tick/s")
                : "");
        add("Observed server MSPT",
            s.serverRateKnown && std::isfinite(s.observedServerMspt) && s.observedServerMspt >= 0
                ? (std::to_string(s.observedServerMspt).substr(0, 5) + " ms")
                : "");
        if (o.showUnavailable && !s.serverMetricsAvailable) {
            add("Server heap", "");
            add("Server chunk count", "");
        }
    }
    if (o.lookingAt) add("Looking at", s.lookingAt);
    if (o.worldDiagnostics) {
        add("Level ID", s.levelId);
        add("Dimension", s.dimension);
        add("World time", s.worldTime);
        add("Chunk", s.chunk);
        add("Biome", s.biome);
        add("Light", s.light);
        add("Process memory", s.memory);
    }
    return out;
}

// Java Edition F3-inspired layout, with Bedrock SDK-backed values only.
// "Observed tick interval" is NOT server MSPT; never label it as such.
struct JavaDebugColumns {
    std::vector<std::string> left, right;
};
inline JavaDebugColumns javaDebugColumns(
    DebugSnapshot const& s, DebugScreenOptions const& o,
    std::string xyz, std::string facing, double fps
) {
    JavaDebugColumns result;
    auto append = [](std::vector<std::string>& lines, char const* label, std::string value) {
        if (value.empty()) value = "Unavailable";
        lines.emplace_back(std::string(label) + ": " + std::move(value));
    };
    auto fmt = [](double value, char const* unit) {
        if (!std::isfinite(value) || value <= 0) return std::string{"Unavailable"};
        auto result = std::to_string(value);
        auto decimal = result.find('.');
        if (decimal != std::string::npos) result.resize(decimal + 3);
        return result + unit;
    };
    result.left.emplace_back("Minecraft Bedrock 1.26.60.05  (Praxis F3)");
    if (o.clientPerformance) {
        append(result.left, "FPS (UI estimate)", fmt(fps, ""));
        append(result.left, "Client tick", s.clientTick ? std::to_string(s.clientTick) : "");
        append(result.left, "Tick delta", fmt(s.tickDeltaSeconds*1000., " ms"));
    }
    if (o.worldDiagnostics) {
        result.left.emplace_back("");
        append(result.left, "XYZ", std::move(xyz));
        append(result.left, "Facing", std::move(facing));
        append(result.left, "Dimension", s.dimension);
        append(result.left, "Chunk", s.chunk);
        append(result.left, "Biome", s.biome);
        append(result.left, "Light", s.light);
        append(result.left, "World time", s.worldTime);
    }
    if (o.lookingAt) {
        result.left.emplace_back("");
        append(result.left, "Targeted block/entity", s.lookingAt);
    }
    result.right.emplace_back("Praxis Client / Debug");
    result.right.emplace_back("Client-side data; no server internals");
    if (o.worldDiagnostics) {
        result.right.emplace_back("");
        append(result.right, "Process memory", s.memory);
        append(result.right, "Level ID", s.levelId);
    }
    if (o.serverPerformance) {
        result.right.emplace_back("");
        append(result.right, "Server tick", s.serverTick ? std::to_string(s.serverTick) : "");
        append(result.right, "Observed tick rate",
            s.serverRateKnown ? fmt(s.observedServerTps, " tick/s") : "");
        append(result.right, "Observed tick interval",
            s.serverRateKnown ? fmt(s.observedServerMspt, " ms") : "");
        if (o.showUnavailable) {
            append(result.right, "Actual server MSPT", "");
            append(result.right, "Server heap", "");
            append(result.right, "Server chunk count", "");
        }
    }
    return result;
}

} // namespace praxis::companion
