#pragma once
// 26.60 link-independent lookup for three SDK-declared but not published
// Minecraft client symbols. All lookups are by the exact decorated C++ name
// in the installed LeviLamina symbol resolver, never by guessed RVAs.
//
// Missing symbols are NOT simulated. The native transaction declines before
// any native pack stack mutation; no success receipt may be fabricated.
#include "ll/api/memory/Symbol.h"
#include "mc/client/gui/screens/models/MinecraftScreenModel.h"
#include "mc/client/gui/screens/models/MainMenuScreenModel.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include <Windows.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace praxis::companion::packs::native2660 {
using Apply = std::function<void(MinecraftScreenModel&)>;
inline void* gameCodeSymbol(char const* decorated) noexcept {
    try {
        auto* value=ll::memory::SymbolView{decorated}.resolve(true);
        if(!value)return nullptr;
        HMODULE actual{};
        if(!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(value),&actual)
            || actual!=GetModuleHandleW(nullptr))return nullptr;
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(value,&info,sizeof(info))!=sizeof(info)
            || info.State!=MEM_COMMIT || info.Type!=MEM_IMAGE
            || info.Protect&(PAGE_GUARD|PAGE_NOACCESS))return nullptr;
        auto const access=info.Protect&0xffu;
        if(access!=PAGE_EXECUTE&&access!=PAGE_EXECUTE_READ
            &&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)
            return nullptr;
        return value;
    }catch(...){return nullptr;}
}

inline void* resolveLoadingGetter() noexcept {
    static void* const result=[]() noexcept -> void* {
        try {return gameCodeSymbol("?isResourceLoadingFinished@MinecraftScreenModel@@QEBA_NXZ");}catch(...){return nullptr;}
    }();
    return result;
}
inline void* resolveTitleNavigate() noexcept {
    static void* const result=[]() noexcept -> void* {
        try {return gameCodeSymbol("?navigateToApplyResourcePacksScreen@MainMenuScreenModel@@QEAAXV?$function@$$A6AXAEAVMinecraftScreenModel@@@Z@std@@@Z");}catch(...){return nullptr;}
    }();
    return result;
}
inline void* resolveSceneFactoryApply() noexcept {
    static void* const result=[]() noexcept -> void* {
        try {return gameCodeSymbol("?createApplyResourcePacksScreen@SceneFactory@@QEAA?AV?$shared_ptr@VAbstractScene@@@std@@V?$function@$$A6AXAEAVMinecraftScreenModel@@@Z@3@@Z");}catch(...){return nullptr;}
    }();
    return result;
}
// 26.60 still declares MinecraftGame::$isInGame and
// $onFullPackStackInvalid, but neither name exists in this build's prelink
// table (using &MinecraftGame::$... fails at link time only). Resolve the
// exact decorated names through the installed runtime symbol resolver, never
// through a guessed RVA. A missing symbol returns null so the caller can fall
// back; no address is fabricated.
inline void* resolveMinecraftGameIsInGame() noexcept {
    static void* const result=[]() noexcept -> void* {
        try {return gameCodeSymbol("?$isInGame@MinecraftGame@@QEBA_NXZ");}catch(...){return nullptr;}
    }();
    return result;
}
inline void* resolveMinecraftGameFullStackInvalid() noexcept {
    static void* const result=[]() noexcept -> void* {
        try {return gameCodeSymbol("?$onFullPackStackInvalid@MinecraftGame@@QEAA_NXZ");}catch(...){return nullptr;}
    }();
    return result;
}
inline bool loadingFinished(MinecraftScreenModel const& model) noexcept {
    // Without the real predicate a resource reload must never be declared done.
    if(auto raw=resolveLoadingGetter()) {
        try {return reinterpret_cast<bool (*)(MinecraftScreenModel const*)>(raw)(&model);}
        catch(...){return false;}
    }
    return false;
}
inline bool navigateTitle(MainMenuScreenModel& model,Apply apply) {
    if(auto raw=resolveTitleNavigate()) {
        reinterpret_cast<void (*)(MainMenuScreenModel*,Apply)>(raw)(&model,std::move(apply));
        return true;
    }
    return false;
}
inline std::shared_ptr<AbstractScene> createApplyScene(SceneFactory& factory,Apply apply) {
    if(auto raw=resolveSceneFactoryApply()) {
        return reinterpret_cast<std::shared_ptr<AbstractScene> (*)(SceneFactory*,Apply)>(raw)
            (&factory,std::move(apply));
    }
    return {};
}
inline bool fullNativeApiAvailable() noexcept {
    return resolveLoadingGetter() && resolveTitleNavigate() && resolveSceneFactoryApply();
}
} // namespace praxis::companion::packs::native2660
