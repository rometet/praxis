#pragma once
#include "resourcepacks/PackPlan.h"
#include "resourcepacks/PackDiagnostics.h"
#include "resourcepacks/ServerResourcePackAudit.h"
#include "resourcepacks/TitlePackTransaction.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace praxis::companion::packs {
inline std::uint64_t titlePackNow() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct TitlePackView {
    TitlePackPhase phase{TitlePackPhase::Idle};
    bool available{}, busy{}, inWorld{}, canReload{};
    std::uint64_t revision{}, until{}, token{};
    AppliedStackView current;
    PackApplyBlock block{PackApplyBlock::Unknown};
    PackApplyObservation observation;
    std::array<std::string,2> folders;
    std::string status{"適用の準備中です。現在状態の確認を待っています。"};
    // Copied server/world RP counts only. No names, IP, UUID, player IDs,
    // pack bytes, or server data leave the native owner.
    bool serverPackContext{},serverMultiplayer{},nativeGlobalIncluded{};
    bool r24AuthorizedClone{};
    std::size_t nativeLevelPackCount{},nativeFullPackCount{};
    ServerAssetLayer powderSnowSource{ServerAssetLayer::Unknown};
};
struct TitlePackRequest {
    std::uint64_t token{}, revision{};
    std::shared_ptr<Catalog const> catalog;
    Selection selection;
    bool reload{};
};
enum class TitleRequestBlock {None,Closed,Busy,Unavailable,UnknownCurrent,Expired,NoCatalog,ReloadUnavailable,Budget,StaleCatalog,UnsupportedPack};
struct TitleRequestResult {bool accepted{};TitleRequestBlock block{TitleRequestBlock::None};std::uint64_t token{};};
inline char const* titleRequestBlockJa(TitleRequestBlock reason) noexcept {
    switch(reason){
    case TitleRequestBlock::None:return "適用要求を受け付けました。進行と構成照合の結果を確認してください。";
    case TitleRequestBlock::Closed:return "要求を開始していません: 再読込機能は停止しています。";
    case TitleRequestBlock::Busy:return "要求を開始していません: 一覧読込または適用が進行中です。";
    case TitleRequestBlock::Unavailable:return "要求を開始していません: 適用条件が変わりました。下の状態を確認してください。";
    case TitleRequestBlock::UnknownCurrent:return "要求を開始していません: 現在の構成を確認できません。";
    case TitleRequestBlock::Expired:return "要求を開始していません: 現在状態の確認が古くなりました。";
    case TitleRequestBlock::ReloadUnavailable:return "要求を開始していません: 同じ構成の再読込はワールド内で行ってください。";
    case TitleRequestBlock::Budget:return "要求を開始していません: 候補の数が上限を超えています。";
    case TitleRequestBlock::UnsupportedPack:return "要求を開始していません: 非対応の候補が含まれています。";
    default:return "要求を開始していません: 一覧を更新し、候補を確認してください。";
    }
}
class TitlePackControl {
    std::mutex mutex;
    TitlePackView state;
    std::optional<TitlePackRequest> queued;
    std::uint64_t tokens{};
    std::uint64_t currentToken{}, cancelledToken{}, claimedAttempt{};
    bool closed{};
public:
    // Native Settings and custom UI share one monotonic operation namespace.
    // Reserving a native token does not alter a queued custom setter's claim.
    std::uint64_t reserveWorldOperationToken(){std::lock_guard lock(mutex);return closed?0:++tokens;}
    TitlePackView view(std::uint64_t now){
        std::lock_guard lock(mutex);auto value=state;
        if(closed||now>value.until){value.available=false;value.current={};value.block=closed?PackApplyBlock::Closed:PackApplyBlock::Expired;}
        return value;
    }
    void publish(TitlePackView value){
        std::lock_guard lock(mutex);
        if(closed){value.available=false;value.current={};}
        if(queued){value.token=queued->token;value.observation={};value.phase=TitlePackPhase::Queued;value.busy=true;value.available=false;value.status=titlePackStatusJa(value.phase);}
        state=std::move(value);
    }
    TitleRequestResult requestResult(std::shared_ptr<Catalog const> catalog,Selection const& selection,std::uint64_t now,bool reload=false){
        std::lock_guard lock(mutex);
        auto reject=[](TitleRequestBlock reason){return TitleRequestResult{false,reason,0};};
        if(closed)return reject(TitleRequestBlock::Closed);
        if(queued||state.busy)return reject(TitleRequestBlock::Busy);
        if(!state.available)return reject(TitleRequestBlock::Unavailable);
        if(!state.current.known)return reject(TitleRequestBlock::UnknownCurrent);
        if(now>state.until)return reject(TitleRequestBlock::Expired);
        if(reload&&!state.canReload)return reject(TitleRequestBlock::ReloadUnavailable);
        // Current-active world reload never consumes the edited proposal.
        // It still requires an unexpired, known current owner and available API.
        if(!(reload&&state.inWorld)){
            if(!catalog)return reject(TitleRequestBlock::NoCatalog);
            if(selection.order.size()>256)return reject(TitleRequestBlock::Budget);
            auto validation=checkApply(*catalog,selection,false);
            if(validation!=ApplyResult::Valid)return reject(validation==ApplyResult::UnsupportedPack?TitleRequestBlock::UnsupportedPack:TitleRequestBlock::StaleCatalog);
        }
        currentToken=++tokens;cancelledToken=claimedAttempt=0;
        queued=TitlePackRequest{currentToken,state.revision,std::move(catalog),reload&&state.inWorld?Selection{}:selection,reload};
        state.token=currentToken;state.phase=TitlePackPhase::Queued;state.busy=true;state.available=false;
        state.observation={};state.status=titlePackStatusJa(state.phase);
        return {true,TitleRequestBlock::None,currentToken};
    }
    bool request(std::shared_ptr<Catalog const> catalog,Selection const& selection,std::uint64_t now,bool reload=false){
        return requestResult(std::move(catalog),selection,now,reload).accepted;
    }
    std::optional<TitlePackRequest> take(){std::lock_guard lock(mutex);auto result=std::move(queued);queued.reset();return result;}
    bool cancel(std::uint64_t token){
        std::lock_guard lock(mutex);
        if(closed||!token||token!=currentToken||claimedAttempt)return false;
        cancelledToken=token;
        if(queued){queued.reset();state.phase=TitlePackPhase::Rejected;state.busy=false;state.status=titlePackStatusJa(state.phase);}
        return true;
    }
    bool canCancel(std::uint64_t token){std::lock_guard lock(mutex);return !closed&&token&&token==currentToken&&cancelledToken!=token&&!claimedAttempt;}
    bool cancelled(std::uint64_t token){std::lock_guard lock(mutex);return token&&cancelledToken==token;}
    // Linearization boundary immediately before the setter. Cancel/close that
    // wins this mutex prevents mutation; a previously claimed setter is counted
    // in-flight and retains its owners through nonblocking stop.
    bool claimSetter(std::uint64_t token,std::uint64_t attempt){
        std::lock_guard lock(mutex);
        if(closed||!token||token!=currentToken||cancelledToken==token||!attempt||attempt<=claimedAttempt)return false;
        claimedAttempt=attempt;return true;
    }
    void close(){std::lock_guard lock(mutex);closed=true;queued.reset();state.available=false;state.current={};}
    bool stopped(){std::lock_guard lock(mutex);return closed;}
};
// Contains copied CPU values only. The native backend pins the module before
// callbacks/worker startup; retain this queue through callbacks until exit.
inline TitlePackControl& titlePackControl(){static auto* value=new TitlePackControl;return *value;}
inline TitlePackView titlePackView(){return titlePackControl().view(titlePackNow());}
inline bool requestTitlePackApply(std::shared_ptr<Catalog const> catalog,Selection const& selection,bool reload=false){return titlePackControl().request(std::move(catalog),selection,titlePackNow(),reload);}
}
