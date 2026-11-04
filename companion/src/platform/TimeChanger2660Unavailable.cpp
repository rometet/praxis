#include "platform/TimeChanger.h"

// The original 26.51 visual clock hooks require four native symbols that
// are not exported by the official 26.60 MCAPI. Do not install stale hooks,
// alter the simulation clock, or advertise the TimeChanger capability.
namespace praxis::companion {
TimeChangerSdkBindingRecord timeChangerSdkBindingEvidence(TimeChangerSdkBinding) noexcept {
    return {};
}
bool installTimeChanger() { return false; }
bool removeTimeChanger() noexcept { return true; }
}