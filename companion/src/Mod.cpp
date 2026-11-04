#include "platform/Native.h"
#include "runtime/Runtime.h"
#include "core/RuntimePolicy.h"
#include "core/Settings.h"
#include "map/MapRuntime.h"
#include "ll/api/Versions.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <vector>
#include <exception>
#include <filesystem>
#include <memory>

namespace praxis::companion {
namespace {
void moduleAnchor() {}
// Refuse physical unloading if any native patch/hook could still refer to us.
void retainModuleOnCleanupFailure() noexcept {
    HMODULE handle{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&moduleAnchor), &handle);
}

LoaderVersionView loaderVersionView(ll::data::Version const& version) noexcept {
    return {
        version.major,
        version.minor,
        version.patch,
        version.preRelease.has_value(),
        version.build.has_value(),
    };
}
// The same verified 26.60.0 loader can report a plain semantic version
// without its build suffix. Accept it only if its actual loaded DLL is
// byte-for-byte the previously verified compatible loader. Do not disable
// the native patch ABI guard for a different loader build.
bool exactInstalledLoader2660() noexcept {
    constexpr std::array<unsigned char,32> expected{
        0x44,0x64,0xe0,0xf2,0x28,0x94,0xc9,0x82,
        0x52,0x8b,0xdb,0xeb,0x1a,0x35,0xb4,0xdd,
        0x5a,0xf5,0x0e,0x13,0x3e,0xd6,0x2f,0x56,
        0x09,0x0d,0x72,0xe2,0xf1,0x14,0xe0,0x75};
    auto module=GetModuleHandleW(L"LeviLamina.dll");
    if(!module)return false;
    std::array<wchar_t,32768> path{};
    auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!count||count>=path.size())return false;
    HANDLE file=CreateFileW(path.data(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct FileOwner {HANDLE h;~FileOwner(){CloseHandle(h);}} owner{file};
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart!=4801024)return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    struct BCryptOwner {
        BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;
        ~BCryptOwner(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}
    } bcryptOwner{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        return false;
    DWORD objectSize{},got{};
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&got,0)<0)
        return false;
    std::vector<unsigned char> object(objectSize);
    if(BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)<0)
        return false;
    std::array<unsigned char,65536> buffer{};
    std::uint64_t total{};
    for(;;){
        DWORD countRead{};
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&countRead,nullptr))
            return false;
        if(!countRead)break;
        if(BCryptHashData(hash,buffer.data(),countRead,0)<0)return false;
        total+=countRead;
    }
    std::array<unsigned char,32> digest{};
    return total==4801024
        &&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0
        &&digest==expected;
}
}

class CompanionMod {
    std::shared_ptr<ll::mod::NativeMod> self;
    bool enabled{};
    bool cleanupFailed{};
    bool loaderVersionAccepted{};

    bool checkLoaderVersion() {
        auto actual = ll::getLoaderVersion();
        if (supportedLoaderVersion(loaderVersionView(actual))
            && actual.to_string() == "26.60.0+f28a4b5d") return true;
        // The verified installed LeviLamina binary can report "26.60.0"
        // without the build metadata. Keep exact DLL identity validation.
        if (actual.major == 26 && actual.minor == 60 && actual.patch == 0
            && !actual.preRelease && !actual.build && exactInstalledLoader2660()) {
            if (self) self->getLogger().info(
                "LeviLamina runtime 26.60.0 accepted by exact installed DLL SHA-256.");
            return true;
        }
        if (self) {
            self->getLogger().error(
                "Unsupported LeviLamina runtime {} or unknown loader image; native hooks were not installed.",
                actual.to_string()
            );
        }
        return false;
    }

public:
    static CompanionMod& instance() {
        static CompanionMod mod;
        return mod;
    }

    bool load() {
        loaderVersionAccepted = false;
        self = ll::mod::NativeMod::current();
        if (!self) return false;
        try {
            if (!checkLoaderVersion()) return false;
            auto path = self->getConfigDir() / "config.json";
            if (!loadRuntimeConfig(path)) {
                self->getLogger().error("Invalid PraxisCompanion config (version/range): {}", path.string());
                return false;
            }
            loaderVersionAccepted = true;
            self->getLogger().info("PraxisCompanion 0.1.0 client runtime; LeviLamina compatible build verified; config: {}", path.string());
            self->getLogger().warn("Companion feature port: native utility adapters are installed where available; Minecraft runtime verification is still required.");
            return true;
        } catch (std::exception const& error) {
            loaderVersionAccepted = false;
            self->getLogger().error("PraxisCompanion load failed: {}", error.what());
            return false;
        }
    }

    bool enable() {
        if (cleanupFailed) return false;
        if (enabled) return true;
        if (!self || !loaderVersionAccepted) return false;
        try {
            // Re-check immediately before touching any native hook or patch.
            if (!checkLoaderVersion()) {
                loaderVersionAccepted = false;
                return false;
            }
            if (installNative()) {
                auto const mapRoot = std::filesystem::absolute(self->getConfigDir() / "map");
                if (!initializeMap(mapRoot)) {
                    self->getLogger().error("Praxis Map initialization failed: {}", mapRoot.string());
                    disable();
                    return false;
                }
                activateRuntime();
                enabled = true;
                self->getLogger().info("PraxisCompanion native hooks installed; features follow config (default all OFF).");
                return true;
            }
            self->getLogger().error("PraxisCompanion native hook installation failed.");
        } catch (std::exception const& error) {
            self->getLogger().error("PraxisCompanion enable failed: {}", error.what());
        } catch (...) {
            self->getLogger().error("PraxisCompanion enable failed with an unknown exception.");
        }
        disable();
        return false;
    }

    bool disable() {
        enabled = false;
        if (!beginNativeStop()) {
            cleanupFailed = true;
            if (self) self->getLogger().error("PraxisCompanion stop deferred: native callbacks in flight; hooks, state and pre-pinned DLL retained until Minecraft exits.");
            return false;
        }
        deactivateRuntime();
        if (!removeNative()) {
            cleanupFailed = true;
            retainModuleOnCleanupFailure();
            if (self) self->getLogger().error("PraxisCompanion stopped; process-lifetime native hooks and pre-pinned DLL retained. Hot unload/re-enable is unsupported; restart Minecraft.");
            return false;
        }
        availableFeatures = 0;
        availableRenderPasses = 0;
        return true;
    }

    bool unload() {
        if (cleanupFailed || !disable()) return false;
        loaderVersionAccepted = false;
        self.reset();
        return true;
    }
};
} // namespace praxis::companion

LL_REGISTER_MOD(praxis::companion::CompanionMod, praxis::companion::CompanionMod::instance());
