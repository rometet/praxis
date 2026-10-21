#pragma once
#include "core/HandRestockPolicy.h"
#include "core/InventoryRequestPolicy.h"
#include "core/FeatureSlots.h"
#include "core/HotkeyPolicy.h"
#include "core/CameraInputPolicy.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

// Host doubles only. RunFlowFixture.py extracts the unchanged production
// capture/tick bodies; the separate native build checks the real SDK types.
struct Player {};
struct LocalPlayer : Player {};
enum class HandSlot { Mainhand, Offhand };
struct ContainerManagerModel { Player& mPlayer; };
namespace fixture {
using namespace praxis::companion;
using namespace praxis::companion::inventory;
inline LocalPlayer player;
inline HandSnapshot inventory;
inline std::uint64_t clock=100;
inline bool focus=true,enabled=true,failMove=false;
inline bool swapEnabled=false;
inline int swapKey='G';inline unsigned swapModifiers=8;
inline std::array<bool,256> keys{};
inline unsigned moves{},requestBegins{};
inline unsigned requestFinishes{},requestReleases{};
inline bool requestOwned{},requestRetired{};
inline std::function<void()> duringNative;
inline RequestResult response=RequestResult::Accepted;
inline std::vector<std::string> logs;
inline std::vector<std::string> addresses;
inline HandRestockOptions options;
}
inline std::uint64_t GetTickCount64() {return fixture::clock;}
inline constexpr int VK_F10=0x79,VK_F11=0x7a,VK_CONTROL=0x11,VK_SHIFT=0x10,VK_MENU=0x12;
inline int GetAsyncKeyState(int key){return fixture::keys[key]?0x8000:0;}
struct Client {LocalPlayer* getLocalPlayer(){return &fixture::player;}};
namespace ll::service {inline Client* getClientInstance(){static Client client;return &client;}}
namespace ll::event {using ListenerPtr=std::shared_ptr<int>;}
struct SlotData {
    std::string collection;int index;
    SlotData(std::string c,int i):collection(std::move(c)),index(i){}
};
struct HudContainerManagerController {
    bool mContainersClosed{};
    std::shared_ptr<ContainerManagerModel> model=std::make_shared<ContainerManagerModel>(fixture::player);
    std::weak_ptr<ContainerManagerModel> mContainerManagerModel=model;
    bool split{};
    bool offhand=true;
    bool hasContainerController(std::string const& c) const {
        return c=="inventory_items"||(split&&c=="hotbar_items")||(offhand&&c=="offhand_items");
    }
    int getContainerSize(std::string const& c) const {
        return c=="inventory_items"?(split?27:36):(c=="hotbar_items"&&split?9:c=="offhand_items"&&offhand?1:0);
    }
    int slot(SlotData const& s) const {
        if(!hasContainerController(s.collection)||s.index<0||s.index>=getContainerSize(s.collection))throw std::runtime_error("Invalid native collection slot");
        return s.collection=="offhand_items"?36:s.index+(split&&s.collection=="inventory_items"?9:0);
    }
    praxis::companion::inventory::StackValue const& getItemStack(std::string const& c,int i) const {
        return fixture::inventory.slots[slot({c,i})];
    }
    bool handlePlaceAmount(SlotData const& source,int amount,SlotData const& target) {
        ++fixture::moves;fixture::addresses={source.collection+":"+std::to_string(source.index),target.collection+":"+std::to_string(target.index)};
        if(fixture::failMove)return false;
        auto& from=fixture::inventory.slots[slot(source)];auto& to=fixture::inventory.slots[slot(target)];
        int count=to.count;if(!count)to=from;to.count=count+amount;from.count-=amount;if(!from.count)from={};if(fixture::duringNative)fixture::duringNative();return true;
    }
    bool handleSwap(SlotData const& source,SlotData const& target) {
        ++fixture::moves;if(fixture::failMove)return false;
        std::swap(fixture::inventory.slots[slot(source)],fixture::inventory.slots[slot(target)]);if(fixture::duringNative)fixture::duringNative();return true;
    }
};
namespace praxis::companion {
struct FakeSettings {
    HandRestockOptions handRestock;
    std::array<int,27> keys{};std::array<unsigned,27> keyModifiers{};
    int zoomActionKey='C',mapToggleActionKey='M';unsigned zoomActionModifiers=8,mapToggleActionModifiers=8;
    bool on(Feature f) const{return f==Feature::OffhandSwap?fixture::swapEnabled:fixture::enabled;}
};
inline constexpr unsigned ownedFeatureSlotMask=0x05b07fffu;
inline std::atomic<unsigned long long> worldGeneration{1};
inline std::array<int,6> mapActionBindings() noexcept {return {};}
inline std::shared_ptr<FakeSettings> settings(){auto s=std::make_shared<FakeSettings>();s->handRestock=fixture::options;s->keys[26]=fixture::swapKey;s->keyModifiers[26]=fixture::swapModifiers;return s;}
inline bool inventoryFocus(bool){return fixture::focus;}
inline void logGuiInfo(char const* text){fixture::logs.emplace_back(text);}
void resetHandRestock() noexcept;
namespace inventory {
inline HandSnapshot handSnapshot(LocalPlayer&){return fixture::inventory;}
inline StackValue stackValue(StackValue const& value){return value;}
inline std::uint64_t beginRequest(HudContainerManagerController&){
    if(fixture::requestOwned&&fixture::requestRetired&&(fixture::response==RequestResult::Accepted||fixture::response==RequestResult::Rejected))fixture::requestOwned=false;
    if(fixture::requestOwned)return 0;
    ++fixture::requestBegins;fixture::requestOwned=true;fixture::requestRetired=false;return 1;
}
inline void finishRequest(HudContainerManagerController&,std::uint64_t){++fixture::requestFinishes;}
inline RequestResult requestResult(std::uint64_t){return fixture::response;}
inline void releaseRequest(std::uint64_t token) noexcept{
    if(!token)return;++fixture::requestReleases;
    if(fixture::response==RequestResult::Accepted||fixture::response==RequestResult::Rejected)fixture::requestOwned=false;
    else fixture::requestRetired=true;
}
}
}
