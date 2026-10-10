#include "core/NativeHookLifetime.h"
#include "core/NoRenderWeatherRegistry.h"
#include "core/WeatherOwnedResultPolicy.h"
#include "core/WeatherChangerPolicy.h"
#include "core/WeatherRenderPolicy.h"
#include <array>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool ok, char const* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class T> struct Storage { T value; T& get() { return value; } };
struct ViewData { bool mDrawWeather = true, mIsShadowPass = false; };
struct Sky { float mFogLevel = .7f, brightness = .4f, time = .8f; };
struct Parameter { float fAlpha = .8f; };
struct Weather { float mDensityRain = .6f, mDensitySnow = .4f, mDensityAsh = .3f;
    Storage<std::array<Parameter, 7>> mParams; };
struct View { Storage<ViewData> mViewData; Storage<Sky> mSkyState;
    Storage<Weather> mWeatherState; float density = .6f; };
void publish(bool enabled) {
    auto settings = std::make_shared<Settings>();
    settings->enabled[static_cast<unsigned>(Feature::WeatherChanger)] = enabled;
    publishedSettings.store(settings);
}
struct Object { float rain{}, snow{}, biome{}; bool alive = true; };
struct State { float rain, snow, biome; };
State capture(Object& o) { if (!o.alive) throw std::runtime_error("capture after destruction"); return {o.rain, o.snow, o.biome}; }
void restore(Object& o, State s) {
    if (!o.alive) throw std::runtime_error("restore after destruction");
    o.rain = s.rain; o.snow = s.snow; o.biome = s.biome;
}
void suppress(Object& o) { o.rain = o.snow = o.biome = 0.f; }

void ownerFaults() {
    runtimeReady = true; weatherNativeAdmission = true; worldGeneration = 17; publish(true);
    WeatherOwnerSnapshot const before{17, 0, 0, true, true};
    std::array<WeatherOwnerSnapshot, 10> exits{before, before, before, before, before,
        before, before, before, before, before};
    exits[1].eligible = false; // secondary/capture camera
    exits[2].subClient = 1; // same generation, changed subclient
    exits[3].dimension = 1; // same generation, changed dimension
    exits[4].generation = 18;
    exits[5].hasWeather = false;
    exits[6].eligible = false; // menu / missing player / retired renderer
    exits[7].hasWeather = false;
    exits[8].generation = 16;
    exits[9].dimension = -1;
    for (unsigned i = 0; i < exits.size(); ++i) {
        View target, secondary, captureView;
        unsigned origins{}, revalidations{}, mutations{};
        auto origin = [&] { ++origins; return View{}; };
        target = origin();
        bool accepted = withOwnedWeatherResult(before, [&] { ++revalidations; return exits[i]; },
            [] { return worldGeneration.load(); }, [&](auto const& owner) {
                VisualWeatherScope verified(owner.eligible, owner.hasWeather, owner.generation);
                if (applyWeatherView(target)) {
                    suppressRenderRainAndSnow(target.mWeatherState.get()); ++mutations;
                }
            });
        check(origins == 1 && revalidations == 1, "native producer once, owner revalidated after producer");
        check(accepted == (i == 0) && mutations == (i == 0), "unknown/changed owner cannot mutate output");
        check(target.mViewData.value.mDrawWeather == (i != 0), "only verified owned return changes");
        check(secondary.mViewData.value.mDrawWeather && captureView.mViewData.value.mDrawWeather,
            "separate camera/capture outputs untouched");
        check(target.density == .6f && target.mSkyState.value.brightness == .4f
            && target.mSkyState.value.time == .8f, "native density and combined light/time unchanged");
        check(target.mWeatherState.value.mDensityRain == (i == 0 ? 0.f : .6f)
            && target.mWeatherState.value.mDensitySnow == (i == 0 ? 0.f : .4f)
            && target.mWeatherState.value.mDensityAsh == .3f, "only owned eligible return rain/snow suppressed");
        check(secondary.mWeatherState.value.mDensityRain == .6f
            && captureView.mWeatherState.value.mParams.value[0].fAlpha == .8f,
            "other producer outputs remain native");
    }
    for (unsigned fault = 0; fault < 4; ++fault) {
        worldGeneration = 17; runtimeReady = true; weatherNativeAdmission = true; publish(true);
        VisualWeatherScope outer(true, true, 17);
        View owned;
        // Inject while the native producer runs, before its owned return.
        if (fault == 0) publish(false);
        if (fault == 1) weatherNativeAdmission = false;
        if (fault == 2) runtimeReady = false;
        if (fault == 3) worldGeneration = 18;
        withOwnedWeatherResult(before, [&] { return before; }, [] { return worldGeneration.load(); },
            [&](auto const& owner) {
                VisualWeatherScope verified(owner.eligible, owner.hasWeather, owner.generation);
                if (applyWeatherView(owned)) suppressRenderRainAndSnow(owned.mWeatherState.get());
            });
        check(owned.mViewData.value.mDrawWeather && owned.mSkyState.value.mFogLevel == .7f,
            "OFF/stop/world retirement during origin cannot reuse outer admission");
        check(owned.mWeatherState.value.mDensityRain == .6f, "post-origin rejection preserves native owned density");
    }
}

void lifetimeFaults() {
    NativeHookLifetime failedPin;
    unsigned registrations{};
    if (failedPin.prepare([] { return false; })) ++registrations;
    check(registrations == 0 && !failedPin.open() && !failedPin.retained(), "pin failure installs no hook");
    for (unsigned failAt = 0; failAt < 4; ++failAt) {
        NativeHookLifetime lifetime;
        unsigned pins{}, installed{};
        check(lifetime.prepare([&] { ++pins; return true; }), "pin before any registration");
        lifetime.registrationStarted();
        for (unsigned hook = 0; hook < 3; ++hook) {
            if (hook == failAt) break; // failed registration keeps every earlier hook
            ++installed;
        }
        if (installed == 3) check(lifetime.open(), "all installed enables admission");
        lifetime.close();
        unsigned unhooks{}; // the retained design has no unhook action to fail
        check(pins == 1 && lifetime.retained() && unhooks == 0, "partial install cannot retire origins or module");
        check(!lifetime.open() && !lifetime.prepare([] { return true; }), "stopped retained module cannot re-enable");
        NativeHookLifetime::Lease passthrough(lifetime);
        check(!passthrough.admitted() && lifetime.readers() == 1, "late callback is retained passthrough");
    }
    NativeHookLifetime lifetime;
    check(lifetime.prepare([] { return true; }), "concurrent fixture pin");
    lifetime.registrationStarted(); check(lifetime.open(), "concurrent fixture open");
    std::latch entered(1), release(1);
    std::atomic_bool nativeCompleted{}, afterCloseMutation{};
    std::thread callback([&] {
        NativeHookLifetime::Lease lease(lifetime);
        entered.count_down(); release.wait(); // injected native origin in flight
        afterCloseMutation = lease.admitted(); nativeCompleted = true;
    });
    entered.wait(); lifetime.close();
    bool deferCleanup = lifetime.readers() != 0;
    release.count_down(); callback.join();
    check(deferCleanup && nativeCompleted && !afterCloseMutation, "stop defers resource cleanup and permits origin completion");
    check(lifetime.readers() == 0 && lifetime.retained(), "drained origin still does not authorize trampoline retirement");
    lifetime.close(); check(!lifetime.admitted(), "repeated stop is idempotent");
}

void registryFaults() {
    NoRenderWeatherRegistry<Object, State> registry;
    Object primary{.6f, .4f, .3f}, other{.7f, .5f, .2f};
    registry.constructed(primary, true, true, capture, suppress);
    registry.constructed(other, true, true, capture, suppress);
    check(registry.size() == 2 && primary.rain == 0.f && other.biome == 0.f,
        "NoRender retains its explicit all-species collection");
    // Close while origin is in flight: do not clear entries or restore from
    // a different control thread. The callback finishes with suppression OFF.
    registry.beforeTick(restore);
    check(primary.rain == .6f && other.rain == .7f, "native tick sees latest saved native values");
    primary.rain = .9f; other.rain = .8f;
    registry.afterTick(false, capture, restore, suppress);
    check(registry.size() == 2 && primary.rain == .9f && other.rain == .8f,
        "stop during origin retains ownership and fresh native results");
    registry.afterTick(true, capture, restore, suppress);
    registry.restoreSource(primary, restore);
    Object moved{primary.rain, primary.snow, primary.biome};
    registry.constructed(moved, false, false, capture, suppress);
    check(moved.rain == .9f && registry.size() == 2, "move after stop transfers restored native value without new admission");
    registry.destroyed(other); other.alive = false;
    registry.beforeTick(restore); registry.afterTick(false, capture, restore, suppress);
    check(registry.size() == 1 && primary.rain == .9f, "retained destructor removes entry before native lifetime ends");
    registry.destroyed(primary); primary.alive = false;
    registry.beforeTick(restore); registry.afterTick(false, capture, restore, suppress);
    check(registry.size() == 0, "all native deaths end ownership without stop-time clear");
    Object reused{.2f, .1f, .8f};
    registry.constructed(reused, false, true, capture, suppress);
    check(registry.size() == 0 && reused.rain == .2f, "no new suppression after close");
    // Fault: a constructor ran while native origin was executing and applied
    // NoRender before stop. Closing must restore that owned modification,
    // rather than publishing/caching the plugin's zero as real weather.
    Object duringOrigin{.65f, .35f, .25f};
    registry.beforeTick(restore);
    registry.constructed(duringOrigin, true, true, capture, suppress);
    registry.afterTick(false, capture, restore, suppress);
    check(duringOrigin.rain == .65f && duringOrigin.snow == .35f && duringOrigin.biome == .25f,
        "stop inside origin restores newly constructed NoRender output without caching its zero");
    // When NoRender never modified a captured object, an unrelated native
    // update must not be rolled back by the next camera tick or move.
    duringOrigin.rain = .85f;
    registry.beforeTick(restore); registry.restoreSource(duringOrigin, restore);
    check(duringOrigin.rain == .85f, "unmodified native objects are not restored from stale cache");
    registry.afterTick(true, capture, restore, suppress);
    registry.afterTick(true, capture, restore, suppress);
    registry.afterTick(false, capture, restore, suppress);
    check(duringOrigin.rain == .85f, "repeated post-tick suppression cannot overwrite native snapshot with plugin zero");
    registry.destroyed(duringOrigin);
}
}
int main() {
    try {
        ownerFaults(); lifetimeFaults(); registryFaults();
        std::cout << "WeatherLifetimeTests PASS " << checks << " checks; simulated origin/lifetime faults only\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
