#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/world/level/PackInstanceId.h"
#include <shared_mutex>
#include <type_traits>
#include <utility>

// Compile-only against the pinned client SDK. Unevaluated operands and type
// checks never obtain a live manager/stack or invoke a native getter/loader.
// An owned return value does not establish a safe source-stack read lease.
namespace praxis::companion::packs::snapshot_probe {
using IdentityGetter = std::vector<PackInstanceId> (ResourcePackStack::*)() const;
static_assert(std::is_same_v<decltype(&ResourcePackStack::getStackAsIdentities), IdentityGetter>);
using PackVisitor = void (ResourcePackManager::*)(std::function<void(PackInstance const&)> const&) const;
static_assert(std::is_same_v<decltype(&ResourcePackManager::iteratePacks), PackVisitor>);

using StackOwner = std::unique_ptr<ResourcePackStack>;
static_assert(std::is_same_v<decltype(ResourcePackManager::mGlobalStack), StackOwner>);
static_assert(std::is_same_v<decltype(ResourcePackManager::mLevelStack), StackOwner>);
static_assert(std::is_same_v<decltype(ResourcePackManager::mTreatmentStack), StackOwner>);
static_assert(std::is_same_v<decltype(ResourcePackManager::mBaseGameStack), StackOwner>);
static_assert(std::is_same_v<decltype(ResourcePackManager::mFullStack), StackOwner>);
// TypedStorage specializes unique_ptr directly: get() borrows a raw pointer.
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mGlobalStack.get()), ResourcePackStack*>);
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mLevelStack.get()), ResourcePackStack*>);
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mTreatmentStack.get()), ResourcePackStack*>);
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mBaseGameStack.get()), ResourcePackStack*>);
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mFullStack.get()), ResourcePackStack*>);
static_assert(std::is_same_v<decltype(std::declval<ResourcePackManager&>().mFullStackAccess.get()), std::shared_mutex&>);

// Future DTO conversion must preserve pack type, subpack and full semver;
// major.minor.patch alone loses prerelease/build metadata. These accessible
// types prove neither ordering nor synchronization of the manager's owners.
static_assert(std::is_same_v<decltype(std::declval<PackInstanceId const&>().mSubpackName.get()), std::string const&>);
static_assert(std::is_same_v<decltype(std::declval<PackInstanceId const&>().mPackId.get()), PackIdVersion const&>);
static_assert(std::is_same_v<decltype((std::declval<PackIdVersion const&>().mPackType)), PackType const&>);
static_assert(std::is_same_v<decltype(std::declval<PackIdVersion const&>().mId->asString()), std::string>);
static_assert(std::is_same_v<decltype(std::declval<PackIdVersion const&>().mVersion->asString()), std::string>);
static_assert(std::is_same_v<decltype((std::declval<PackInstance const&>().mSubpackIndex)), int const&>);
static_assert(std::is_convertible_v<decltype(std::declval<PackInstance const&>().mPack.get()), std::shared_ptr<ResourcePack>>);
} // namespace praxis::companion::packs::snapshot_probe
