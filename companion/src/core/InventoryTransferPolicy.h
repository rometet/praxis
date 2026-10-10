#pragma once
#include "core/InventorySortPolicy.h"
#include <set>
namespace praxis::companion {
struct InventoryTransferOptions {
    bool wheelOne=true,wheelStack=true,dragOne=true,dragStack=true;
    bool preserveHotbar=true,respectSortExclusions=true,reverseWheel=false;
    bool leaveOne=false,excludeInventoryGui=false,excludeStorageGui=false;
    int sameKey=0,allKey=0,cancelKey=0,maxSteps=128;
    unsigned sameModifiers=anyModifiers,allModifiers=anyModifiers,cancelModifiers=anyModifiers;
    bool operator==(InventoryTransferOptions const&)const=default;
};
inline bool validInventoryTransferOptions(InventoryTransferOptions const& o) noexcept {
    auto key=[](int k){return k>=0&&k<=255&&k!=0x79&&k!=0x7A;};
    return key(o.sameKey)&&key(o.allKey)&&key(o.cancelKey)&&o.maxSteps>=1&&o.maxSteps<=256
        &&validModifiers(o.sameModifiers)&&validModifiers(o.allModifiers)&&validModifiers(o.cancelModifiers);
}
namespace inventory {
enum class TransferGesture {None,WheelOne,WheelStack,DragOne,DragStack,SameType,All};
inline TransferGesture dragGesture(int button,bool shift,bool control) noexcept {
    if(button!=1)return TransferGesture::None;
    if(control)return TransferGesture::DragOne;
    return shift?TransferGesture::DragStack:TransferGesture::None;
}
inline bool transferEnabled(TransferGesture gesture,InventoryTransferOptions const& o) noexcept {
    switch(gesture){
    case TransferGesture::WheelOne:return o.wheelOne;
    case TransferGesture::WheelStack:return o.wheelStack;
    case TransferGesture::DragOne:return o.dragOne;
    case TransferGesture::DragStack:return o.dragStack;
    case TransferGesture::SameType:case TransferGesture::All:return true;
    default:return false;
    }
}
// Models paired input ownership and once-per-slot visits without native objects.
class TransferStroke {
public:
    bool press(int button,bool eligible,TransferGesture gesture) {
        consumed_=0;cancel();
        if(button!=1||!eligible||(gesture!=TransferGesture::DragOne&&gesture!=TransferGesture::DragStack))return false;
        consumed_=button;gesture_=gesture;return true;
    }
    bool release(int button) {if(button!=consumed_)return false;consumed_=0;cancel();return true;}
    void cancel(){gesture_=TransferGesture::None;visited_.clear();++revision_;}
    bool visit(int slot){return gesture_!=TransferGesture::None&&visited_.insert(slot).second;}
    TransferGesture gesture()const{return gesture_;}
    unsigned revision()const{return revision_;}
private:
    int consumed_{};TransferGesture gesture_{TransferGesture::None};unsigned revision_{};std::set<int> visited_;
};
struct TransferPlan {std::vector<MoveStep> steps;std::vector<StackValue> result;};
// Flattened indices 0..35 are player slots; 36..89 are ordinary storage.
inline std::optional<TransferPlan> planTransfer(std::vector<StackValue> const& player,std::vector<StackValue> const& storage,
    int hovered,TransferGesture gesture,int wheelDirection,InventoryTransferOptions const& o,InventorySortOptions const& exclusions) {
    if(player.size()!=36||storage.empty()||!validStacks(player)||!validStacks(storage)||!validInventoryTransferOptions(o)||!transferEnabled(gesture,o))return {};
    auto values=player;values.insert(values.end(),storage.begin(),storage.end());
    if(hovered<0||hovered>=static_cast<int>(values.size())||values[hovered].count<=0)return {};
    auto allowed=[&](int index){
        if(index<0||index>=static_cast<int>(values.size())||values[index].locked)return false;
        if(index<36)return !(o.preserveHotbar&&index<9)&&!(o.respectSortExclusions&&exclusions.excludedInventory[index]);
        return !(o.respectSortExclusions&&exclusions.excludedStorage[index-36]);
    };
    if(!allowed(hovered))return {};
    if(gesture==TransferGesture::SameType||gesture==TransferGesture::All){
        TransferPlan plan;bool const sourceStorage=hovered>=36;
        auto const reference=values[hovered].name;
        int const first=sourceStorage?36:0,last=sourceStorage?static_cast<int>(values.size()):36;
        int const dstFirst=sourceStorage?0:36,dstLast=sourceStorage?36:static_cast<int>(values.size());
        for(int source=first;source<last;++source){
            if(!allowed(source)||values[source].count<=0||(gesture==TransferGesture::SameType&&values[source].name!=reference))continue;
            int remaining=values[source].count-(o.leaveOne?1:0);
            for(bool empty:{false,true})for(int destination=dstFirst;destination<dstLast&&remaining>0;++destination){
                if(!allowed(destination))continue;
                auto const& to=values[destination];auto const& from=values[source];
                if(empty?to.count!=0:to.count<=0||to.key!=from.key||to.maximum!=from.maximum)continue;
                int const amount=std::min(remaining,from.maximum-to.count);if(amount<=0)continue;
                MoveStep step{source,destination,amount,false};if(!applyMove(values,step))return {};
                plan.steps.push_back(step);remaining-=amount;
                if(plan.steps.size()==static_cast<std::size_t>(o.maxSteps)){plan.result=std::move(values);return plan;}
            }
        }
        if(plan.steps.empty())return {};
        plan.result=std::move(values);return plan;
    }
    bool const wheel=gesture==TransferGesture::WheelOne||gesture==TransferGesture::WheelStack;
    bool const one=gesture==TransferGesture::WheelOne||gesture==TransferGesture::DragOne;
    if(wheel&&wheelDirection!=1&&wheelDirection!=-1)return {};
    bool const destinationStorage=wheel?(o.reverseWheel?wheelDirection<0:wheelDirection>0):hovered<36;
    bool const sourceStorage=!destinationStorage;
    int source=-1;
    if(!wheel||(one&&((hovered>=36)==sourceStorage)))source=hovered;
    else for(int i=static_cast<int>(values.size())-1;i>=0;--i)
        if(i!=hovered&&((i>=36)==sourceStorage)&&allowed(i)&&values[i].count>0&&values[i].key==values[hovered].key){source=i;break;}
    if(source<0&&((hovered>=36)==sourceStorage))source=hovered;
    if(source<0||!allowed(source)||values[source].count<=0||values[source].key!=values[hovered].key)return {};
    std::vector<int> destinations;
    if(wheel&&one&&((hovered>=36)==destinationStorage))destinations.push_back(hovered);
    else {
        int const first=destinationStorage?36:0,last=destinationStorage?static_cast<int>(values.size()):36;
        for(bool empty:{false,true})for(int i=first;i<last;++i) {
            if(!allowed(i))continue;
            auto const& v=values[i];
            if((empty&&v.count==0)||(!empty&&v.count>0&&v.key==values[source].key&&v.maximum==values[source].maximum&&v.count<v.maximum))destinations.push_back(i);
        }
    }
    TransferPlan plan;int remaining=one?1:values[source].count;
    for(int destination:destinations){
        if(!remaining||!allowed(destination))break;
        auto const& to=values[destination];
        if(to.count&&to.key!=values[source].key)continue;
        int const amount=std::min({remaining,values[source].count,values[source].maximum-to.count});
        if(amount<=0)continue;
        MoveStep step{source,destination,amount,false};
        if(!applyMove(values,step))return {};
        plan.steps.push_back(step);remaining-=amount;
        if(plan.steps.size()==static_cast<std::size_t>(o.maxSteps))break;
    }
    auto original=player;original.insert(original.end(),storage.begin(),storage.end());
    if(plan.steps.empty()||totals(values)!=totals(original))return {};
    plan.result=std::move(values);return plan;
}
}
}
