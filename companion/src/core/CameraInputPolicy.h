#pragma once
#include <array>
#include <cstdint>
namespace praxis::companion {
inline constexpr std::array<int,8> cameraMovementKeys{'W','A','S','D',0x20,0x10,0xA0,0xA1};
inline bool cameraMovementKey(int key) noexcept {
    for(auto k:cameraMovementKeys)if(k==key)return true;return false;
}
inline bool cameraShiftKey(int key) noexcept {return key==0x10||key==0xA0||key==0xA1;}
// Down has no repeat flag in the official event. An observed Up or a trusted
// released physical state is required before another Down can be fresh.
class CameraInputLedger {
    std::array<bool,256> down_{},owned_{},inhibited_{},bodyDown_{},bodyPossible_{},releaseDebt_{};
public:
    struct Event {bool freshDown{},ownedRelease{},wasOwned{};};
    Event observe(int key,bool down,bool cancelled) noexcept {
        Event result{down&&!down_[key],!down&&owned_[key],owned_[key]};
        down_[key]=down;
        if(down&&cancelled)inhibited_[key]=true;
        if(!down&&cancelled&&!result.ownedRelease&&cameraShiftKey(key)) {
            bool possible=false;for(auto alias:{0x10,0xA0,0xA1})possible|=bodyPossible_[alias];
            if(possible){bodyPossible_[key]=true;releaseDebt_[key]=true;}
        }
        if(!down){
            owned_[key]=false;inhibited_[key]=false;
            if(!cancelled||result.ownedRelease){bodyDown_[key]=false;bodyPossible_[key]=false;}
        }
        if(!down&&!cancelled&&cameraShiftKey(key)) {
            // Raw generic/specific Shift polling can describe the same missed
            // Down. A formal Up retires only unobserved alias suspicions;
            // another explicitly held Shift keeps its own delivery ledger.
            releaseDebt_[key]=false;
            for(auto alias:{0x10,0xA0,0xA1})if(!releaseDebt_[alias])bodyPossible_[alias]=false;
        }
        return result;
    }
    void own(int key) noexcept {owned_[key]=true;}
    void passDown(int key) noexcept {if(cameraMovementKey(key))bodyDown_[key]=true;}
    void suspectBodyDown(int key) noexcept {if(cameraMovementKey(key))bodyPossible_[key]=true;}
    void inhibit(int key) noexcept {if(key>0&&key<256)inhibited_[key]=true;}
    void released(int key) noexcept {
        // Physical release cannot prove vanilla received an externally-cancelled Up.
        if(!bodyDown_[key]&&!bodyPossible_[key]&&!releaseDebt_[key]){down_[key]=owned_[key]=inhibited_[key]=false;}
    }
    bool down(int key) const noexcept {return key>0&&key<256&&down_[key];}
    bool owned(int key) const noexcept {return key>0&&key<256&&owned_[key];}
    bool inhibited(int key) const noexcept {return key>0&&key<256&&inhibited_[key];}
    bool neutral() const noexcept {
        for(auto k:cameraMovementKeys)if(down_[k]||owned_[k]||bodyDown_[k]||bodyPossible_[k]||releaseDebt_[k]||inhibited_[k])return false;
        return true;
    }
    bool anyOwned() const noexcept {for(auto v:owned_)if(v)return true;return false;}
    bool knownShift() const noexcept {
        for(auto key:{0x10,0xA0,0xA1})if(down_[key]||owned_[key]||inhibited_[key]||bodyDown_[key]||releaseDebt_[key])return true;
        return false;
    }
    std::array<int,3> movement() const noexcept {
        return {int(owned_['W'])-int(owned_['S']),int(owned_['D'])-int(owned_['A']),
            int(owned_[0x20])-int(owned_[0x10]||owned_[0xA0]||owned_[0xA1])};
    }
};
}
