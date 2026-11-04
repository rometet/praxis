#pragma once
#include "core/Settings.h"
#include "core/InventoryActionPolicy.h"
#include "platform/InventoryRequest.h"
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
// Native/SDK boundaries are host doubles. The runner extracts complete current
// production request ownership functions and inventory job tick bodies.
struct LocalPlayer {};
struct ContainerScreenController {};
struct SlotData {std::string collection;int index;SlotData(std::string c,int i):collection(std::move(c)),index(i){}};
struct ItemStackNetManagerClient {bool mRequest{},mIsHandlingResponse{};std::set<std::int64_t> batch;};
namespace fixture {
using namespace praxis::companion;
using namespace praxis::companion::inventory;
inline LocalPlayer player;
inline HandSnapshot hand;
inline std::vector<StackValue> storage;
inline ItemStackNetManagerClient net;
inline std::uint64_t clock=100;
inline std::int64_t nextId=-1000;
inline unsigned moves{};
inline bool managerAvailable=true,failMove=false;
inline std::function<void(std::int64_t)> duringNative;
inline std::vector<std::string> logs;
}
inline std::uint64_t GetTickCount64(){return fixture::clock;}
namespace ll::event {using ListenerPtr=std::shared_ptr<int>;}
struct ContainerManagerController {
    bool mContainersClosed{};
    praxis::companion::inventory::StackValue& value(SlotData const& address) {
        return address.collection=="inventory_items"?fixture::hand.slots.at(address.index):fixture::storage.at(address.index);
    }
    praxis::companion::inventory::StackValue const& getItemStack(std::string const& collection,int index){return value({collection,index});}
    bool handleSwap(SlotData const& source,SlotData const& target){
        ++fixture::moves;if(fixture::failMove)return false;
        std::swap(value(source),value(target));auto id=--fixture::nextId;fixture::net.batch.insert(id);
        if(fixture::duringNative)fixture::duringNative(id);return true;
    }
    bool handlePlaceAmount(SlotData const& source,int amount,SlotData const& target){
        ++fixture::moves;if(fixture::failMove)return false;
        auto& from=value(source);auto& to=value(target);int count=to.count;
        if(!count)to=from;to.count=count+amount;from.count-=amount;if(!from.count)from={};
        auto id=--fixture::nextId;fixture::net.batch.insert(id);
        if(fixture::duringNative)fixture::duringNative(id);return true;
    }
};
namespace praxis::companion {
inline void logGuiInfo(char const* message){fixture::logs.emplace_back(message);}
namespace inventory {
inline HandSnapshot handSnapshot(LocalPlayer&){return fixture::hand;}
inline StackValue stackValue(StackValue const& value){return value;}
// These two functions are SDK boundaries. begin/finish/result/release/reset
// themselves are extracted from InventoryRequest.cpp without translation.
inline ItemStackNetManagerClient* currentManager(ContainerManagerController& c){return fixture::managerAvailable&&!c.mContainersClosed?&fixture::net:nullptr;}
inline std::set<std::int64_t> requestIds(ItemStackNetManagerClient const& manager){return manager.batch;}
}
}
