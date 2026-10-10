#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace praxis::companion {
enum class TimeChangerSdkBinding : unsigned { LightData, PreRender, Count };
struct TimeChangerSdkBindingRecord {
    TimeChangerSdkBinding binding = TimeChangerSdkBinding::Count;
    std::uintptr_t savedTarget{}, registeredTarget{}, typedTarget{}, continuation{};
    std::uint64_t epoch{};
    bool complete{}, installed{}, admitted{};
};
// Only startup/stop metadata. Executable addresses come from this DLL's SDK
// hook storage; no native render object or borrowed reference is retained.
class TimeChangerSdkRegistration {
    std::array<std::atomic<std::uintptr_t>,2> targets_{};
    std::atomic<std::uint64_t> epoch_{};
    std::atomic_bool ready_{}, running_{}, retired_{}, valid_{true};
public:
    void begin() noexcept {
        ready_.store(false);
        auto const epoch=epoch_.fetch_add(1)+1;
        if(!epoch) valid_.store(false);
        running_.store(!retired_.load() && valid_.load());
    }
    void capture(TimeChangerSdkBinding binding,std::uintptr_t target,bool installed) noexcept {
        auto const index=static_cast<unsigned>(binding);
        if(!running_.load() || retired_.load() || index>=targets_.size() || !installed || !target) {
            valid_.store(false);return;
        }
        std::uintptr_t empty{};
        if(!targets_[index].compare_exchange_strong(empty,target) && empty!=target) valid_.store(false);
    }
    void finish() noexcept {
        bool const complete=running_.exchange(false) && valid_.load() && !retired_.load()
            && epoch_.load() && targets_[0].load() && targets_[1].load();
        if(!complete) valid_.store(false);
        ready_.store(complete);
    }
    void retire() noexcept {
        retired_.store(true);ready_.store(false);running_.store(false);epoch_.fetch_add(1);
    }
    TimeChangerSdkBindingRecord snapshot(TimeChangerSdkBinding binding,std::uintptr_t registered,
        std::uintptr_t typed,std::uintptr_t continuation,bool installed,bool admitted) const noexcept {
        auto const index=static_cast<unsigned>(binding);
        if(index>=targets_.size()) return {};
        auto const epoch=epoch_.load();
        TimeChangerSdkBindingRecord result{binding,targets_[index].load(),registered,typed,continuation,
            epoch,ready_.load() && valid_.load() && !retired_.load(),installed,admitted};
        if(epoch!=epoch_.load()) result.complete=false;
        return result;
    }
};
inline bool matchesTimeChangerSdkBinding(TimeChangerSdkBinding binding,
    TimeChangerSdkBindingRecord const& record,std::uintptr_t candidate) noexcept {
    return static_cast<unsigned>(binding)<static_cast<unsigned>(TimeChangerSdkBinding::Count) && record.binding==binding
        && candidate && record.epoch && record.complete && record.installed && record.admitted
        && record.savedTarget==candidate && record.registeredTarget==candidate
        && record.typedTarget==candidate && record.continuation;
}
TimeChangerSdkBindingRecord timeChangerSdkBindingEvidence(TimeChangerSdkBinding binding) noexcept;
bool installTimeChanger();
bool removeTimeChanger() noexcept;
}
