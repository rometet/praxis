#include "platform/InventoryRequest.h"
#include "core/Settings.h"
#include "ll/api/memory/Hook.h"
#include "mc/network/packet/ItemStackResponseSlotInfo.h"
#include "mc/network/packet/ItemStackResponseContainerInfo.h"
#include "mc/network/packet/ItemStackResponseInfo.h"
#include "mc/network/packet/ItemStackResponsePacket.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/world/inventory/network/ItemStackNetManagerClient.h"
#include "mc/world/inventory/network/ItemStackRequestBatch.h"
#include "mc/world/inventory/network/ItemStackRequestData.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/actor/player/Player.h"
#include <Windows.h>
#include <mutex>
#include <set>
namespace praxis::companion::inventory {
namespace {
std::mutex mutex;
RequestBarrier barrier;
std::set<std::int64_t> before;
std::uint64_t serial{},owner{};
bool installed{},retired{};
ItemStackNetManagerClient* currentManager(ContainerManagerController& controller) {
    auto model=controller.mContainerManagerModel.lock();
    if(!model||controller.mContainersClosed)return nullptr;
    auto base=model->mPlayer.mItemStackNetManager.get();
    if(!base||!base->mIsClientSide||!base->mIsEnabled)return nullptr;
    return static_cast<ItemStackNetManagerClient*>(base);
}
std::set<std::int64_t> requestIds(ItemStackNetManagerClient const& manager) {
    std::set<std::int64_t> ids;
    if(manager.mRequestBatch) for(auto const& request:manager.mRequestBatch->mRequests.get())
        if(request)ids.insert(request->mClientRequestId->mRawId);
    return ids;
}
LL_TYPE_INSTANCE_HOOK(InventoryResponseObservation,ll::memory::HookPriority::Normal,ClientNetworkHandler,
    &ClientNetworkHandler::$handle,void,NetworkIdentifier const& source,ItemStackResponsePacket const& packet) {
    auto const generation=worldGeneration.load();
    origin(source,packet); // Vanilla applies accepted results and rollbacks first.
    std::lock_guard lock(mutex);
    for(auto const& response:packet.mResponses.get())
        barrier.response(generation,response.mClientRequestId->mRawId,response.mResult==ItemStackNetResult::Success);
}
}
bool installRequests() {
    if(installed)return true;
    installed=InventoryResponseObservation::hook()==0;return installed;
}
bool removeRequests() noexcept {
    resetRequests();
    if(installed&&InventoryResponseObservation::unhook())installed=false;
    return !installed;
}
std::uint64_t beginRequest(ContainerManagerController& controller) {
    std::lock_guard lock(mutex);
    if(retired&&barrier.settled(worldGeneration.load())){barrier.reset();before.clear();owner=0;retired=false;}
    if(!installed||!runtimeReady.load()||barrier.busy())return 0;
    auto manager=currentManager(controller);
    if(!manager||manager->mRequest||manager->mIsHandlingResponse||!requestIds(*manager).empty())return 0;
    auto const generation=worldGeneration.load();
    if(!barrier.begin(generation,GetTickCount64()))return 0;
    before=requestIds(*manager);retired=false;owner=++serial;return owner;
}
void finishRequest(ContainerManagerController& controller,std::uint64_t token) {
    std::lock_guard lock(mutex);
    if(!token||token!=owner)return;
    std::vector<std::int64_t> added;
    if(auto manager=currentManager(controller)) for(auto id:requestIds(*manager))if(!before.contains(id))added.push_back(id);
    barrier.capture(added);before.clear();
}
RequestResult requestResult(std::uint64_t token) {
    std::lock_guard lock(mutex);
    if(!token||token!=owner)return RequestResult::Untracked;
    return barrier.result(worldGeneration.load(),GetTickCount64());
}
bool requestPathBusy() noexcept {
    std::lock_guard lock(mutex);
    return barrier.busy() || owner != 0;
}
void releaseRequest(std::uint64_t token) noexcept {
    std::lock_guard lock(mutex);if(token&&token==owner){
        // Retire the plan without losing ownership of its native request. A
        // reentrant cancellation before finishRequest must still capture IDs.
        if(barrier.settled(worldGeneration.load())){barrier.reset();before.clear();owner=0;retired=false;}
        else retired=true;
    }
}
void resetRequests() noexcept {std::lock_guard lock(mutex);barrier.reset();before.clear();owner=0;retired=false;}
}
