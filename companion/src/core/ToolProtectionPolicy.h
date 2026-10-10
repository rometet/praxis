#pragma once
#include <algorithm>
#include <cmath>

namespace praxis::companion {
struct ToolProtectionOptions {
    int remaining = 1;
    int percent = 0;
    bool strict = true;
    bool protectAttacks = true;
    bool operator==(ToolProtectionOptions const&) const = default;
};
inline bool validToolProtectionOptions(ToolProtectionOptions const& o) noexcept {
    return o.strict && o.remaining >= 1 && o.remaining <= 100 && o.percent >= 0 && o.percent <= 25;
}
inline bool toolAtThreshold(int maximum, int damage, ToolProtectionOptions const& o) noexcept {
    if (maximum <= 0 || damage < 0 || damage > maximum) return false;
    int const threshold = std::max(o.remaining, static_cast<int>(std::ceil(maximum * (o.percent / 100.0))));
    return maximum - damage <= threshold;
}
// Strict-only boundary guard; callback repetition never authorizes bypass.
inline bool protectTool(bool belowThreshold) noexcept {return belowThreshold;}
}
