#pragma once
#include "mc/client/gui/screens/ScreenEvent.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/deps/core/string/StringHash.h"
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace praxis::companion::packs {
// ScreenEventData is a generated union whose copy constructor is deleted.
// Only the ButtonEvent variant is accepted here. Its value fields are copied
// from the generated SDK ABI; the weak lifetime member is copy-constructed
// and destroyed normally. The borrowed property pointer is replaced with an
// owned UIPropertyBag with deep-copied JSON fields; this source alone
// does not establish the SDK method's runtime copy depth. The assertions below
// validate the compiled SDK layout, not live instructions or symbol addresses.
// Keep this limited adapter until the SDK provides an owning ButtonEvent clone;
// never generalize it to another union variant.
struct NativeSettingsExitEvent {
    static_assert(sizeof(ScreenEvent)==72 && offsetof(ScreenEvent,type)==0
        && offsetof(ScreenEvent,data)==8 && offsetof(ScreenEvent,scope)==48
        && offsetof(ScreenEvent,localEvent)==49 && offsetof(ScreenEvent,lifetimeCheck)==56);
    static_assert(sizeof(ScreenEventData)==40 && offsetof(ScreenEventData,button)==0);
    static_assert(std::is_trivially_copyable_v<ButtonScreenEventData>);
    static_assert(sizeof(ButtonScreenEventData)==40 && offsetof(ButtonScreenEventData,id)==0
        && offsetof(ButtonScreenEventData,fromButtonId)==4
        && offsetof(ButtonScreenEventData,properties)==16
        && offsetof(ButtonScreenEventData,cursorPosition)==24
        && offsetof(ButtonScreenEventData,isInteracted)==32
        && offsetof(ButtonScreenEventData,inputMode)==36);
    alignas(ScreenEvent) std::byte bytes[sizeof(ScreenEvent)]{};
    UIPropertyBag properties;
    std::weak_ptr<void> sourceLifetime;
    bool lifetimeTracked{};
    bool hasProperties{};

    explicit NativeSettingsExitEvent(ScreenEvent const& source,unsigned exitButton)
        : sourceLifetime(source.lifetimeCheck) {
        if(source.type!=ScreenEventType::ButtonEvent||!source.data->button->isInteracted
            ||source.data->button->id!=exitButton)throw std::invalid_argument("not the Settings exit button event");
        std::weak_ptr<void> empty;
        lifetimeTracked=sourceLifetime.owner_before(empty)||empty.owner_before(sourceLifetime);
        hasProperties=source.data->button->properties!=nullptr;
        std::memcpy(bytes,&source,offsetof(ScreenEvent,lifetimeCheck));
        std::construct_at(reinterpret_cast<std::weak_ptr<void>*>(bytes+offsetof(ScreenEvent,lifetimeCheck)),sourceLifetime);
        try {
            if (hasProperties) {
                // The destination bag starts empty, so a deep copy of its
                // JSON payload preserves the consumed button-event values
                // without the removed UIPropertyBag::append export.
                // A cloned event never publishes property-change notifications.
                auto const& borrowed=*source.data->button->properties;
                properties.mJsonValue.get()=borrowed.mJsonValue.get();
                properties.mChangeVersion=borrowed.mChangeVersion;
                // The event clone is read-only; its source may have UI
                // observers whose private copy constructor is not available
                // in 26.60. Refuse to replay such an event rather than create
                // an incompletely owned callback object.
                if(!borrowed.mPropertyChangedNotificationInfoMap.get().empty())
                    throw std::runtime_error("button event carries live property observers");
                event().data->button->properties=&properties;
            }
        } catch (...) {
            std::destroy_at(reinterpret_cast<std::weak_ptr<void>*>(bytes+offsetof(ScreenEvent,lifetimeCheck)));
            throw;
        }
    }
    ~NativeSettingsExitEvent(){
        std::destroy_at(reinterpret_cast<std::weak_ptr<void>*>(bytes+offsetof(ScreenEvent,lifetimeCheck)));
    }
    ScreenEvent& event() noexcept { return *reinterpret_cast<ScreenEvent*>(bytes); }
    bool alive() const noexcept { return (!lifetimeTracked&&!hasProperties)||!sourceLifetime.expired(); }
    NativeSettingsExitEvent(NativeSettingsExitEvent const&)=delete;
};
}
