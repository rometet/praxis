#pragma once
#include "resourcepacks/PackCatalog.h"
#include <algorithm>
#include <optional>

namespace praxis::companion::packs {
// Copied identities only. Fresh-title native observations use this seam.
// An empty Unknown view is different from a verified empty Global stack.
struct AppliedPack { std::string uuid,version,subpack;unsigned packType{6}; };
struct AppliedStackView {
    bool known{};
    std::vector<AppliedPack> order; // native Global priority, not server/world
};
inline bool matchesApplied(Pack const& p,AppliedPack const& a) noexcept {
    return a.packType==6&&a.subpack.empty()&&p.uuid==a.uuid&&p.version==a.version;
}
inline std::optional<bool> appliedTo(Pack const& p,AppliedStackView const& current) noexcept {
    if(!current.known)return std::nullopt;
    return std::any_of(current.order.begin(),current.order.end(),[&](auto const& a){return matchesApplied(p,a);});
}
enum class ProposalChange { Unknown, Keep, Enable };
struct ProposalLine { std::string key;ProposalChange change{ProposalChange::Unknown}; };
struct PackPlan {
    ApplyResult validation{ApplyResult::StaleCatalog};
    bool currentKnown{},priorityChanged{};
    std::vector<ProposalLine> proposed;
    std::vector<std::string> disabledCandidates;
    std::vector<AppliedPack> outsideScope;
    // This pure proposal model grants no engine operation. The title backend
    // has separate owner/admission checks; fixtures never authorize native work.
    bool canApply() const noexcept {return false;}
};
inline PackPlan makePackPlan(Catalog const& catalog,Selection const& selected,
                             AppliedStackView const& current,bool busy) {
    PackPlan plan;plan.validation=checkApply(catalog,selected,busy);plan.currentKnown=current.known;
    if(plan.validation!=ApplyResult::Valid)return plan;
    for(auto const& key:selected.order){
        auto p=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& value){return value.key==key;});
        auto active=appliedTo(*p,current);
        plan.proposed.push_back({key,!active?ProposalChange::Unknown:*active?ProposalChange::Keep:ProposalChange::Enable});
    }
    if(!current.known)return plan;
    std::vector<std::string> oldSelectedOrder,newSelectedOrder;
    for(auto const& a:current.order){
        auto p=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& value){return value.selectable&&matchesApplied(value,a);});
        if(p==catalog.packs.end()){plan.outsideScope.push_back(a);continue;}
        if(std::find(selected.order.begin(),selected.order.end(),p->key)==selected.order.end())plan.disabledCandidates.push_back(p->key);
        else oldSelectedOrder.push_back(p->key);
    }
    for(auto const& key:selected.order)
        if(std::find(oldSelectedOrder.begin(),oldSelectedOrder.end(),key)!=oldSelectedOrder.end())newSelectedOrder.push_back(key);
    plan.priorityChanged=oldSelectedOrder!=newSelectedOrder;
    return plan;
}
inline char const* proposalChangeJa(ProposalChange value) noexcept {
    switch(value){
    case ProposalChange::Keep:return "現在も有効 / 優先順は候補";
    case ProposalChange::Enable:return "有効にする候補";
    default:return "現在との比較は未確認";
    }
}
inline char const* packReasonJa(std::string const& reason) noexcept {
    constexpr std::pair<char const*,char const*> labels[]{
        {"Manifest missing, unsupported or invalid","manifest がないか、形式が非対応です。"},
        {"Linked directories are excluded","リンクされたフォルダは一覧の対象外です。"},
        {"Manifest unreadable or exceeds 128 KiB","manifest を読めないか、128 KiB を超えています。"},
        {"Only manifest v2 is supported by this candidate","この候補では manifest v2 のみ一覧に対応します。"},
        {"Behavior/script/skin modules are excluded","behavior / script / skin は変更候補にできません。"},
        {"Protected content is excluded","保護されたパックは通常の管理機能を使用してください。"},
        {"Dependencies need normal Minecraft pack management","依存パックがあるため通常の管理機能が必要です。"},
        {"Subpack selection needs normal Minecraft pack management","subpack の選択は通常の管理機能が必要です。"},
        {"Declared capabilities need normal Minecraft pack management","特殊な描画機能を指定するパックは通常の管理機能が必要です。"},
        {"Pack enumeration cancelled or exceeded its budget","内容の確認を中止したか、確認の上限を超えました。"},
        {"Mixed/unsupported content: use normal pack management and re-enter","混在または非対応の内容です。通常の管理機能で適用し、必要なら再入場してください。"},
        {"This candidate supports ordinary texture packs only","texture 以外の分類は変更候補にできません。通常の管理機能を使用してください。"},
        {"Local texture proposal only; compatibility and native registration unverified","ローカル texture の変更候補です。内容の互換性とゲームへの登録は未確認です。"},
        {"Registered texture resources can use Minecraft's native Full Stack reload","ブロック・アイテムなどの通常の画像パックです。ゲームの登録実体と安全条件を適用時に再確認します。"},
        {"World-scoped pack: use normal Minecraft world pack settings","ワールド専用パックです。Globalからの切替には対応していません。"},
        {"Unknown resource pack scope","未対応のリソースパック適用範囲です。"},
        {"Texture directory must be a normal local directory","texturesフォルダが通常のローカルフォルダではありません。"},
        {"blocks.json must be a regular local file under 8 MiB","blocks.jsonが8MiB以上か、安全な通常ファイルではありません。"},
        {"Archive: import using Minecraft first","アーカイブです。通常の Minecraft の取込機能が必要です。"},
        {"Duplicate pack UUID: resolve in Minecraft first","UUID が重複しています。通常の管理機能で解消してください。"}};
    for(auto const& [key,value]:labels)if(reason==key)return value;
    return reason.c_str();
}
inline char const* browserStatusJa(std::string const& status) noexcept {
    constexpr std::pair<char const*,char const*> labels[]{
        {"Choose a local resource_packs folder and reload the list","ローカル resource_packs フォルダを指定し、一覧を更新してください。"},
        {"Local list reloaded","ローカル一覧を更新しました。ゲームの適用状態は変更していません。"},
        {"Reading local manifests in background...","manifest を別スレッドで読み込んでいます。"},
        {"Scan cancelled or failed; previous list retained","読込を中止したか失敗しました。以前の一覧を保っています。"},
        {"Choose an absolute folder on a local fixed drive","ローカル固定ドライブの絶対パスを指定してください。"},
        {"Missing or linked folder is excluded","存在しないフォルダやリンクは対象外です。"},
        {"Unable to read the selected folder","指定されたフォルダを読み込めません。"},
        {"Scan cancelled or exceeded the 512-entry / 2-second limit","読込を中止したか、512 件 / 2 秒の上限を超えました。"},
        {"Folder changed or became unreadable during scanning","読込中にフォルダが変わったか、読み込めなくなりました。"},
        {"Folder enumeration failed","一覧の読込に失敗しました。"},
        {"Unable to inspect local pack manifests","ローカル manifest を確認できません。"}};
    for(auto const& [key,value]:labels)if(status==key)return value;
    return status.c_str();
}
}
