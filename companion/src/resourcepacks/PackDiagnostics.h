#pragma once
#include "resourcepacks/WorldPackReloadApi.h"
#include "resourcepacks/PackPlan.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <optional>

namespace praxis::companion { void logGuiInfo(char const*) noexcept; }
namespace praxis::companion::packs {
// Existence in Global and permission to edit it are separate observations.
// This display-only helper never changes matchesApplied/native selection rules.
enum class PackCurrentState {Unknown,Inactive,EditableActive,ReadOnlyActive};
inline bool packHeaderMatches(Pack const& pack,AppliedPack const& active) noexcept {
    return pack.uuid==active.uuid&&pack.version==active.version;
}
inline PackCurrentState packCurrentState(Pack const& pack,AppliedStackView const& current) noexcept {
    if(!current.known)return PackCurrentState::Unknown;
    bool editable{};
    for(auto const& active:current.order)if(packHeaderMatches(pack,active)){
        if(!matchesApplied(pack,active)||!pack.selectable)return PackCurrentState::ReadOnlyActive;
        editable=true;
    }
    return editable?PackCurrentState::EditableActive:PackCurrentState::Inactive;
}
inline char const* packCurrentStateJa(PackCurrentState state) noexcept {
    switch(state){
    case PackCurrentState::Inactive:return "現在 Global で無効";
    case PackCurrentState::EditableActive:return "現在 Global で有効 / 変更可能な候補";
    case PackCurrentState::ReadOnlyActive:return "現在 Global で有効 / この候補の変更対象外。通常の管理機能で確認してください。";
    default:return "現在の有効状態は未確認";
    }
}
// Copied CPU observations only. Never a participant receipt or native admission.
struct PackPeerObservation {
    bool observed{},accepted{};
    WorldPackReloadCommand command{WorldPackReloadCommand::Query};
    std::uint64_t epoch{};
    WorldPackReloadPhase phase{WorldPackReloadPhase::Unknown};
    std::uint32_t error{},readers{},jobs{},borrowers{};
    OwnedGpuFence gpu{OwnedGpuFence::Unknown};
    bool receiptMatched{};
    bool operator==(PackPeerObservation const&) const = default;
};
struct PackApplyObservation {
    bool world{},reload{},started{},startReturned{},finished{},stackChecked{},stackMatches{},restoring{};
    std::uint64_t attempt{},oldEpoch{},nextEpoch{};
    std::size_t expectedCount{},observedCount{};
    std::array<PackPeerObservation,2> peers;
};
enum class PackApplyBlock {Unknown,None,Version,Owner,Screen,Loading,Restack,Callback,Provenance,Peer,History,Expired,Closed,ServerPack};
inline char const* packApplyBlockJa(PackApplyBlock reason) noexcept {
    switch(reason){
    case PackApplyBlock::None:return "適用を開始できます。下の適用ボタンを押すまでゲームの構成は変わりません。";
    case PackApplyBlock::Version:return "適用不可: 対応するゲーム本体を確認できません。";
    case PackApplyBlock::Owner:return "適用待ち: ワールドと描画の現在状態を確認できません。";
    case PackApplyBlock::Screen:return "適用待ち: 通常のゲーム画面へ戻ってください。";
    case PackApplyBlock::Loading:return "適用待ち: ゲームの読込完了を待っています。";
    case PackApplyBlock::Restack:return "適用待ち: ゲームが別のパック変更を処理しています。";
    case PackApplyBlock::Callback:return "適用待ち: リソース処理が終了するのを待っています。";
    case PackApplyBlock::Provenance:return "適用不可: 再読込の安全な追跡を確認できません。再起動が必要です。";
    case PackApplyBlock::Peer:return "適用不可: LHoloの再読込機能を確認できません。対応する候補が必要です。";
    case PackApplyBlock::History:return "適用不可: この起動中の適用上限（16回）です。再起動してください。";
    case PackApplyBlock::Expired:return "適用待ち: 現在状態の確認が古くなりました。";
    case PackApplyBlock::Closed:return "適用不可: 再読込機能は停止しています。";
    case PackApplyBlock::ServerPack:return "サーバー／ワールド専用RPを保護しています。サーバー接続中のローカルtexture更新経路は開発中のため、Full Stack再構成は実行しません。";
    default:return "適用待ち: 現在状態の確認を待っています。";
    }
}
// One process-wide cap across UI, scan and native observers, including concurrent
// callbacks. No paths, pack/player identifiers, pointers or arbitrary errors.
class PackDiagnosticBudget {
    std::atomic<unsigned> emitted{};
public:
    static constexpr unsigned Limit=384;
    std::optional<unsigned> reserve() noexcept {
        auto value=emitted.load();
        while(value<Limit){if(emitted.compare_exchange_weak(value,value+1))return value;}
        return std::nullopt;
    }
    bool take() noexcept {return reserve().has_value();}
    unsigned count() const noexcept {return emitted.load();}
};
class PackDiagnosticQueue {
    struct Entry {std::array<char,512> text{};std::atomic_bool ready{};};
    PackDiagnosticBudget budget;
    std::array<Entry,PackDiagnosticBudget::Limit> entries;
    std::atomic_flag draining=ATOMIC_FLAG_INIT;
    unsigned cursor{};
public:
    template<class... Args> void record(char const* format,Args... args) noexcept {
        auto slot=budget.reserve();if(!slot)return;
        auto& entry=entries[*slot];std::snprintf(entry.text.data(),entry.text.size(),format,args...);
        entry.ready.store(true,std::memory_order_release);
    }
    // No logger/registry call from native hooks or while an operation mutex is
    // held. The normal update/UI owner drains at most eight copied lines.
    template<class Sink> unsigned drain(Sink&& sink) noexcept {
        if(draining.test_and_set())return 0;
        struct Exit {std::atomic_flag& flag;~Exit(){flag.clear();}} exit{draining};
        unsigned count{};
        while(count<8&&cursor<budget.count()){
            auto& entry=entries[cursor];if(!entry.ready.load(std::memory_order_acquire))break;
            sink(entry.text.data());++cursor;++count;
        }
        return count;
    }
};
inline PackDiagnosticQueue& packDiagnosticQueue(){static PackDiagnosticQueue value;return value;}
inline unsigned long long diagNumber(std::uint64_t value) noexcept {return value;}
template<class... Args> void packDiagnostic(char const* format,Args... args) noexcept {
    packDiagnosticQueue().record(format,args...);
}
inline void drainPackDiagnostics() noexcept {packDiagnosticQueue().drain([](char const* text) noexcept {logGuiInfo(text);});}
}
