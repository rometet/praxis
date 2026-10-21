#pragma once
#include <memory>
#include <typeinfo>

namespace praxis::companion::packs {
// Native scene/controller objects may omit MSVC RTTI. dynamic_pointer_cast
// is noexcept: __non_rtti_object would terminate before an outer catch runs.
// Keep the pointer cast outside that noexcept wrapper and retain the original
// shared owner only after a successful cast. Missing RTTI never grants access.
template<class Target, class Source>
std::shared_ptr<Target> titleNativeSharedCast(std::shared_ptr<Source> const& source,
                                            bool& typeInfoUnavailable) {
    if(!source)return {};
    try {
        auto* target=dynamic_cast<Target*>(source.get());
        return target?std::shared_ptr<Target>(source,target):std::shared_ptr<Target>{};
    } catch(std::bad_typeid const&) {
        typeInfoUnavailable=true;
        return {};
    }
}
}
