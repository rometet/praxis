#pragma once
#include <type_traits>
#include <utility>

namespace praxis::companion {
// Minecraft's saved clip is a single slot. Preserve the caller's saved value
// as well as its current clip, including construction/body exception paths.
template<class Context> class NativePreviewClipScope {
    using Saved = std::remove_cvref_t<decltype(std::declval<Context&>().mSavedOriginalClippingRectangle.get())>;
    Context &context_;
    Saved saved_;
    bool active_{true}, savedCurrent_{}, restored_{true};
public:
    explicit NativePreviewClipScope(Context &context)
        : context_(context), saved_(context.mSavedOriginalClippingRectangle.get()) {
        try {
            context_.saveCurrentClippingRectangle();
            savedCurrent_=true;
            context_.setFullClippingRectangle();
        } catch (...) { restore(); throw; }
    }
    NativePreviewClipScope(NativePreviewClipScope const&)=delete;
    NativePreviewClipScope& operator=(NativePreviewClipScope const&)=delete;
    bool restore() noexcept {
        if(!active_) return restored_;
        active_=false;
        try { if(savedCurrent_)context_.restoreSavedClippingRectangle(); }
        catch (...) { restored_=false; }
        context_.mSavedOriginalClippingRectangle.get()=saved_;
        return restored_;
    }
    ~NativePreviewClipScope(){restore();}
};
}
