#pragma once
#include "resourcepacks/LocalTerrainAtlasProof.h"
#include <cstdint>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
namespace praxis::companion::packs {
enum class NativeAtlasApplyPhase:unsigned {
    Idle,QueuedOn,QueuedOff,RebuildingOn,RebuildingOff,WaitingFrame,
    Enabled,Restored,Failed
};
struct NativeAtlasApplyRequest {
    std::uint64_t token{},stageToken{},world{};
    bool enable{};
};
struct NativeAtlasApplyView {
    NativeAtlasApplyPhase phase{NativeAtlasApplyPhase::Idle};
    std::uint64_t token{},world{};
    unsigned sourceHits{},statusCallbacks{},finished{},renderFrames{};
    bool enableRequested{},modelReady{},nativeIssued{},renderVerified{};
    std::string status{"未適用です。"};
    bool busy() const noexcept {
        return phase==NativeAtlasApplyPhase::QueuedOn||phase==NativeAtlasApplyPhase::QueuedOff
          ||phase==NativeAtlasApplyPhase::RebuildingOn||phase==NativeAtlasApplyPhase::RebuildingOff
          ||phase==NativeAtlasApplyPhase::WaitingFrame;
    }
};
class LocalNativeAtlasApplyControl {
    mutable std::mutex mx;
    NativeAtlasApplyView state;
    std::optional<NativeAtlasApplyRequest> pending;
    std::uint64_t next{};
    std::chrono::steady_clock::time_point nativeIssuedAt{};
public:
    bool request(bool enable,LocalTerrainAtlasProof const& proof,std::uint64_t stageToken) {
        std::lock_guard lock(mx);
        if(state.busy()||pending||!proof.mappingObserved()||!terrainUvGeometryValid(proof)||!proof.world
            ||!stageToken||proof.stagedToken!=stageToken)return false;
        if(enable&&(state.phase!=NativeAtlasApplyPhase::Idle&&state.phase!=NativeAtlasApplyPhase::Restored))return false;
        if(!enable&&state.phase!=NativeAtlasApplyPhase::Enabled)return false;
        pending=NativeAtlasApplyRequest{++next,stageToken,proof.world,enable};
        state={};
        state.token=next;state.world=proof.world;state.enableRequested=enable;state.modelReady=true;
        state.phase=enable?NativeAtlasApplyPhase::QueuedOn:NativeAtlasApplyPhase::QueuedOff;
        state.status=enable?"粉雪ローカル表示を再構築待ちです。":"元の粉雪表示への復元待ちです。";
        return true;
    }
    std::optional<NativeAtlasApplyRequest> take(){
        std::lock_guard lock(mx);
        auto req=std::move(pending);pending.reset();
        if(req)state.phase=req->enable?NativeAtlasApplyPhase::RebuildingOn:NativeAtlasApplyPhase::RebuildingOff;
        return req;
    }
    void begun(std::uint64_t token,bool ok,std::string error={}){
        std::lock_guard lock(mx);
        if(state.token!=token)return;
        state.nativeIssued=ok;
        if(!ok){state.phase=NativeAtlasApplyPhase::Failed;state.status=std::move(error);}
        else {
            nativeIssuedAt=std::chrono::steady_clock::now();
            state.status="Native terrain atlas再生成の完了通知を待っています。";
        }
    }
    void statusCallback(std::uint64_t token) noexcept{
        std::lock_guard lock(mx);
        if(state.token==token)++state.statusCallbacks;
    }
    void finished(std::uint64_t token) noexcept{
        std::lock_guard lock(mx);
        if(state.token==token&&state.nativeIssued
            &&(state.phase==NativeAtlasApplyPhase::RebuildingOn
                ||state.phase==NativeAtlasApplyPhase::RebuildingOff)){
            ++state.finished;state.phase=NativeAtlasApplyPhase::WaitingFrame;
            state.status="Minecraftのアトラス再生成通知を受信。次の描画フレームを待機中。";
        }
    }
    void frame(std::uint64_t token,unsigned sourceHits) noexcept{
        std::lock_guard lock(mx);
        if(state.token!=token||state.phase!=NativeAtlasApplyPhase::WaitingFrame)return;
        state.sourceHits=sourceHits;
        ++state.renderFrames;state.renderVerified=true;
        if(state.enableRequested&&!sourceHits){
            state.phase=NativeAtlasApplyPhase::Failed;
            state.status="Minecraftから候補画像の取得要求がありません。表示反映は確認できません。再起動して確認してください。";
        }else{
            state.phase=state.enableRequested?NativeAtlasApplyPhase::Enabled:NativeAtlasApplyPhase::Restored;
            state.status=state.enableRequested
                ?"Native再生成通知と描画フレームを確認。粉雪の見た目を目視確認してください。"
                :"Native再生成通知と描画フレームを確認。元の見た目を目視確認してください。";
        }
    }
    bool failIfStalled(std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()) noexcept {
        std::lock_guard lock(mx);
        if(!state.nativeIssued || !state.busy())return false;
        if(now-nativeIssuedAt < std::chrono::seconds(20))return false;
        state.phase=NativeAtlasApplyPhase::Failed;
        state.status="Native terrain atlas再生成が20秒以内に完了しませんでした。安全のため停止しました。Minecraftを再起動してください。";
        return true;
    }
    void fail(std::uint64_t token,char const* reason) noexcept {
        std::lock_guard lock(mx);
        if(state.token==token){state.phase=NativeAtlasApplyPhase::Failed;state.status=reason;}
    }
    NativeAtlasApplyView view() const {std::lock_guard lock(mx);return state;}
};
inline LocalNativeAtlasApplyControl& localNativeAtlasApplyControl() {
    static auto* control=new LocalNativeAtlasApplyControl;return *control;
}
} // namespace praxis::companion::packs
