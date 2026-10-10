#include "platform/InventoryActions.h"
#include "platform/InventoryRequest.h"
#include "platform/InventorySnapshot.h"
#include "core/Settings.h"
#include "core/InventoryActionPolicy.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/controllers/ContainerController.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include <Windows.h>
#include <mutex>
#include <set>
#include <map>
#include <stdexcept>
#include <deque>
namespace praxis::companion {
namespace {
using namespace inventory;
struct Address {std::string collection;int index{};bool operator==(Address const&)const=default;};
struct Region {std::vector<Address> slots;std::vector<StackValue> values;std::vector<std::string> aliases;bool operator==(Region const&)const=default;};
struct Snapshot {
    HandSnapshot player;
    Region inventory,storage;
    bool operator==(Snapshot const&)const=default;
};
struct Context {
    std::shared_ptr<ContainerScreenController> screen;
    std::shared_ptr<ContainerManagerController> manager;
    LocalPlayer* player{}; // borrowed for this UI call only
    Snapshot snapshot;
    std::uintptr_t view{};
};
struct Job {
    std::weak_ptr<ContainerScreenController> screen;
    std::uintptr_t view{};
    Snapshot expected;
    Snapshot submittedState;
    std::vector<MoveStep> steps;
    InventorySortOptions options;
    InventoryTransferOptions transferOptions;
    std::size_t index{};
    std::uint64_t token{},started{},lastStep{};
    bool storage{},transfer{};
};
struct InputHover {
    std::weak_ptr<ContainerScreenController> screen;
    std::uintptr_t view{};
    std::uint64_t generation{},seen{};
    unsigned session{};
    int index=-1;
    StackValue reference;
};
struct Pulse {InputHover target;TransferGesture gesture{};int direction{};unsigned stroke{};};
std::recursive_mutex mutex;
std::shared_ptr<Job> job;
std::map<std::uintptr_t,std::set<std::uintptr_t>> editors;
ll::event::ListenerPtr uiListener;
std::atomic_bool resetRequested{};
bool faulted{};
std::array<InventoryActionEdge,unsigned(InventoryAction::Count)> actionEdges;
std::optional<InputHover> inputHover;
std::weak_ptr<ContainerScreenController> inputOwner;
std::uintptr_t inputOwnerView{};
std::uint64_t inputOwnerGeneration{};
std::deque<Pulse> pulses;
TransferStroke stroke;
unsigned inputSession{};
std::atomic_int consumedButton{};
std::atomic_bool strokeCancelRequested{};
void cancel() {if(job)releaseRequest(job->token);job.reset();}
void clearInput(){inputHover.reset();inputOwner.reset();inputOwnerView=0;inputOwnerGeneration=0;pulses.clear();stroke.cancel();++inputSession;}
void abortAll(){cancel();clearInput();}
bool foreground(){DWORD id{};auto window=GetForegroundWindow();return window&&GetWindowThreadProcessId(window,&id)&&id==GetCurrentProcessId();}
bool ordinary(SharedTypes::Legacy::ContainerType type) {
    using SharedTypes::Legacy::ContainerType;
    return type==ContainerType::Container||type==ContainerType::MinecartChest||type==ContainerType::ChestBoat;
}
void append(Region& region,ContainerManagerController& manager,std::string const& collection,int size) {
    if(size<0||size>54)throw std::runtime_error("Unsupported inventory region size");
    for(int i=0;i<size;++i){region.slots.push_back({collection,i});region.values.push_back(stackValue(manager.getItemStack(collection,i)));}
}
std::optional<Context> context(ll::event::AfterUIRenderEvent& event) {
    auto& view=event.screenView();auto& client=event.uiRenderContext().mClient;
    if(!inventoryFocus(false)||!view.mHasFocus||view.mIsEntering||view.mIsExiting||view.mIsTerminating
        ||view.getScreenName()!=client.getTopScreenName())return {};
    std::shared_ptr<ScreenController> base=view.mController;
    if(!base||!base->_isContainerScreen())return {};
    auto screen=std::static_pointer_cast<ContainerScreenController>(base);
    std::shared_ptr<ContainerManagerController> manager=screen->mContainerManagerController;auto player=client.getLocalPlayer();
    if(!manager||!player||manager->mContainersClosed||screen->_isCursorSelectedActive())return {};
    auto const viewId=reinterpret_cast<std::uintptr_t>(&view);
    if(editors.contains(viewId)&&!editors.at(viewId).empty())return {};
    auto model=manager->mContainerManagerModel.lock();if(!model||&model->mPlayer!=player)return {};
    auto const type=manager->getContainerType();
    if(type!=SharedTypes::Legacy::ContainerType::Inventory&&!ordinary(type))return {};
    auto const config=settings();bool const storageGui=ordinary(type);
    bool const sortExcluded=storageGui?config->inventorySort.excludeStorageGui:config->inventorySort.excludeInventoryGui;
    bool const transferExcluded=storageGui?config->inventoryTransfer.excludeStorageGui:config->inventoryTransfer.excludeInventoryGui;
    if((!config->on(Feature::InventorySort)||sortExcluded)&&(!config->on(Feature::InventoryTransfer)||transferExcluded))return {};
    Context result;result.screen=screen;result.manager=manager;result.player=player;result.view=viewId;
    result.snapshot.player=handSnapshot(*player);
    if(!manager->hasContainerController("inventory_items"))return {};
    int const size=manager->getContainerSize("inventory_items");
    if(size==27){
        if(!manager->hasContainerController("hotbar_items")||manager->getContainerSize("hotbar_items")!=9)return {};
        append(result.snapshot.inventory,*manager,"hotbar_items",9);
    }else if(size!=36)return {};
    append(result.snapshot.inventory,*manager,"inventory_items",size);
    // Collection aliases must actually describe the current player's slots.
    for(int i=0;i<36;++i)if(result.snapshot.inventory.values[i]!=result.snapshot.player.slots[i])return {};
    if(ordinary(type))for(auto const name:{"container_items","barrel_items","shulker_box_items"}) {
        if(!manager->hasContainerController(name))continue;
        int const count=manager->getContainerSize(name);if(count<1||count>54)continue;
        auto& storage=result.snapshot.storage;
        if(storage.slots.empty())append(storage,*manager,name,count);
        else {
            auto a=manager->getContainerController(storage.slots[0].collection),b=manager->getContainerController(name);
            if(!a||!b||!a->mContainerModel.lock()||a->mContainerModel.lock()!=b->mContainerModel.lock()
                ||count!=static_cast<int>(storage.values.size()))return {};
            for(int i=0;i<count;++i)if(stackValue(manager->getItemStack(name,i))!=storage.values[i])return {};
        }
        storage.aliases.emplace_back(name);
    }
    if(ordinary(type)&&result.snapshot.storage.slots.empty())return {};
    std::size_t bytes{};
    for(auto const& s:result.snapshot.inventory.values)bytes+=s.key.size();
    for(auto const& s:result.snapshot.storage.values)bytes+=s.key.size();
    if(bytes>1048576||!validStacks(result.snapshot.inventory.values)||!validStacks(result.snapshot.storage.values))return {};
    return result;
}
std::optional<std::pair<bool,int>> hovered(Context const& c) {
    auto const& slot=c.screen->mHoveredSlotData.get();
    if(!c.screen->mShowHoverText||c.screen->mHoverTextSuppressedUntilPointerMoves)return {};
    auto const& storage=c.snapshot.storage;
    if(std::find(storage.aliases.begin(),storage.aliases.end(),slot.mCollectionName.get())!=storage.aliases.end()
        &&slot.mCollectionIndex>=0&&slot.mCollectionIndex<static_cast<int>(storage.values.size()))return {{true,slot.mCollectionIndex}};
    for(bool storage:{false,true}) {
        auto const& region=storage?c.snapshot.storage:c.snapshot.inventory;
        for(int i=0;i<static_cast<int>(region.slots.size());++i){auto const& address=region.slots[i];
            bool const alias=!storage&&slot.mCollectionName.get()=="hotbar_items"&&i<9&&slot.mCollectionIndex==i;
            if(alias||(address.collection==slot.mCollectionName.get()&&address.index==slot.mCollectionIndex))return {{storage,i}};
        }
    }
    return {};
}
void stop(std::string const& reason) {faulted=true;abortAll();logGuiInfo(("Inventory action stopped: "+reason).c_str());}
std::vector<StackValue> flatten(Snapshot const& snapshot) {
    auto result=snapshot.inventory.values;result.insert(result.end(),snapshot.storage.values.begin(),snapshot.storage.values.end());return result;
}
Address const* address(Snapshot const& snapshot,Job const& op,int index) {
    auto const& region=op.transfer?(index<36?snapshot.inventory:snapshot.storage):(op.storage?snapshot.storage:snapshot.inventory);
    if(op.transfer&&index>=36)index-=36;
    return index>=0&&index<static_cast<int>(region.slots.size())?&region.slots[index]:nullptr;
}
bool predict(Job& op,MoveStep const& step) {
    if(op.transfer){
        auto values=flatten(op.expected);if(!applyMove(values,step))return false;
        std::copy_n(values.begin(),36,op.expected.inventory.values.begin());
        std::copy(values.begin()+36,values.end(),op.expected.storage.values.begin());
    }else if(!applyMove(op.storage?op.expected.storage.values:op.expected.inventory.values,step))return false;
    for(int i=0;i<36;++i)op.expected.player.slots[i]=op.expected.inventory.values[i];return true;
}
Snapshot liveSnapshot(Context const& c) {
    auto result=c.snapshot;result.player=handSnapshot(*c.player);
    for(auto region:{&result.inventory,&result.storage})
        for(std::size_t i=0;i<region->slots.size();++i){auto const& a=region->slots[i];
            region->values[i]=stackValue(c.manager->getItemStack(a.collection,a.index));}
    return result;
}
void tick(Context const& c) {
    if(!job)return;
    auto retained=job;auto& op=*retained;auto s=settings();auto const time=GetTickCount64();
    if(!s->on(op.transfer?Feature::InventoryTransfer:Feature::InventorySort)||s->inventorySort!=op.options
        ||(op.transfer&&s->inventoryTransfer!=op.transferOptions)||op.screen.lock()!=c.screen||op.view!=c.view
        ||op.expected.player.generation!=worldGeneration.load()){abortAll();return;}
    if(op.token){
        auto const result=requestResult(op.token);
        if(result==RequestResult::Waiting){
            // Compare the complete post-submit native observation before
            // waiting. Cancel the plan; releaseRequest retains its ACK lease.
            if(c.snapshot!=op.submittedState)stop("inventory changed while awaiting acknowledgement");
            return;
        }
        if(result!=RequestResult::Accepted){stop("request rejected, timed out or untracked");return;}
        releaseRequest(op.token);op.token=0;
    }
    if(c.snapshot!=op.expected){stop("inventory changed");return;}
    if(op.index==op.steps.size()){cancel();return;}
    if(time<op.started||time-op.started>60000){stop("operation deadline");return;}
    if(time<op.lastStep||time-op.lastStep<50)return;
    auto const step=op.steps[op.index];auto a=address(c.snapshot,op,step.source),b=address(c.snapshot,op,step.target);
    if(!a||!b){stop("invalid plan");return;}
    // Predict only owned values. Native simulation and server response own rollback.
    if(!predict(op,step)){stop("invalid predicted move");return;}
    op.token=beginRequest(*c.manager);if(!op.token){cancel();return;}
    SlotData const source(a->collection,a->index),target(b->collection,b->index);
    bool const success=step.swap?c.manager->handleSwap(source,target):c.manager->handlePlaceAmount(source,step.amount,target);
    finishRequest(*c.manager,op.token);
    if(job!=retained)return;
    if(!success){stop("vanilla move failed");return;}
    op.submittedState=liveSnapshot(c);
    ++op.index;op.lastStep=time;
}
void onFrame(ll::event::AfterUIRenderEvent& event) {
    std::lock_guard lock(mutex);
    if(resetRequested.exchange(false)){abortAll();editors.clear();actionEdges={};faulted=false;}
    if(strokeCancelRequested.exchange(false)){pulses.clear();stroke.cancel();if(job&&job->transfer)cancel();}
    auto& view=event.screenView();auto& client=event.uiRenderContext().mClient;
    // Underlying HUD views must not cancel the active top container's job.
    if(view.getScreenName()!=client.getTopScreenName())return;
    auto s=settings();std::array<bool,unsigned(InventoryAction::Count)> pressed{};
    unsigned const mods=modifierBits((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,(GetAsyncKeyState(VK_MENU)&0x8000)!=0);
    for(unsigned i=0;i<pressed.size();++i){auto chord=inventoryActionBinding(*s,InventoryAction(i));
        pressed[i]=actionEdges[i].sample(chord,chord.key>0&&(GetAsyncKeyState(chord.key)&0x8000)!=0,mods,inventoryFocus(false)&&!inventoryActionConflict(*s,InventoryAction(i)));}
    if(pressed[unsigned(InventoryAction::Cancel)]){abortAll();faulted=false;return;}
    if(!s->on(Feature::InventorySort)&&!s->on(Feature::InventoryTransfer)){abortAll();faulted=false;return;}
    auto c=context(event);if(!c){abortAll();return;}
    bool const storageGui=!c->snapshot.storage.values.empty();
    bool const sortEnabled=s->on(Feature::InventorySort)&&!(storageGui?s->inventorySort.excludeStorageGui:s->inventorySort.excludeInventoryGui);
    bool const transferEnabledHere=s->on(Feature::InventoryTransfer)&&!(storageGui?s->inventoryTransfer.excludeStorageGui:s->inventoryTransfer.excludeInventoryGui);
    auto slot=hovered(*c);
    if(inputOwner.lock()!=c->screen||inputOwnerView!=c->view||inputOwnerGeneration!=worldGeneration.load())clearInput();
    inputOwner=c->screen;inputOwnerView=c->view;inputOwnerGeneration=worldGeneration.load();
    auto const mode=stroke.gesture();
    bool const held=mode==TransferGesture::DragOne?(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0:
        mode==TransferGesture::DragStack&&(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    if(mode!=TransferGesture::None&&!held){pulses.clear();stroke.cancel();if(job&&job->transfer)cancel();}
    if(transferEnabledHere&&!c->snapshot.storage.values.empty()&&slot&&(!job||job->transfer)){
        if(inputHover&&(inputHover->screen.lock()!=c->screen||inputHover->view!=c->view
            ||inputHover->generation!=worldGeneration.load()))clearInput();
        int const index=slot->first?slot->second+36:slot->second;
        auto const& values=slot->first?c->snapshot.storage.values:c->snapshot.inventory.values;
        InputHover hover;hover.screen=c->screen;hover.view=c->view;hover.generation=worldGeneration.load();hover.seen=GetTickCount64();
        hover.session=inputSession;hover.index=index;hover.reference=values[slot->second];
        bool const excluded=s->inventoryTransfer.respectSortExclusions
            &&(index<36?s->inventorySort.excludedInventory[index]:s->inventorySort.excludedStorage[index-36]);
        if(hover.reference.count>0&&!hover.reference.locked&&!excluded&&!(s->inventoryTransfer.preserveHotbar&&index<9))inputHover=std::move(hover);else inputHover.reset();
        if(inputHover&&stroke.visit(index)&&pulses.size()<32)pulses.push_back({*inputHover,stroke.gesture(),0,stroke.revision()});
    }else {inputHover.reset();if(!transferEnabledHere||c->snapshot.storage.values.empty()||(job&&!job->transfer)){pulses.clear();stroke.cancel();}}
    if(job&&!(job->transfer?transferEnabledHere:sortEnabled)){abortAll();return;}
    if(job){tick(*c);return;}
    if(sortEnabled&&pressed[unsigned(InventoryAction::Sort)]){
    faulted=false;clearInput();if(!slot)return;
    auto const& region=slot->first?c->snapshot.storage:c->snapshot.inventory;
    auto plan=planSort(region.values,!slot->first,s->inventorySort);if(!plan||plan->steps.empty())return;
    Job next;next.screen=c->screen;next.view=c->view;next.expected=c->snapshot;next.steps=std::move(plan->steps);
    next.options=s->inventorySort;next.started=GetTickCount64();next.storage=slot->first;job=std::make_shared<Job>(std::move(next));
    tick(*c);
    return;
    }
    if(transferEnabledHere&&slot&&storageGui&&(pressed[unsigned(InventoryAction::SameType)]||pressed[unsigned(InventoryAction::All)])){
        clearInput();int const index=slot->first?slot->second+36:slot->second;
        auto const gesture=pressed[unsigned(InventoryAction::All)]?TransferGesture::All:TransferGesture::SameType;
        auto plan=planTransfer(c->snapshot.inventory.values,c->snapshot.storage.values,index,gesture,0,s->inventoryTransfer,s->inventorySort);
        if(!plan)return;
        faulted=false;Job next;next.screen=c->screen;next.view=c->view;next.expected=c->snapshot;next.steps=std::move(plan->steps);
        next.options=s->inventorySort;next.transferOptions=s->inventoryTransfer;next.transfer=true;next.started=GetTickCount64();
        job=std::make_shared<Job>(std::move(next));tick(*c);return;
    }
    if(!transferEnabledHere||pulses.empty())return;
    auto pulse=std::move(pulses.front());pulses.pop_front();auto const& target=pulse.target;
    if(target.session!=inputSession||target.generation!=worldGeneration.load()||target.screen.lock()!=c->screen||target.view!=c->view)return;
    bool const wheel=pulse.gesture==TransferGesture::WheelOne||pulse.gesture==TransferGesture::WheelStack;
    if((wheel&&(!slot||(slot->first?slot->second+36:slot->second)!=target.index))||(!wheel&&pulse.stroke!=stroke.revision()))return;
    auto values=flatten(c->snapshot);if(target.index<0||target.index>=static_cast<int>(values.size()))return;
    auto const& current=values[target.index];
    if(current.key!=target.reference.key||current.locked||(!wheel&&current!=target.reference)){pulses.clear();return;}
    auto plan=planTransfer(c->snapshot.inventory.values,c->snapshot.storage.values,target.index,pulse.gesture,pulse.direction,s->inventoryTransfer,s->inventorySort);
    if(!plan)return;
    faulted=false;Job next;next.screen=c->screen;next.view=c->view;next.expected=c->snapshot;next.steps=std::move(plan->steps);
    next.options=s->inventorySort;next.transferOptions=s->inventoryTransfer;next.transfer=true;next.started=GetTickCount64();
    job=std::make_shared<Job>(std::move(next));tick(*c);
}
LL_TYPE_INSTANCE_HOOK(InventoryTextFocus,ll::memory::HookPriority::Normal,ScreenView,
    &ScreenView::_fireSelectedStateChangeEvent,void,TextEditComponent const& component,bool selected) {
    {std::lock_guard lock(mutex);auto id=reinterpret_cast<std::uintptr_t>(this);auto token=reinterpret_cast<std::uintptr_t>(&component);
     if(selected){editors[id].insert(token);if(inputOwnerView==id)abortAll();}
     else if(auto it=editors.find(id);it!=editors.end()){it->second.erase(token);if(it->second.empty())editors.erase(it);}}
    origin(component,selected);
}
LL_TYPE_INSTANCE_HOOK(InventoryViewDestroyed,ll::memory::HookPriority::Normal,ScreenView,&ScreenView::$dtor,void) {
    {std::lock_guard lock(mutex);auto id=reinterpret_cast<std::uintptr_t>(this);editors.erase(id);if((job&&job->view==id)||inputOwnerView==id)abortAll();}
    origin();
}
LL_TYPE_INSTANCE_HOOK(InventoryContainerLeave,ll::memory::HookPriority::Normal,ContainerScreenController,&ContainerScreenController::$onLeave,void) {
    {std::lock_guard lock(mutex);if((job&&job->screen.lock().get()==this)||inputOwner.lock().get()==this)abortAll();}
    origin();
}
struct Hook{int(*add)();bool(*remove)();bool installed{};};
template<class T>Hook hook(){return{[]{return T::hook();},[]{return T::unhook();},false};}
Hook hooks[]{hook<InventoryTextFocus>(),hook<InventoryViewDestroyed>(),hook<InventoryContainerLeave>()};
}
void resetInventoryActions() noexcept {resetRequested=true;}
void serviceInventoryActions() noexcept {
    // The game tick never waits for a UI-owned native operation.
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(resetRequested.exchange(false)){abortAll();editors.clear();actionEdges={};faulted=false;}
    if(job&&(job->screen.expired()||!runtimeReady.load()||guiInputCaptured()||job->expected.player.generation!=worldGeneration.load()
        ||!settings()->on(job->transfer?Feature::InventoryTransfer:Feature::InventorySort)||!foreground()))abortAll();
    if(inputHover&&(!runtimeReady.load()||guiInputCaptured()||!foreground()||inputHover->generation!=worldGeneration.load()))clearInput();
    if(job&&job->token&&requestResult(job->token)!=RequestResult::Waiting
        &&requestResult(job->token)!=RequestResult::Accepted)stop("request watchdog");
}
bool handleInventoryMouse(int button,int data,bool alreadyCancelled) noexcept {
    if(button==MouseAction::ActionLeft&&data==MouseAction::DataUp&&consumedButton.exchange(0)==button){strokeCancelRequested=true;return true;}
    if(button==MouseAction::ActionLeft&&data==MouseAction::DataDown){consumedButton=0;strokeCancelRequested=true;}
    try {
        std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return false;
        if(strokeCancelRequested.exchange(false)){pulses.clear();stroke.cancel();if(job&&job->transfer)cancel();}
        if(button==MouseAction::ActionMove||button==MouseAction::ActionMoveRelative){inputHover.reset();return false;}
        auto s=settings();auto const time=GetTickCount64();
        if(alreadyCancelled||!s->on(Feature::InventoryTransfer)||!runtimeReady.load()||guiInputCaptured()||!foreground()
            ||!inputHover||inputHover->generation!=worldGeneration.load()||inputHover->screen.expired()
            ||time<inputHover->seen||time-inputHover->seen>150||(job&&!job->transfer)||pulses.size()>=32)return false;
        bool const shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,control=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
        if(button==MouseAction::ActionWheel){
            auto const mode=shift?TransferGesture::WheelStack:TransferGesture::WheelOne;
            if(!data||!transferEnabled(mode,s->inventoryTransfer))return false;
            pulses.push_back({*inputHover,mode,data>0?1:-1,stroke.revision()});return true;
        }
        if(button==MouseAction::ActionLeft&&data==MouseAction::DataDown){
            auto const mode=dragGesture(button,shift,control);
            if(!stroke.press(button,transferEnabled(mode,s->inventoryTransfer),mode))return false;
            consumedButton=button;
            if(stroke.visit(inputHover->index))pulses.push_back({*inputHover,mode,0,stroke.revision()});return true;
        }
    }catch(...){strokeCancelRequested=true;}
    return false;
}
bool installInventoryActions() {
    for(auto& h:hooks)if(!h.installed){if(h.add()!=0){removeInventoryActions();return false;}h.installed=true;}
    try {if(!uiListener)uiListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
        [](ll::event::AfterUIRenderEvent& event){try{onFrame(event);}catch(...){std::lock_guard lock(mutex);stop("UI exception");}});
    }catch(...){removeInventoryActions();return false;}
    if(!uiListener){removeInventoryActions();return false;}
    availableFeatures.fetch_or((1u<<static_cast<unsigned>(Feature::InventorySort))|(1u<<static_cast<unsigned>(Feature::InventoryTransfer)));return true;
}
bool removeInventoryActions() noexcept {
    {std::lock_guard lock(mutex);abortAll();editors.clear();actionEdges={};faulted=false;resetRequested=false;consumedButton=0;strokeCancelRequested=false;}
    availableFeatures.fetch_and(~((1u<<static_cast<unsigned>(Feature::InventorySort))|(1u<<static_cast<unsigned>(Feature::InventoryTransfer))));
    bool ok=true;if(uiListener){if(ll::event::EventBus::getInstance().removeListener(uiListener))uiListener.reset();else ok=false;}
    for(auto it=std::rbegin(hooks);it!=std::rend(hooks);++it)if(it->installed){if(it->remove())it->installed=false;else ok=false;}return ok;
}
}
