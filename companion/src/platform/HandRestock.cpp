#include "platform/HandRestock.h"
#include "platform/InventoryRequest.h"
#include "platform/InventorySnapshot.h"
#include "core/Settings.h"
#include "core/CameraInputPolicy.h"
#include "map/MapRuntime.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/models/ClientInstanceScreenModel.h"
#include "mc/world/containers/managers/controllers/HudContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/inventory/network/ItemStackNetManagerClient.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionManager.h"
#include <Windows.h>
#include <memory>
#include <mutex>

namespace praxis::companion {
namespace {
using namespace inventory;
std::recursive_mutex mutex;
std::weak_ptr<HudContainerManagerController> hud;
struct Job {
    HandSnapshot before,after,submittedState;
    HandRestockOptions options;
    std::weak_ptr<HudContainerManagerController> controller;
    std::optional<RestockStep> step;
    std::uint64_t started{},stableSince{},token{};
    int target=-1;
    bool submitted{},manual{};
};
std::shared_ptr<Job> pending;
bool faulted{};
ll::event::ListenerPtr uiListener;
std::atomic_bool resetRequested{};
thread_local bool insideUse{};
std::optional<HandSnapshot> remembered;
HandRestockOptions rememberedOptions;
std::uint64_t rememberedAt{};
std::uint32_t diagnosticStages{};
std::atomic_int operationStatus{}; // Last native operation, readable by the GUI without taking its lock.
int observedSwapKey{};unsigned observedSwapModifiers{};bool swapHeld{};
void cancel() {
    if(pending){if(pending->submitted&&operationStatus==1)operationStatus=6;releaseRequest(pending->token);}
    pending.reset();
}
void clearObservation() {remembered.reset();rememberedAt=0;}
void trace(unsigned stage,char const* reason) {
    auto const bit=std::uint32_t{1}<<stage;
    if(diagnosticStages&bit)return;
    diagnosticStages|=bit;
    auto const message=std::string("HandRestock: ")+reason;logGuiInfo(message.c_str());
}
void observe(HandSnapshot const& now,HandRestockOptions const& options,std::uint64_t time) {
    // Keep the last live identity briefly across a single consumption. The
    // completion hook may run after vanilla has cleared the selected stack.
    // Observation alone never authorizes a move; the use callback still does.
    if(remembered&&rememberedOptions==options&&time>=rememberedAt&&time-rememberedAt<=250
        &&planRestock(*remembered,now,options))return;
    remembered=now;rememberedOptions=options;rememberedAt=time;
}
LocalPlayer* eligible(Player const& player) {
    auto client=ll::service::getClientInstance();auto local=client?client->getLocalPlayer():nullptr;
    return local==&player&&settings()->on(Feature::HandRestock)&&inventoryFocus(true)&&!faulted?local:nullptr;
}
bool localIdentity(Player const& player) {
    auto client=ll::service::getClientInstance();return client&&client->getLocalPlayer()==&player;
}
std::shared_ptr<Job> beforeUse(Player& player,HandSlot hand=HandSlot::Mainhand,bool completed=false) {
    std::lock_guard lock(mutex);
    if(resetRequested.exchange(false)){cancel();clearObservation();diagnosticStages=0;faulted=false;}
    if(insideUse||(pending&&pending->submitted)||(hand!=HandSlot::Mainhand&&hand!=HandSlot::Offhand))return {};
    auto local=eligible(player);auto controller=hud.lock();
    if(!local)return {};
    if(!controller){trace(0,"use capture has no live HUD controller");return {};}
    auto op=std::make_shared<Job>();op->before=handSnapshot(*local);op->options=settings()->handRestock;
    op->target=hand==HandSlot::Offhand?36:op->before.selected;
    if(op->target==36&&!op->options.offhand)return {};
    auto const time=GetTickCount64();
    if(completed&&remembered&&rememberedOptions==op->options&&time>=rememberedAt&&time-rememberedAt<=250
        &&planRestock(*remembered,op->before,op->options,op->target)){
        op->before=*remembered;trace(1,"completed use retained the pre-consumption identity");
    }
    if(op->before.selected<0||op->before.selected>=9)return {};
    auto const& stack=op->before.slots[op->target];
    if((op->target<36&&op->options.excludedInventory[op->target])
        ||std::find(op->options.excludedItems.begin(),op->options.excludedItems.end(),stack.name)!=op->options.excludedItems.end())return {};
    if(stack.count<=0||stack.damageable||stack.locked)return {};
    // A later exact use may replace an unsent observation. This preserves the
    // last single decrement in a continuous placement/eating sequence rather
    // than treating two uses as one large consumption. Empty-hand attempts
    // above leave the final depleted observation intact for its quiet period.
    if(pending){if(op->before!=pending->after)return {};cancel();}
    observe(op->before,op->options,time);
    op->controller=controller;op->started=time;return op;
}
void afterUse(Player& player,std::shared_ptr<Job> const& op,bool success) {
    if(!op||!success)return;
    std::lock_guard lock(mutex);
    auto local=eligible(player);if(!local||pending)return;
    auto now=handSnapshot(*local);
    op->after=now;op->step=planRestock(op->before,now,op->options,op->target);
    // No immediate change can still be an asynchronous successful use. All
    // other changes already contradict consumption and are discarded.
    if(!op->step&&now!=op->before){trace(2,"use changed inventory but no exact refill with an allowed spare was eligible");return;}
    op->stableSince=GetTickCount64();pending=op;
    if(op->step)trace(3,"exact consumption captured; waiting for the quiet period");
}
struct CaptureScope { bool active; explicit CaptureScope(bool value):active(value){if(active)insideUse=true;} ~CaptureScope(){if(active)insideUse=false;} };
void applyStep(HandSnapshot& state,RestockStep const& step) {
    if(step.swap)std::swap(state.slots[step.source],state.slots[step.target]);
    else {
        auto& from=state.slots[step.source];auto& to=state.slots[step.target];int const count=to.count;
        if(!count)to=from;
        to.count=count+step.amount;from.count-=step.amount;if(!from.count)from={};
    }
}
// Observe vanilla-generated actions only. No action or packet is manufactured.
struct MainMoveAudit {
    InventoryTransactionManager* owner;
    HandSnapshot const& before;
    HandSnapshot const& after;
    RestockStep step;
    unsigned seen{};
    bool valid=true;
};
thread_local MainMoveAudit* mainMoveAudit{};
void observeMainAction(InventoryTransactionManager& manager,InventoryAction const& action) noexcept {
    auto audit=mainMoveAudit;if(!audit||audit->owner!=&manager)return;
    try {
        auto const& source=action.mSource.get();int const slot=static_cast<int>(action.mSlot);
        unsigned const bit=slot==audit->step.source?1:slot==audit->step.target?2:0;
        if(source.mType!=InventorySourceType::ContainerInventory||source.mContainerId!=ContainerID::Inventory
            ||source.mFlags!=InventorySource::InventorySourceFlags::NoFlag||!bit||(audit->seen&bit)
            ||stackValue(action.mFromItem.get())!=audit->before.slots[slot]
            ||stackValue(action.mToItem.get())!=audit->after.slots[slot])audit->valid=false;
        else audit->seen|=bit;
    }catch(...){audit->valid=false;}
}
bool mainInventoryCompatible(LocalPlayer& player,HandSnapshot const& before,RestockStep const& step) {
    if(step.source<0||step.source>=36||step.target<0||step.target>=36||step.source==step.target)return false;
    auto& inventory=player.getInventory();
    auto const& from=inventory.getItem(step.source);auto const& to=inventory.getItem(step.target);
    if(stackValue(from)!=before.slots[step.source]||stackValue(to)!=before.slots[step.target])return false;
    if(step.swap)return true;
    int const count=before.slots[step.source].count,targetCount=before.slots[step.target].count;
    int const fromLimit=from.getMaxStackSize();
    if(step.amount<=0||step.amount>count||fromLimit<=0||count>fromLimit||targetCount<0
        ||step.amount>fromLimit-targetCount)return false;
    if(!targetCount)return true;
    int const toLimit=to.getMaxStackSize();
    // Snapshot identity is a planner input. Vanilla's native predicate owns
    // merge compatibility, including metadata outside that snapshot.
    return toLimit>0&&targetCount<=toLimit&&step.amount<=toLimit-targetCount&&to.isStackable(from);
}
bool moveMainInventory(LocalPlayer& player,HandSnapshot const& before,HandSnapshot const& after,RestockStep const& step) {
    if(mainMoveAudit||step.source<0||step.source>=36||step.target<0||step.target>=36||step.source==step.target)return false;
    auto& transactions=player.mTransactionManager.get();auto base=player.mItemStackNetManager.get();
    if(transactions.mCurrentTransaction.get()||!base||!base->mIsEnabled||!base->mIsClientSide
        ||base->mLegacyTransactionRequestId->mRawId||static_cast<ItemStackNetManagerClient*>(base)->mRequest)return false;
    auto& inventory=player.getInventory();
    if(!mainInventoryCompatible(player,before,step))return false;
    ItemStack from=inventory.getItem(step.source),to=inventory.getItem(step.target);
    if(step.swap)std::swap(from,to);
    else {
        if(step.amount<=0||step.amount>before.slots[step.source].count)return false;
        if(!before.slots[step.target].count){to=from;to.remove(before.slots[step.source].count-step.amount);}
        else to.add(step.amount);
        from.remove(step.amount);
    }
    if(stackValue(from)!=after.slots[step.source]||stackValue(to)!=after.slots[step.target])return false;
    MainMoveAudit audit{&transactions,before,after,step};
    struct AuditScope {
        explicit AuditScope(MainMoveAudit& value){mainMoveAudit=&value;}
        ~AuditScope(){mainMoveAudit=nullptr;}
    } auditScope{audit};
    {
        auto requestScope=ItemStackNetManagerBase::_tryBeginClientLegacyTransactionRequest(&player);
        if(!base->mLegacyTransactionRequestId->mRawId||!audit.valid||audit.seen)return false;
        // These normal SDK setters run inside vanilla's request scope. Vanilla
        // records both actions and owns prediction, dispatch and server rollback.
        inventory.$setItem(step.source,from);inventory.$setItem(step.target,to);
        if(!audit.valid||audit.seen!=3)return false;
        if(transactions.mCurrentTransaction.get())player.updateInventoryTransactions();
        if(transactions.mCurrentTransaction.get())return false;
    }
    return true;
}
LL_TYPE_INSTANCE_HOOK(RestockHudCapture,ll::memory::HookPriority::Normal,ClientInstanceScreenModel,
    &ClientInstanceScreenModel::createHudContainerManagerController,std::shared_ptr<HudContainerManagerController>) {
    auto controller=origin();std::lock_guard lock(mutex);cancel();clearObservation();diagnosticStages=0;hud=controller;faulted=false;return controller;
}
LL_TYPE_INSTANCE_HOOK(RestockNativeAction,ll::memory::HookPriority::Normal,InventoryTransactionManager,
    &InventoryTransactionManager::addAction,void,InventoryAction const& action,bool forceBalanced) {
    observeMainAction(*this,action);origin(action,forceBalanced);
}
LL_TYPE_INSTANCE_HOOK(RestockUse,ll::memory::HookPriority::Normal,GameMode,
    &GameMode::$useItem,bool,ItemStack& item,HandSlot hand) {
    std::shared_ptr<Job> op;try{op=beforeUse(mPlayer,hand);}catch(...){}
    bool result;{CaptureScope scope{bool(op)};result=origin(item,hand);}
    try{afterUse(mPlayer,op,result);}catch(...){resetHandRestock();}return result;
}
LL_TYPE_INSTANCE_HOOK(RestockUseOn,ll::memory::HookPriority::Normal,GameMode,
    &GameMode::$useItemOn,InteractionResult,ItemStack& item,BlockPos const& pos,uchar face,
    Vec3 const& hit,HandSlot hand,Block const* target,bool first) {
    std::shared_ptr<Job> op;try{op=beforeUse(mPlayer,hand);}catch(...){}
    InteractionResult result;{CaptureScope scope{bool(op)};result=origin(item,pos,face,hit,hand,target,first);}
    try{afterUse(mPlayer,op,result.mSuccess);}catch(...){resetHandRestock();}return result;
}
LL_TYPE_INSTANCE_HOOK(RestockCompletedUse,ll::memory::HookPriority::Normal,Player,
    &Player::completeUsingItem,void) {
    std::shared_ptr<Job> op;try{op=beforeUse(*this,HandSlot::Mainhand,true);}catch(...){}
    {CaptureScope scope{bool(op)};origin();}
    try{afterUse(*this,op,true);}catch(...){resetHandRestock();}
}
LL_TYPE_INSTANCE_HOOK(RestockDropCancel,ll::memory::HookPriority::Normal,Player,
    &Player::$drop,bool,ItemStack const& item,bool const randomly) {
    {std::lock_guard lock(mutex);auto client=ll::service::getClientInstance();
     if(localIdentity(*this)){cancel();clearObservation();}}
    return origin(item,randomly);
}
struct Hook {int(*add)();bool(*remove)();bool installed{};};
template<class T>Hook hook(){return{[]{return T::hook();},[]{return T::unhook();},false};}
Hook hooks[]{hook<RestockHudCapture>(),hook<RestockNativeAction>(),hook<RestockUse>(),hook<RestockUseOn>(),hook<RestockCompletedUse>(),hook<RestockDropCancel>()};
}
void tickHandRestock(LocalPlayer& player) noexcept {
    std::lock_guard lock(mutex);
    if(resetRequested.exchange(false)){cancel();clearObservation();diagnosticStages=0;faulted=false;}
    try {
        auto s=settings();
        auto const slot=static_cast<unsigned>(Feature::OffhandSwap);
        HotkeyChord const chord{unsigned(s->keys[slot]),s->keyModifiers[slot],1};
        bool const down=chord.key>0&&(GetAsyncKeyState(chord.key)&0x8000)!=0;
        bool const edge=observedSwapKey==int(chord.key)&&observedSwapModifiers==chord.modifiers&&down&&!swapHeld;
        observedSwapKey=int(chord.key);observedSwapModifiers=chord.modifiers;swapHeld=down;
        bool conflict=cameraMovementKey(int(chord.key))||chord.key==VK_F10||chord.key==VK_F11;
        for(unsigned i=0;i<featureCount;++i)if(i!=slot&&(ownedFeatureSlotMask&featureRegistryBit(i))
            &&hotkeyConflict(chord,{unsigned(s->keys[i]),s->keyModifiers[i],1}))conflict=true;
        if(hotkeyConflict(chord,{unsigned(s->zoomActionKey),s->zoomActionModifiers,1})
            ||hotkeyConflict(chord,{unsigned(s->mapToggleActionKey),s->mapToggleActionModifiers,1}))conflict=true;
        for(int key:mapActionBindings())if(hotkeyConflict(chord,{unsigned(key),anyModifiers,1}))conflict=true;
        auto const modifiers=modifierBits((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,(GetAsyncKeyState(VK_MENU)&0x8000)!=0);
        if(!inventoryFocus(true)||(!s->on(Feature::HandRestock)&&!s->on(Feature::OffhandSwap))){cancel();clearObservation();return;}
        if(edge&&s->on(Feature::OffhandSwap)&&!conflict&&modifiersMatch(chord.modifiers,modifiers)){
            clearObservation();if(!pending||!pending->submitted){
            cancel();faulted=false;
            auto next=std::make_shared<Job>();next->before=handSnapshot(player);next->after=next->before;
            next->options=s->handRestock;next->step=planOffhandSwap(next->before);next->target=36;
            next->controller=hud;next->started=GetTickCount64();next->stableSince=next->started;next->manual=true;
            if(next->step)pending=next;
            }
        }
        if(faulted){cancel();clearObservation();return;}
        auto const time=GetTickCount64();
        if(s->on(Feature::HandRestock)&&!pending&&remembered&&time>=rememberedAt&&time-rememberedAt<16)return;
        auto now=handSnapshot(player);
        if(s->on(Feature::HandRestock)&&(!pending||!pending->manual))observe(now,s->handRestock,time);
        else clearObservation();
        if(!pending)return;
        auto op=pending;auto controller=op->controller.lock();auto active=hud.lock();
        auto manager=controller;
        if(!controller||active!=controller||!manager||manager->mContainersClosed||s->handRestock!=op->options
            ||!s->on(op->manual?Feature::OffhandSwap:Feature::HandRestock)){cancel();return;}
        if(op->submitted){
            auto result=requestResult(op->token);
            if(result==RequestResult::Waiting){
                if(now!=op->submittedState){operationStatus=6;cancel();clearObservation();}
                return;
            }
            if(result!=RequestResult::Accepted||now!=op->after){operationStatus=2;faulted=true;logGuiInfo("HandRestock stopped: inventory request failed or state changed");}
            else {operationStatus=3;trace(4,"native refill request accepted and inventory matched");}
            cancel();return;
        }
        if(time<op->started||time-op->started>static_cast<std::uint64_t>(op->options.quietMs+1500)
            ||(!op->step&&time-op->started>1000)){cancel();return;}
        if(!op->step){op->step=planRestock(op->before,now,op->options,op->target);if(!op->step){if(now!=op->before)cancel();return;}op->after=now;op->stableSince=time;}
        if(now!=op->after){cancel();return;}
        if(!op->manual&&(time<op->stableSince||time-op->stableSince<static_cast<std::uint64_t>(op->options.quietMs)))return;
        // Never append to a stale HUD/model or a conflicting vanilla request.
        auto model=manager->mContainerManagerModel.lock();
        if(!model){trace(5,"HUD container model expired");cancel();return;}
        if(&model->mPlayer!=&player){trace(12,"HUD container model belongs to another player");cancel();return;}
        bool const combined=manager->hasContainerController("hotbar_items")&&manager->getContainerSize("hotbar_items")==36;
        int const size=manager->hasContainerController("inventory_items")?manager->getContainerSize("inventory_items"):0;
        bool const split=!combined&&size==27;
        if(!combined&&size!=36&&(!split||!manager->hasContainerController("hotbar_items")||manager->getContainerSize("hotbar_items")!=9)){
            trace(6,"HUD inventory collection layout unsupported");cancel();return;
        }
        // The HUD exposes all 36 inventory slots as hotbar_items. Retain the
        // already supported combined inventory_items and 27 + 9 layouts too.
        // Validate every alias against the real inventory before using it.
        auto address=[&](int slot){return std::pair<char const*,int>{slot==36?"offhand_items":combined||(split&&slot<9)?"hotbar_items":"inventory_items",slot==36?0:slot-(split&&slot>=9?9:0)};};
        for(int i=0;i<36;++i){auto const slot=address(i);
            if(stackValue(manager->getItemStack(slot.first,slot.second))!=now.slots[i]){
                trace(7,"HUD collection contents did not match the live inventory");cancel();return;
            }
        }
        auto const step=*op->step;
        if(step.target==36||step.source==36){
            if(!manager->hasContainerController("offhand_items")||manager->getContainerSize("offhand_items")!=1
                ||stackValue(manager->getItemStack("offhand_items",0))!=now.slots[36]){
                operationStatus=4;trace(11,"native offhand collection missing or mismatched; operation unsupported");cancel();return;
            }
        }
        if(step.source<36&&step.target<36&&!mainInventoryCompatible(player,op->after,step)){
            operationStatus=2;trace(13,"native stacks cannot merge within their limits; refill declined");cancel();return;
        }
        op->token=beginRequest(*manager);if(!op->token){operationStatus=5;trace(8,"native inventory request path busy or unavailable");cancel();return;}
        auto expected=op->after;applyStep(expected,step);
        bool success=false;
        try {
            if(step.source<36&&step.target<36)success=moveMainInventory(player,op->after,expected,step);
            else {
                auto const sourceAddress=address(step.source),targetAddress=address(step.target);
                SlotData const source(sourceAddress.first,sourceAddress.second),target(targetAddress.first,targetAddress.second);
                success=step.swap?manager->handleSwap(source,target):manager->handlePlaceAmount(source,step.amount,target);
            }
        }catch(...){finishRequest(*manager,op->token);throw;}
        finishRequest(*manager,op->token);
        if(pending!=op)return;
        if(!success){operationStatus=2;trace(9,"native inventory operation declined or did not record both normal actions");faulted=true;cancel();return;}
        operationStatus=1;
        trace(10,"native refill submitted; waiting for server acknowledgement");
        op->after=std::move(expected);
        op->submitted=true;
        op->submittedState=handSnapshot(player);
    } catch(...) {operationStatus=2;faulted=true;cancel();logGuiInfo("HandRestock stopped after an inventory exception");}
}
void resetHandRestock() noexcept {std::lock_guard lock(mutex);cancel();clearObservation();diagnosticStages=0;faulted=false;observedSwapKey=0;observedSwapModifiers=0;swapHeld=false;}
void requestHandRestockReset() noexcept {resetRequested=true;}
void serviceHandRestock() noexcept {
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;
    try {
        // Runs even when no HUD UI callback is rendered. GUI interruption must
        // retire a remembered consumption before the player returns to the HUD.
        if(!inventoryFocus(true)||(pending&&pending->before.generation!=worldGeneration.load())
            ||(remembered&&remembered->generation!=worldGeneration.load())){
            cancel();clearObservation();observedSwapKey=0;swapHeld=false;return;
        }
        if(pending&&pending->submitted){auto result=requestResult(pending->token);
            if(result==RequestResult::Waiting){auto client=ll::service::getClientInstance();auto player=client?client->getLocalPlayer():nullptr;
                if(!player||handSnapshot(*player)!=pending->submittedState){operationStatus=6;cancel();clearObservation();return;}}
            if(result!=RequestResult::Waiting&&result!=RequestResult::Accepted){operationStatus=2;faulted=true;cancel();clearObservation();}
        }
    }catch(...){operationStatus=2;faulted=true;cancel();clearObservation();}
}
char const* handRestockStatus() noexcept {
    constexpr char const* messages[]{"未実行","サーバー応答待ち","拒否・応答失敗・内容変更で停止",
        "native操作のACKと内容が一致","未対応: nativeのoffhand_itemsスロットを確認できません",
        "要求経路が使用中または未対応","取消（送信済み要求の応答は保持）"};
    auto const value=operationStatus.load();return value>=0&&value<int(std::size(messages))?messages[value]:messages[0];
}
bool installHandRestock() {
    for(auto& h:hooks)if(!h.installed){if(h.add()!=0){removeHandRestock();return false;}h.installed=true;}
    try {
        if(!uiListener)uiListener=ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent& event) {
                auto const name=event.screenView().getScreenName();
                if(name!="hud_screen"&&name!="in_game_play_screen")return;
                if(auto player=event.uiRenderContext().mClient.getLocalPlayer())tickHandRestock(*player);
            });
    } catch(...) {removeHandRestock();return false;}
    if(!uiListener){removeHandRestock();return false;}
    availableFeatures.fetch_or(featureRegistryBit(static_cast<unsigned>(Feature::HandRestock))|featureRegistryBit(static_cast<unsigned>(Feature::OffhandSwap)));return true;
}
bool removeHandRestock() noexcept {
    resetHandRestock();availableFeatures.fetch_and(~(featureRegistryBit(static_cast<unsigned>(Feature::HandRestock))|featureRegistryBit(static_cast<unsigned>(Feature::OffhandSwap))));
    bool ok=true;
    if(uiListener){if(ll::event::EventBus::getInstance().removeListener(uiListener))uiListener.reset();else ok=false;}
    for(auto it=std::rbegin(hooks);it!=std::rend(hooks);++it)if(it->installed){if(it->remove())it->installed=false;else ok=false;}
    if(ok){std::lock_guard lock(mutex);hud.reset();}return ok;
}
}
