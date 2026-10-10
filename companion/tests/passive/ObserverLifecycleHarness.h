#pragma once
#include "core/NativeHookLifetime.h"
#include "resourcepacks/PassiveReloadTrace.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace praxis::companion::packs {
inline std::atomic<unsigned> fixtureRegistration{};
inline void reportPassiveReloadRegistration(unsigned value, char const*) noexcept {fixtureRegistration=value;}
namespace {
std::unique_ptr<NativeHookLifetime> fixtureLifetime;
#define lifetime (*fixtureLifetime)
std::atomic_bool installAttempted{},observerStopping{},passiveObserversInstalled{};
std::atomic<unsigned> installers{};
bool fixtureGuard{},fixturePin=true,fixtureThrow{};
int fixtureFail=-1,fixtureBlock=-1;
std::atomic<unsigned> fixtureGuards{},fixturePins{},fixtureHooks{},fixtureStops{};
std::atomic_bool fixtureEnabled{};
std::mutex fixtureMutex;
std::condition_variable fixtureCondition;
bool fixtureEntered{},fixtureRelease{};
bool preparePassiveReloadTrace() noexcept {
    ++fixtureGuards;fixtureEnabled=fixtureGuard;return fixtureGuard;
}
void stopPassiveReloadTrace(ReloadTraceEnd=ReloadTraceEnd::Stopped) noexcept {
    ++fixtureStops;fixtureEnabled=false;
}
bool pin(){++fixturePins;return fixturePin;}
int fixtureHook(int index){
    ++fixtureHooks;
    if(index==fixtureBlock){
        std::unique_lock lock(fixtureMutex);fixtureEntered=true;fixtureCondition.notify_all();
        fixtureCondition.wait(lock,[]{return fixtureRelease;});
    }
    if(index==fixtureFail){if(fixtureThrow)throw std::runtime_error("registration failure");return -1;}
    return 0;
}
template<int Index>struct FixtureHook {static int hook(){return fixtureHook(Index);}};
using UpdateHook=FixtureHook<0>;using ActiveHook=FixtureHook<1>;using FinishHook=FixtureHook<2>;
using TerrainHook=FixtureHook<3>;using ItemHook=FixtureHook<4>;using WorkerFactoryHook=FixtureHook<5>;
using CallbackFactoryHook=FixtureHook<6>;using TaskQueueHook=FixtureHook<7>;
using TaskCompleteHook=FixtureHook<8>;using TraceFlushHook=FixtureHook<9>;
// Namespace braces intentionally match the production branch in the included TU.
