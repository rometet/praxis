#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include <utility>

namespace praxis::companion::packs {
enum class TitlePackPhase { Idle, Queued, Preparing, AwaitApply, Reloading, RollbackQueued, AwaitRollback, RollingBack, Complete, Restored, Rejected, RecoveryRequired };
enum class TitlePackAction { None, Apply, Rollback, NavigateRollback };
struct TitlePackOwner {
    std::uintptr_t game{}, manager{}, client{};
    std::uint64_t world{}, thread{};
    bool fresh{}, loadingFinished{}, pending{}, renderReady{};
    bool same(TitlePackOwner const& b) const noexcept {
        return game&&manager&&client&&thread&&game==b.game&&manager==b.manager&&client==b.client&&world==b.world&&thread==b.thread;
    }
    bool usable() const noexcept {return game&&manager&&client&&thread&&fresh&&!world&&loadingFinished&&!pending&&renderReady;}
};
struct TitlePackSettlement {
    TitlePackOwner owner;
    bool startScreen{}, callbacksIdle{}, otherStacksUnchanged{};
    std::vector<std::string> global;
};
// Production and fault fixtures use this same controller. The native adapter
// owns navigation, getters and the setter; this controller never owns objects.
class TitlePackTransaction {
public:
    TitlePackPhase phase{TitlePackPhase::Idle};
    TitlePackOwner owner;
    std::vector<std::string> oldGlobal, targetGlobal;
    std::uint64_t token{}, attempt{}, deadline{};
    bool started{}, startReturned{}, finished{}, restoring{}, rollbackNeeded{};
    unsigned stable{};
    std::vector<std::string> lastSettledGlobal;
    bool hasSettledGlobal{};
    static constexpr std::uint64_t CallbackBudget=30000, ReloadBudget=180000;
    bool busy() const noexcept {return phase>=TitlePackPhase::Queued&&phase<=TitlePackPhase::RollingBack;}
    bool begin(std::uint64_t id,TitlePackOwner const& context,std::uint64_t now) noexcept {
        if(busy()||!id||!context.usable())return false;
        token=id;owner=context;phase=TitlePackPhase::Preparing;deadline=now+CallbackBudget;return true;
    }
    void prepared(std::vector<std::string> oldIds,std::vector<std::string> newIds,std::uint64_t now){
        if(phase!=TitlePackPhase::Preparing)return;
        oldGlobal=std::move(oldIds);targetGlobal=std::move(newIds);
        phase=oldGlobal==targetGlobal?TitlePackPhase::Complete:TitlePackPhase::AwaitApply;
        deadline=now+CallbackBudget;
    }
    TitlePackAction callback(TitlePackOwner const& context,std::uint64_t now) noexcept {
        if(phase!=TitlePackPhase::AwaitApply&&phase!=TitlePackPhase::AwaitRollback)return TitlePackAction::None;
        // Initial admission already required loading/render readiness. Here
        // Minecraft's application callback owns serialization; its progress
        // screen may report loading=false before invoking the supplied action.
        if(now>deadline||!context.fresh||context.world||context.pending||!owner.same(context)){if(phase==TitlePackPhase::AwaitRollback)recovery();else reject();return TitlePackAction::None;}
        restoring=phase==TitlePackPhase::AwaitRollback;
        ++attempt;started=startReturned=finished=false;stable=0;hasSettledGlobal=false;lastSettledGlobal.clear();
        phase=restoring?TitlePackPhase::RollingBack:TitlePackPhase::Reloading;
        deadline=now+ReloadBudget;return restoring?TitlePackAction::Rollback:TitlePackAction::Apply;
    }
    void setterResult(bool accepted,bool unchangedOld) noexcept {
        if(phase!=TitlePackPhase::Reloading&&phase!=TitlePackPhase::RollingBack)return;
        if(accepted)return;
        if(restoring){recovery();return;}
        if(unchangedOld&&!started){reject();return;}
        rollbackNeeded=true; // Wait for the engine's completed normal UI cycle.
    }
    bool nativeStart(TitlePackOwner const& context,std::uint64_t cycle) noexcept {
        if((phase!=TitlePackPhase::Reloading&&phase!=TitlePackPhase::RollingBack)||!owner.same(context)||!context.fresh||cycle!=attempt||started){recovery();return false;}
        started=true;return true;
    }
    void nativeStartReturned(std::uint64_t cycle,bool normal) noexcept {
        if(cycle!=attempt||!started||!normal){recovery();return;}startReturned=true;
    }
    void nativeFinish(std::uint64_t cycle,bool normal) noexcept {
        if(cycle!=attempt||!started||finished||!normal){recovery();return;}finished=true;
    }
    TitlePackAction poll(TitlePackSettlement const& value,std::uint64_t now) noexcept {
        if(!busy())return TitlePackAction::None;
        if(!owner.same(value.owner)||!value.owner.fresh){recovery();return TitlePackAction::None;}
        if(now>deadline){
            if(phase==TitlePackPhase::Preparing||phase==TitlePackPhase::AwaitApply)reject();else recovery();
            return TitlePackAction::None;
        }
        if(phase!=TitlePackPhase::Reloading&&phase!=TitlePackPhase::RollingBack)return TitlePackAction::None;
        bool settled=started&&startReturned&&finished&&value.callbacksIdle&&value.startScreen&&value.owner.usable();
        if(!settled){stable=0;hasSettledGlobal=false;return TitlePackAction::None;}
        if(!value.otherStacksUnchanged){recovery();return TitlePackAction::None;}
        // Both success and rollback decisions need two consecutive observations
        // of the same exact identity sequence, not just two ready frames.
        if(!hasSettledGlobal||lastSettledGlobal!=value.global){
            stable=0;
            try{lastSettledGlobal=value.global;}catch(...){recovery();return TitlePackAction::None;}
            hasSettledGlobal=true;
        }
        if(++stable<2)return TitlePackAction::None;
        if(restoring){phase=value.global==oldGlobal?TitlePackPhase::Restored:TitlePackPhase::RecoveryRequired;return TitlePackAction::None;}
        if(!rollbackNeeded&&value.global==targetGlobal){phase=TitlePackPhase::Complete;return TitlePackAction::None;}
        if(value.global==oldGlobal){phase=TitlePackPhase::Restored;return TitlePackAction::None;}
        phase=TitlePackPhase::RollbackQueued;return TitlePackAction::NavigateRollback;
    }
    bool rollbackNavigation(std::uint64_t now) noexcept {
        if(phase!=TitlePackPhase::RollbackQueued)return false;
        phase=TitlePackPhase::AwaitRollback;deadline=now+CallbackBudget;return true;
    }
    bool cancel() noexcept {
        if(phase!=TitlePackPhase::Preparing&&phase!=TitlePackPhase::AwaitApply)return false;
        reject();return true;
    }
    void reject() noexcept {phase=TitlePackPhase::Rejected;}
    void recovery() noexcept {phase=TitlePackPhase::RecoveryRequired;}
};
template<class T,class Editable,class SameId>
std::optional<std::vector<T>> mergeTitleGlobal(std::span<T const> old,std::span<T const> selected,Editable&& editable,SameId&& sameId){
    std::vector<T> result;std::size_t next{};
    for(auto const& entry:old){
        if(editable(entry)){if(next<selected.size())result.push_back(selected[next++]);}
        else {for(auto const& chosen:selected)if(sameId(entry,chosen))return {};result.push_back(entry);}
    }
    while(next<selected.size())result.push_back(selected[next++]);return result;
}
inline char const* titlePackStatusJa(TitlePackPhase phase) noexcept {
    switch(phase){
    case TitlePackPhase::Queued:case TitlePackPhase::Preparing:return "適用候補を確認しています。";
    case TitlePackPhase::AwaitApply:return "ゲームの通常の適用画面を待っています。";
    case TitlePackPhase::Reloading:return "ゲームがリソースを読込中です。";
    case TitlePackPhase::RollbackQueued:case TitlePackPhase::AwaitRollback:case TitlePackPhase::RollingBack:return "通常の適用経路で旧構成に戻しています。";
    case TitlePackPhase::Complete:return "タイトルで Global 構成の一致を確認しました。ワールド内切替ではありません。";
    case TitlePackPhase::Restored:return "適用は失敗しました。旧 Global 構成の復帰を確認しました。";
    case TitlePackPhase::Rejected:return "変更開始前に拒否・取消しました。";
    case TitlePackPhase::RecoveryRequired:return "完了または安全な復帰を確認できません。再起動が必要です。";
    default:return "起動直後のタイトルで、一般 texture の Global 構成を適用できます。";
    }
}
}
