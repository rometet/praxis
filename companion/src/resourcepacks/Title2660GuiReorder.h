#pragma once
#include "resourcepacks/TitlePackControl.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace praxis::companion::packs {
inline bool sameTitle2660Pack(AppliedPack const& a,AppliedPack const& b) noexcept {
    return a.uuid==b.uuid&&a.version==b.version&&
           a.subpack==b.subpack&&a.packType==b.packType;
}
inline bool sameTitle2660Stack(std::vector<AppliedPack> const& a,
                              std::vector<AppliedPack> const& b) noexcept {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),
        [](auto const& x,auto const& y){return sameTitle2660Pack(x,y);});
}
struct Title2660GuiReorderRequest {
    std::uint64_t token{},revision{},deadline{};
    std::vector<AppliedPack> original,target;
};
enum class Title2660GuiReorderState {
    Idle, Queued, Validating, Applying, Applied, Restoring,
    Restored, Rejected, RecoveryRequired
};
struct Title2660GuiReorderView {
    Title2660GuiReorderState state{Title2660GuiReorderState::Idle};
    std::uint64_t token{};
    std::string status;
    bool busy{};
};
class Title2660GuiReorder {
    mutable std::mutex mutex_;
    std::optional<Title2660GuiReorderRequest> queued_;
    Title2660GuiReorderView latest_;
    std::uint64_t counter_{};

public:
    // In a title screen the user explicitly presses Apply. No automatic
    // application, enabling or removal, and no new pack registration.
    bool request(TitlePackView const& owner,std::vector<AppliedPack> proposed,
                 std::uint64_t now) {
        std::lock_guard lock(mutex_);
        if(queued_||latest_.busy || latest_.state==Title2660GuiReorderState::RecoveryRequired || !owner.nativeReorderOnly ||
           !owner.available || owner.busy || !owner.current.known ||
           now>owner.until || !owner.revision ||
           owner.current.order.size()<2 || owner.current.order.size()>512 ||
           owner.current.order.size()!=proposed.size()) return false;
        if(sameTitle2660Stack(owner.current.order,proposed))return false;
        // A permutation of the current native stack only. If any identity
        // appears in different multiplicity, refuse the request entirely.
        std::vector<bool> used(proposed.size());
        for(auto const& old:owner.current.order){
            bool found=false;
            for(std::size_t i=0;i<proposed.size();++i){
                if(!used[i]&&sameTitle2660Pack(old,proposed[i])){
                    used[i]=true;found=true;break;
                }
            }
            if(!found)return false;
        }
        auto token=++counter_;
        queued_=Title2660GuiReorderRequest{
            token,owner.revision,now+30000,
            owner.current.order,std::move(proposed)};
        latest_={Title2660GuiReorderState::Queued,token,
                 "現在のパックを照合しています。ゲームの構成はまだ変更していません。",true};
        return true;
    }
    std::optional<Title2660GuiReorderRequest> take() {
        std::lock_guard lock(mutex_);
        auto result=std::move(queued_);
        queued_.reset();
        if(result&&result->token==latest_.token){
            latest_.state=Title2660GuiReorderState::Validating;
            latest_.status="ゲーム側のパック構成を再確認しています。";
        }
        return result;
    }
    bool isBusy() const {std::lock_guard lock(mutex_);return latest_.busy;}
    bool needsRecovery() const {std::lock_guard lock(mutex_);return latest_.state==Title2660GuiReorderState::RecoveryRequired;}
    Title2660GuiReorderView view() const {
        std::lock_guard lock(mutex_);
        return latest_;
    }
    void report(std::uint64_t token,Title2660GuiReorderState state,std::string message){
        std::lock_guard lock(mutex_);
        if(!token||token!=latest_.token)return;
        latest_.state=state;
        latest_.busy=state==Title2660GuiReorderState::Queued||
                     state==Title2660GuiReorderState::Validating||
                     state==Title2660GuiReorderState::Applying||
                     state==Title2660GuiReorderState::Restoring;
        latest_.status=std::move(message);
    }
    bool cancelBeforeNative(std::uint64_t token) {
        std::lock_guard lock(mutex_);
        if(!queued_||!token||queued_->token!=token)return false;
        queued_.reset();
        latest_.state=Title2660GuiReorderState::Rejected;
        latest_.status="開始前に取り消しました。パック構成は変更していません。";
        latest_.busy=false;
        return true;
    }
};
inline Title2660GuiReorder& title2660GuiReorder() {
    static auto* value=new Title2660GuiReorder;
    return *value;
}
} // namespace praxis::companion::packs
