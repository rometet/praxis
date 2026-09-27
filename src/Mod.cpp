#include "platform/Native.h"
#include "runtime/Runtime.h"
#include "core/RuntimePolicy.h"
#include "core/Settings.h"
#include "ll/api/Versions.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include <Windows.h>
#include <exception>
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
}

class CompanionMod {
    std::shared_ptr<ll::mod::NativeMod> self;
    bool enabled{};
    bool cleanupFailed{};
    bool loaderVersionAccepted{};

    bool checkLoaderVersion() {
        auto actual = ll::getLoaderVersion();
        if (supportedLoaderVersion(loaderVersionView(actual))) return true;
        if (self) {
            self->getLogger().error(
                "Unsupported LeviLamina runtime {}. PraxisCompanion requires exactly 26.51.5; native hooks and patches were not installed.",
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
            self->getLogger().info("PraxisCompanion 0.1.0 client runtime; LeviLamina 26.51.5 exact guard PASS; config: {}", path.string());
            self->getLogger().warn("Companion feature port: native utility adapters are installed where available; Minecraft runtime verification is still required.");
            return true;
        } catch (std::exception const& error) {
            loaderVersionAccepted = false;
            self->getLogger().error("PraxisCompanion load failed: {}", error.what());
            return false;
        }
    }

    bool enable() {
        if (enabled) return true;
        if (!self || cleanupFailed || !loaderVersionAccepted) return false;
        try {
            // Re-check immediately before touching any native hook or patch.
            if (!checkLoaderVersion()) {
                loaderVersionAccepted = false;
                return false;
            }
            if (installNative()) {
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
        deactivateRuntime();
        if (!removeNative()) {
            cleanupFailed = true;
            retainModuleOnCleanupFailure();
            if (self) self->getLogger().error("PraxisCompanion cleanup incomplete; DLL retained. Restart Minecraft before retrying.");
            return false;
        }
        enabled = false;
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
