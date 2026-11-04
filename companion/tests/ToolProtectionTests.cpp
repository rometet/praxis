#include "core/RuntimePolicy.h"
#include "runtime/ConfigMigration.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};
    auto require = [&](bool value) { ++checks; if (!value) throw std::runtime_error("ToolProtection contract"); };
    try {
        ToolProtectionOptions o;
        require(validToolProtectionOptions(o));
        require(!toolAtThreshold(0, 0, o));
        require(!toolAtThreshold(100, -1, o));
        require(!toolAtThreshold(100, 101, o));
        require(!toolAtThreshold(100, 98, o));
        require(toolAtThreshold(100, 99, o));
        o.percent = 5;
        require(toolAtThreshold(250, 237, o));
        require(!toolAtThreshold(250, 236, o));
        // A held mouse/repeated startDestroy/attack callback never bypasses strict.
        for(int call=0;call<1000;++call){require(protectTool(true));require(!protectTool(false));}
        auto unsupported=o;unsupported.strict=false;require(!validToolProtectionOptions(unsupported));
        RuntimeConfig c; require(!c.toolProtection && c.keybinds[10] == 0);
        require(static_cast<unsigned>(Feature::TimeChanger) == 7);
        c.toolProtection = true;
        require(!runtimeSettings(c, 0).on(Feature::ToolProtection));
        require(runtimeSettings(c, 0, 1u << static_cast<unsigned>(Feature::ToolProtection)).on(Feature::ToolProtection));
        for (int oldSize : {7, 8}) {
            nlohmann::ordered_json json = {{"keybinds", std::vector<int>(oldSize, 42)}};
            require(normalizeFeatureKeybinds(json));
            for (int i = 0; i < oldSize; ++i) require(json["keybinds"][i] == 42);
            for (unsigned i = oldSize; i < featureCount; ++i) require(json["keybinds"][i] == 0);
        }
        std::cout << checks << " ToolProtection checks passed\n";
    } catch (std::exception const& e) { std::cerr << e.what(); return 1; }
}
