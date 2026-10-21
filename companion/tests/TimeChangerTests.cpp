#include "core/TimeChangerPolicy.h"
#include "core/RuntimePolicy.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool success, char const* message) {
    ++checks;
    if (!success) throw std::runtime_error(message);
}
void publish(bool enabled) {
    auto current = std::make_shared<Settings>();
    current->enabled[static_cast<unsigned>(Feature::TimeChanger)] = enabled;
    publishedSettings.store(std::move(current));
}
struct NativeVisualFixture {
    int serverTime = 18000;
    int gameplayTicks = 27;
    int clockHud = 18000;
    int actualTimeArgument{};
    float actualAlphaArgument{};
    int calls{};
    float calculate(int time, float alpha) {
        ++calls; actualTimeArgument = time; actualAlphaArgument = alpha;
        // A native-origin substitute makes selection of the callback's input
        // observable. Its celestial curve is not a Minecraft shader test.
        return static_cast<float>(time) / 24000.f + alpha / 24000.f;
    }
};
}
int main() {
    try {
        RuntimeConfig config;
        check(!config.timeChanger && config.keybinds.back() == 0, "default OFF and unbound");
        check(static_cast<unsigned>(Feature::Zoom) == 6
              && static_cast<unsigned>(Feature::TimeChanger) == 7, "original seven IDs unchanged");
        config.timeChanger = true;
        check(!runtimeSettings(config, 0, 0).on(Feature::TimeChanger), "native availability required");
        check(runtimeSettings(config, 0, 1u << 7).on(Feature::TimeChanger), "available native callback publishes ON");
        NativeVisualFixture first, second;
        auto invoke = [&](NativeVisualFixture& dimension) {
            auto const before = dimension.calls;
            auto const result = visualTimeCalculation(&dimension, dimension.serverTime, .75f,
                [&](int time, float alpha) { return dimension.calculate(time, alpha); });
            check(dimension.calls == before + 1, "native origin called exactly once");
            return result;
        };
        runtimeReady = true; worldGeneration = 11;
        visualNativeAdmission = true;
        publish(false);
        check(!visualDayEnabled(), "feature OFF selects native callback fast passthrough");
        visualRenderCallback(&first, &first, true, true, true, [&] { invoke(first); });
        check(first.actualTimeArgument == 18000 && first.actualAlphaArgument == .75f, "OFF original exact input");
        publish(true);
        check(visualDayEnabled(), "active installed render callbacks may open day scope");
        invoke(first);
        check(first.actualTimeArgument == 18000, "ON outside rendering remains actual time");
        visualRenderCallback(&first, &first, true, true, true, [&] {
            auto const value = invoke(first);
            check(first.actualTimeArgument == 6000 && first.actualAlphaArgument == 0, "real production callback changes native input");
            check(value == .25f, "native origin result returned rather than raw 6000");
            invoke(second);
            check(second.actualTimeArgument == 18000, "another dimension is unchanged");
            visualRenderCallback(&second, &first, true, true, true, [&] { invoke(first); });
            check(first.actualTimeArgument == 18000, "rejected nested callback masks inherited scope");
            invoke(first);
            check(first.actualTimeArgument == 6000, "outer render scope restored");
            std::thread worker([&] { invoke(second); }); worker.join();
            check(second.actualTimeArgument == 18000, "another thread has no visual clock scope");
            publish(false); invoke(first);
            check(first.actualTimeArgument == 18000, "concurrent OFF invalidates existing scope");
            publish(true); worldGeneration.fetch_add(1); invoke(first);
            check(first.actualTimeArgument == 18000, "world leave/dimension generation invalidates scope");
        });
        check(!visualTimeContext.dimension, "render scope ends without retained game pointer");
        auto const capturedGeneration = worldGeneration.load();
        worldGeneration.fetch_add(1);
        visualRenderCallback(&first, &first, true, true, true, [&] { invoke(first); }, capturedGeneration);
        check(first.actualTimeArgument == 18000, "retired eligibility snapshot cannot inherit a new generation");
        try {
            visualRenderCallback(&first, &first, true, true, true, [&] { throw std::runtime_error("fixture"); });
        } catch (std::runtime_error const&) {}
        check(!visualTimeContext.dimension, "exception unwinds scope");
        for (auto condition : {0, 1, 2}) {
            visualRenderCallback(&first, &first, condition != 0, condition != 1, condition != 2, [&] { invoke(first); });
            check(first.actualTimeArgument == 18000, "nonprimary/loading/UI callback is excluded");
        }
        for (int transition = 0; transition < 10000; ++transition) {
            publish(transition % 2 == 0);
            worldGeneration.fetch_add(1);
            visualRenderCallback(&first, &first, true, true, true, [&] { invoke(first); });
            check(first.actualTimeArgument == (transition % 2 ? 18000 : 6000), "fresh snapshot follows ON/OFF, not cached clock");
            check(!visualTimeContext.dimension, "no scope leaks across frames");
        }
        runtimeReady = false;
        publish(true);
        check(!visualDayEnabled(), "module retirement selects native callback fast passthrough");
        visualRenderCallback(&first, &first, true, true, true, [&] { invoke(first); });
        check(first.actualTimeArgument == 18000, "module disable/cleanup immediately pass actual time");
        runtimeReady = true; visualNativeAdmission = false;
        check(!visualDayEnabled(), "retained hook rejects world/settings traversal before native origin");
        visualRenderCallback(&first, &first, true, true, true, [&] { invoke(first); });
        check(first.actualTimeArgument == 18000, "native retirement rejects new day calculation");
        { VisualNativeLease lease; check(visualNativeReaders == 1, "native reader tracks full callback"); }
        check(visualNativeReaders == 0, "native reader drains after callback");
        check(first.serverTime == 18000 && first.gameplayTicks == 27 && first.clockHud == 18000,
              "server clock, gameplay tick and HUD clock are never written");
        publishedSettings.store(std::make_shared<Settings const>());
        std::cout << "TimeChanger production render callbacks: " << checks << " checks PASS; Minecraft NOT RUN\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
