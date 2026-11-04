#pragma once

#include <mutex>
#include <unordered_map>

namespace praxis::companion {

// Existing NoRender all-species owner only. WeatherChanger must never select
// or mutate this collection. Entries end at their native destructor, not at
// loader stop; the ctor/dtor/tick detours and this state remain process-owned.
template<class Object, class State> class NoRenderWeatherRegistry {
    struct Entry { State native; bool suppressed{}; };
    std::mutex mutex_;
    std::unordered_map<Object*, Entry> objects_;
public:
    template<class Restore> void restoreSource(Object& source, Restore restore) {
        std::lock_guard lock(mutex_);
        auto it = objects_.find(&source);
        if (it != objects_.end() && it->second.suppressed) {
            restore(source, it->second.native);
            it->second.suppressed = false;
        }
    }
    template<class Capture, class Suppress>
    void constructed(Object& object, bool admitted, bool suppress, Capture capture, Suppress apply) {
        std::lock_guard lock(mutex_);
        if (!admitted) return;
        objects_.insert_or_assign(&object, Entry{capture(object), suppress});
        if (suppress) apply(object);
    }
    void destroyed(Object& object) {
        std::lock_guard lock(mutex_);
        objects_.erase(&object);
    }
    template<class Restore> void beforeTick(Restore restore) {
        std::lock_guard lock(mutex_);
        for (auto& [object, entry] : objects_) if (entry.suppressed) {
            restore(*object, entry.native);
            entry.suppressed = false;
        }
    }
    template<class Capture, class Restore, class Suppress>
    void afterTick(bool suppress, Capture capture, Restore restore, Suppress apply) {
        std::lock_guard lock(mutex_);
        for (auto& [object, entry] : objects_) {
            // A constructor may have applied NoRender inside this origin.
            // Its own zero output is not an authoritative native snapshot.
            if (!entry.suppressed) entry.native = capture(*object);
            if (suppress) apply(*object);
            else if (entry.suppressed) restore(*object, entry.native);
            entry.suppressed = suppress;
        }
    }
    std::size_t size() {
        std::lock_guard lock(mutex_);
        return objects_.size();
    }
};

} // namespace praxis::companion
