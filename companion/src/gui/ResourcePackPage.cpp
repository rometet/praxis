#include "gui/ResourcePackPage.h"
#include "resourcepacks/PackBrowser.h"
#include "resourcepacks/LocalOverlayStage.h"
#include "resourcepacks/R27LocalToggle.h"
#include "resourcepacks/LocalTerrainAtlasProof.h"
#include "resourcepacks/LocalNativeAtlasApplyControl.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace praxis::companion {
ResourcePackAction drawResourcePackApplyControls(packs::PackPlan const& plan,packs::TitlePackView const& backend){
    bool allowed=plan.validation==packs::ApplyResult::Valid&&backend.available&&!backend.busy&&backend.current.known;
    bool reloadAllowed=backend.available&&!backend.busy&&backend.current.known&&backend.canReload
        &&(backend.inWorld||(allowed&&!plan.proposed.empty()));
    bool changes=plan.priorityChanged||!plan.disabledCandidates.empty()||std::any_of(plan.proposed.begin(),plan.proposed.end(),[](auto const& p){return p.change==packs::ProposalChange::Enable;});
    ImGui::BeginDisabled(!allowed||!changes);
    bool apply=ImGui::Button("選択を適用");ImGui::EndDisabled();
    ImGui::BeginDisabled(!reloadAllowed);
    bool reload=ImGui::Button(backend.inWorld?"現在の有効構成を再読込":"選択した構成を再読込");
    ImGui::EndDisabled();
    if(reload&&reloadAllowed)return ResourcePackAction::Reload;
    if(!allowed)return ResourcePackAction::None;
    if(apply&&changes)return ResourcePackAction::Apply;
    return reload&&backend.canReload&&!plan.proposed.empty()?ResourcePackAction::Reload:ResourcePackAction::None;
}
void drawResourcePackPage(packs::AppliedStackView const& supplied){
    struct Drain {~Drain(){packs::drainPackDiagnostics();}} drain;
    auto backend=packs::titlePackView();auto const& current=supplied.known?supplied:backend.current;
    static char root[2048]{};
    static packs::Selection selection;
    static bool proposed{};
    static std::string lastRequest;
    static std::uint64_t lastRequestToken{},loggedGeneration{};
    static std::string loggedBrowserStatus;
    static packs::Selection loggedSelection;
    auto browser=packs::browserView();
    auto const& catalog=*browser.catalog;
    auto fromCurrent=[&]{selection.order.clear();selection.generation=catalog.generation;
        for(auto const& enabled:current.order)for(auto const& p:catalog.packs)
            if(p.selectable&&packs::matchesApplied(p,enabled))selection.order.push_back(p.key);
        proposed=true;
    };
    if(selection.generation!=catalog.generation)selection.reconcile(catalog);
    if(!proposed&&catalog.complete&&current.known)fromCurrent();
    ImGui::TextUnformatted("リソースパック");
    if(backend.r24AuthorizedClone)
        ImGui::TextWrapped("パックを選択して、Minecraftの全リソースを再読み込みできます（複製ワールド限定）。");
    else
        ImGui::TextWrapped("手順: 1. 一覧を読む / 2. 候補と順序を選ぶ / 3.「選択を適用」/ 4. 構成照合と見た目を確認する。");
    if(backend.inWorld&&!backend.r24AuthorizedClone)
        ImGui::TextWrapped("「現在の有効構成を再読込」は候補を適用せず、現在有効なファイルだけを再読込します。新規・削除ファイルの検出や非対応カテゴリの反映には再入場が必要です。構成変更はこのセッションに適用します。保存には通常のグローバルリソース設定を使用してください。");
    if(!backend.r24AuthorizedClone)
        ImGui::TextWrapped("texture フォルダを含むローカルパックを候補にできます。字体・shader・material・model・UI・behavior・依存・subpack はこの候補では適用できません。必須 pack は通常の Minecraft の管理機能を使用してください。");
    if(!root[0]&&!backend.folders[0].empty())
        std::snprintf(root,sizeof(root),"%s",backend.folders[0].c_str());
    // Open the test UI with the local catalog ready when possible.
    // Limit automatic scanning to once per process; manual refresh remains.
    static bool r27AutoScanRequested{};
    if(backend.r24AuthorizedClone&&!r27AutoScanRequested
        &&!catalog.complete&&!browser.busy&&root[0]){
        r27AutoScanRequested=true;
        bool accepted=packs::startBrowser()
            &&packs::requestScan(std::filesystem::path(
                std::u8string_view(reinterpret_cast<char8_t const*>(root))));
        packs::packDiagnostic("RESOURCEPACK_R27 event=auto_catalog_scan accepted=%u",
            unsigned(accepted));
    }
    ImGui::BeginDisabled(browser.busy);
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##pack_root","Minecraft が認識しているローカル pack フォルダ",root,sizeof(root));
    if(!backend.folders[0].empty()&&ImGui::Button("通常の場所"))std::snprintf(root,sizeof(root),"%s",backend.folders[0].c_str());
    if(ImGui::Button("一覧を更新")){
        bool accepted=packs::startBrowser()&&packs::requestScan(std::filesystem::path(std::u8string_view(reinterpret_cast<char8_t const*>(root))));
        packs::packDiagnostic("RESOURCEPACK_UI event=scan_request accepted=%u",unsigned(accepted));
    }
    ImGui::EndDisabled();
    if(browser.busy&&ImGui::Button("一覧の読込を中止"))packs::cancelScan();
    ImGui::TextWrapped("%s",packs::browserStatusJa(browser.status));
    if(!backend.r24AuthorizedClone)
        ImGui::TextWrapped("「一覧を更新」は一覧の読込だけです。選択や順序の編集も、適用ボタンを押すまではゲームに反映されません。");
    if(loggedGeneration!=catalog.generation||loggedBrowserStatus!=browser.status){
        loggedGeneration=catalog.generation;loggedBrowserStatus=browser.status;
        packs::packDiagnostic("RESOURCEPACK_UI event=scan_observation generation=%llu busy=%u complete=%u count=%zu error=%u",
            packs::diagNumber(catalog.generation),unsigned(browser.busy),unsigned(catalog.complete),catalog.packs.size(),unsigned(!catalog.error.empty()));
    }
    if(backend.inWorld&&backend.serverPackContext&&!backend.r24AuthorizedClone){
        ImGui::SeparatorText("サーバー／ワールド専用RPの共存状況");
        ImGui::Text("マルチプレイ %s / Level RP %zu / Full Stack %zu / Global対象 %s",
            backend.serverMultiplayer?"あり":"なし",backend.nativeLevelPackCount,
            backend.nativeFullPackCount,backend.nativeGlobalIncluded?"あり":"なし");
        ImGui::TextWrapped("粉雪texture解決元: %s",packs::serverAssetLayerJa(backend.powderSnowSource));
        ImGui::TextWrapped(backend.r24AuthorizedClone
            ?"R24の許可されたオフライン複製ワールドです。専用の全再読み込みボタンを使用してください。"
            :"サーバー配布パックと通常ワールドのFull Stack再構成は引き続き無効です。");
    }
    if(!backend.r24AuthorizedClone||ImGui::CollapsingHeader("現在のパック構成と変更候補")){
    ImGui::SeparatorText("ゲームで現在有効な構成");
    if(current.known){
        ImGui::Text("現在の Global 有効数: %zu",current.order.size());
        for(std::size_t i=0;i<current.order.size();++i){
            auto const& enabled=current.order[i];
            auto p=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& value){return packs::packHeaderMatches(value,enabled);});
            ImGui::TextWrapped("%zu. %s %s",i+1,p==catalog.packs.end()?enabled.uuid.c_str():p->name.c_str(),enabled.version.c_str());
        }
    }else ImGui::TextWrapped("現在状態を確認できません。一覧や候補は現在の適用状態を示しません。");
    ImGui::BeginDisabled(!current.known||!catalog.complete||browser.busy);
    if(ImGui::Button("候補を現在の構成に戻す"))fromCurrent();ImGui::EndDisabled();
    ImGui::SeparatorText("変更候補・優先順");
    ImGui::TextWrapped("上の候補ほど優先します。");
    ImGui::BeginDisabled(browser.busy);
    std::string moved;int direction{};
    for(std::size_t i=0;i<selection.order.size();++i){
        auto const& key=selection.order[i];auto it=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& p){return p.key==key;});
        if(it==catalog.packs.end())continue;
        ImGui::PushID(key.c_str());
        if(ImGui::SmallButton("外す")){selection.enable(catalog,key,false);proposed=true;ImGui::PopID();break;}
        ImGui::SameLine();ImGui::BeginDisabled(i==0);if(ImGui::SmallButton("上へ")){moved=key;direction=-1;}ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(i+1==selection.order.size());if(ImGui::SmallButton("下へ")){moved=key;direction=1;}ImGui::EndDisabled();
        ImGui::TextWrapped("%zu. %s",i+1,it->name.c_str());
        auto active=packs::appliedTo(*it,current);
        ImGui::TextWrapped("%s",packs::proposalChangeJa(!active?packs::ProposalChange::Unknown:*active?packs::ProposalChange::Keep:packs::ProposalChange::Enable));
        ImGui::PopID();
    }
    if(direction){selection.move(moved,direction);proposed=true;}
    if(selection.order.empty())ImGui::TextWrapped("候補は空です。適用すると、この一覧で変更可能な現在のパックを無効にします。対象外のパックは保持します。");
    ImGui::EndDisabled();
    }
    // Log only counts/types from already copied identities. No UUID/path/player
    // identifiers are emitted, and these observations grant no engine operation.
    unsigned headerMatches{},editableMatches{},nonResource{},subpacks{};
    for(auto const& active:current.order){
        nonResource+=active.packType!=6;subpacks+=!active.subpack.empty();
        for(auto const& p:catalog.packs)if(packs::packHeaderMatches(p,active)){
            ++headerMatches;if(p.selectable&&packs::matchesApplied(p,active))++editableMatches;break;
        }
    }
    std::array<std::uint64_t,8> identityState{catalog.generation,backend.token,std::uint64_t(current.known),current.order.size(),headerMatches,editableMatches,nonResource,subpacks};
    static decltype(identityState) loggedIdentityState{};
    if(identityState!=loggedIdentityState){loggedIdentityState=identityState;
        packs::packDiagnostic("RESOURCEPACK_UI event=current_identity token=%llu known=%u count=%zu header_matches=%u editable_matches=%u non_resource_types=%u named_subpacks=%u",
            packs::diagNumber(backend.token),unsigned(current.known),current.order.size(),headerMatches,editableMatches,nonResource,subpacks);
        for(std::size_t i=0;i<current.order.size();++i){auto const& p=current.order[i];
            packs::packDiagnostic("RESOURCEPACK_UI event=current_identity_shape ordinal=%zu type=%u named_subpack=%u",i,p.packType,unsigned(!p.subpack.empty()));
        }
    }
    auto plan=packs::makePackPlan(catalog,selection,current,browser.busy);
    if(!backend.r24AuthorizedClone){
    if(plan.priorityChanged)ImGui::TextWrapped("現在の有効順序から優先順を変える候補があります。");
    for(auto const& key:plan.disabledCandidates){
        auto p=std::find_if(catalog.packs.begin(),catalog.packs.end(),[&](auto const& value){return value.key==key;});
        ImGui::TextWrapped("無効にする候補: %s",p->name.c_str());
    }
    if(!plan.outsideScope.empty())ImGui::TextWrapped("一覧の対象外・非対応の現在パックは候補に含めません。通常の管理機能を使用してください。");
    auto enabledCount=std::count_if(plan.proposed.begin(),plan.proposed.end(),[](auto const& p){return p.change==packs::ProposalChange::Enable;});
    if(plan.currentKnown&&plan.validation==packs::ApplyResult::Valid){
        ImGui::Text("適用予定: 有効にする %zu / 無効にする %zu / 順序変更 %s",std::size_t(enabledCount),plan.disabledCandidates.size(),plan.priorityChanged?"あり":"なし");
        if(!backend.inWorld&&!enabledCount&&plan.disabledCandidates.empty()&&!plan.priorityChanged&&!plan.proposed.empty())
            ImGui::TextWrapped("現在と同じ構成の候補です。構成の変更なしで再読込を要求する場合は「選択した構成を再読込」を使用してください。");
    }
    if(loggedSelection.generation!=selection.generation||loggedSelection.order!=selection.order){
        loggedSelection=selection;
        packs::packDiagnostic("RESOURCEPACK_UI event=proposal generation=%llu count=%zu enable=%zu disable=%zu reorder=%u validation=%u",
            packs::diagNumber(selection.generation),selection.order.size(),std::size_t(enabledCount),plan.disabledCandidates.size(),unsigned(plan.priorityChanged),unsigned(plan.validation));
    }
    if(browser.busy)ImGui::TextWrapped("適用待ち: 一覧の読込が進行中です。");
    else if(plan.validation!=packs::ApplyResult::Valid)ImGui::TextWrapped("適用不可: 一覧と候補が一致していないか、非対応の候補が含まれています。一覧を更新して確認してください。");
    else if(!backend.busy&&plan.currentKnown&&plan.proposed.empty()&&plan.disabledCandidates.empty()&&!plan.priorityChanged)ImGui::TextWrapped("適用する変更がありません。対象のtextureを候補に選んでください。");
    else if(!backend.busy)ImGui::TextWrapped("%s",backend.available&&backend.current.known?packs::packApplyBlockJa(packs::PackApplyBlock::None):packs::packApplyBlockJa(backend.block));
    if(!backend.inWorld&&plan.validation==packs::ApplyResult::Valid&&plan.proposed.empty())ImGui::TextWrapped("再読込するtexture候補がありません。対象外の有効パックは、このツールでは変更しません。");
    }
    if(backend.inWorld&&backend.r24AuthorizedClone){
        ImGui::SeparatorText("リソースパック切替");
        ImGui::TextWrapped("オフライン複製ワールド専用。ブロック・アイテムなどの通常の画像パックを、ワールドから出ずにON/OFFできます。");
        // User selection is independent of proposed stack manipulation.
        // Off never means "remove all"; the native owner validates one
        // ordinary registered local texture pack at the Global tail.
        static std::string r27ChosenKey;
        static std::uint64_t r27SelectionGeneration{};
        static char r27NameFilter[128]{};
        if(catalog.complete){
            bool stillValid=std::any_of(catalog.packs.begin(),catalog.packs.end(),
                [&](auto const& pack){return pack.selectable&&pack.key==r27ChosenKey;});
            if(r27SelectionGeneration!=catalog.generation||!stillValid){
                if(!stillValid)r27ChosenKey.clear();
                r27SelectionGeneration=catalog.generation;
                if(r27ChosenKey.empty()){
                    for(auto const& pack:catalog.packs)
                        if(pack.selectable&&packs::r27UiToggleState(catalog,backend.current,pack).targetActive){
                            r27ChosenKey=pack.key;break;
                        }
                    if(r27ChosenKey.empty())
                        for(auto const& pack:catalog.packs)if(pack.selectable){
                            r27ChosenKey=pack.key;break;
                        }
                }
            }
        }
        packs::Pack const* chosen=nullptr;
        for(auto const& pack:catalog.packs)
            if(pack.selectable&&pack.key==r27ChosenKey){chosen=&pack;break;}
        ImGui::TextUnformatted("対象のローカルパック");
        std::string currentName=chosen?chosen->name+"  v"+chosen->version:"パックを選択";
        ImGui::BeginDisabled(browser.busy||!catalog.complete||backend.busy);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##r27_local_pack",currentName.c_str())){
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##r27_pack_filter","パック名で絞り込む",r27NameFilter,sizeof(r27NameFilter));
            for(auto const& pack:catalog.packs){
                if(!pack.selectable)continue;
                if(r27NameFilter[0]&&std::string_view(pack.name).find(r27NameFilter)==std::string_view::npos
                    &&std::string_view(pack.version).find(r27NameFilter)==std::string_view::npos)continue;
                ImGui::PushID(pack.key.c_str());
                bool selected=pack.key==r27ChosenKey;
                std::string label=pack.name+"  v"+pack.version;
                if(packs::r27UiToggleState(catalog,backend.current,pack).targetActive)
                    label+="  [ON]";
                if(ImGui::Selectable(label.c_str(),selected)){
                    r27ChosenKey=pack.key;
                    packs::packDiagnostic("RESOURCEPACK_R27 event=pack_selected generation=%llu",
                        packs::diagNumber(catalog.generation));
                }
                if(selected)ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        auto toggle=chosen?packs::r27UiToggleState(catalog,backend.current,*chosen)
            :packs::R27UiToggleState{};
        bool active=chosen&&toggle.targetActive;
        if(chosen&& !chosen->visualCoverage.empty())
            ImGui::TextWrapped("対象画像：%s",chosen->visualCoverage.c_str());
        if(chosen){
            ImGui::TextColored(active?ImVec4(.35f,.80f,.46f,1.f)
                                     :ImVec4(.64f,.66f,.70f,1.f),
                "%s",active?"現在 ON（有効）":"現在 OFF（無効）");
        }else ImGui::TextDisabled("切り替えるパックを選んでください。");
        bool canChange=chosen&&backend.available&&!backend.busy&&backend.current.known
            &&catalog.complete&&!browser.busy&&!backend.serverMultiplayer
            &&backend.nativeGlobalIncluded&&backend.nativeLevelPackCount==2
            &&(active?toggle.canDisable:toggle.canEnable);
        ImGui::BeginDisabled(!canChange);
        char const* primaryLabel=active?"OFFにして再読み込み":"ONにして再読み込み";
        if(ImGui::Button(primaryLabel,ImVec2(-1,34))){
            packs::Selection target;
            target.generation=catalog.generation;
            if(!active)target.order.push_back(chosen->key);
            auto result=packs::titlePackControl().requestResult(
                browser.catalog,target,packs::titlePackNow(),false);
            lastRequest=packs::titleRequestBlockJa(result.block);
            lastRequestToken=result.token;
            packs::packDiagnostic("RESOURCEPACK_R27 event=toggle_ui action=%s accepted=%u reason=%u token=%llu",
                active?"disable":"enable",unsigned(result.accepted),
                unsigned(result.block),packs::diagNumber(result.token));
        }
        ImGui::EndDisabled();
        bool canReload=backend.available&&!backend.busy&&backend.canReload
            &&backend.current.known&&!browser.busy;
        ImGui::BeginDisabled(!canReload);
        if(ImGui::Button("現在有効なパックを再読み込み",ImVec2(-1,0))){
            packs::Selection ignored;
            auto result=packs::titlePackControl().requestResult(
                browser.catalog,ignored,packs::titlePackNow(),true);
            lastRequest=packs::titleRequestBlockJa(result.block);
            lastRequestToken=result.token;
            packs::packDiagnostic("RESOURCEPACK_R27 event=toggle_ui action=reload accepted=%u reason=%u token=%llu",
                unsigned(result.accepted),unsigned(result.block),
                packs::diagNumber(result.token));
        }
        ImGui::EndDisabled();
        if(backend.phase==packs::TitlePackPhase::RecoveryRequired){
            ImGui::TextColored(ImVec4(1.0f,0.53f,0.34f,1.0f),
                "再読み込みの復帰を確認できません。Minecraft終了後にバックアップから復元してください。");
        }else if(backend.busy){
            ImGui::TextWrapped("Minecraftのリソースを読み込み中です。操作が完了するまで再度押さないでください。");
        }else if(!canChange&&chosen&&!active&&!toggle.canEnable){
            ImGui::TextWrapped("別のローカルパックが有効な可能性があります。先にそのパックをOFFにしてください。");
        }else if(!canChange&&chosen&&active&&!toggle.canDisable){
            ImGui::TextWrapped("このパックは安全な単独OFF条件を満たしません。元のパック構成を維持します。");
        }
        if(backend.phase==packs::TitlePackPhase::Complete){
            ImGui::TextColored(ImVec4(.35f,.80f,.46f,1.f),
                "再読み込みとLHoloの再開を確認しました。表示も目視で確認してください。");
        }
        if(!lastRequest.empty())ImGui::TextWrapped("%s",lastRequest.c_str());
        if(ImGui::CollapsingHeader("詳細：操作状態とテクスチャ診断")){
            ImGui::TextWrapped("%s",backend.status.c_str());
            ImGui::Text("Global RP %zu / Level RP %zu / Full Stack %zu",
                backend.current.order.size(),backend.nativeLevelPackCount,
                backend.nativeFullPackCount);
            ImGui::TextWrapped("旧・粉雪PNGの事前検証は任意の診断です。一般の画像パック切替には不要です。");
            bool canStage=chosen&&catalog.complete&&!browser.busy&&!backend.busy;
            ImGui::BeginDisabled(!canStage);
            if(ImGui::Button("粉雪PNGの詳細検証（任意）")){
                packs::Selection source;
                source.generation=catalog.generation;
                source.order.push_back(chosen->key);
                bool ok=packs::localOverlayStageControl().request(
                    browser.catalog,source,backend.inWorld);
                packs::packDiagnostic("RESOURCEPACK_R27 event=png_diagnostic accepted=%u",
                    unsigned(ok));
            }
            ImGui::EndDisabled();
            auto stage=packs::localOverlayStageControl().view();
            ImGui::TextWrapped("%s",stage.status.c_str());
            auto [proofValid,proof]=packs::localTerrainAtlasProofControl().view();
            if(proofValid&&proof.stagedToken==stage.token
                &&stage.phase==packs::LocalOverlayStagePhase::Decoded)
                ImGui::Text("Atlas %s / UV %s / PNG %ux%u",
                    proof.sameAtlasManager?"一致":"未確認",
                    packs::terrainUvGeometryValid(proof)?"正常":"未確認",
                    stage.width,stage.height);
        }
    }else if(backend.inWorld){
        ImGui::TextWrapped("ここでのワールド内切替は複製テストワールド限定です。"
            "通常ワールドやサーバーのパック設定には変更を加えません。");
    }
    // R25: the old atlas-only experiment is deliberately unavailable in the UI.
    auto action=backend.r24AuthorizedClone?ResourcePackAction::None:drawResourcePackApplyControls(plan,backend);
    if(action!=ResourcePackAction::None){
        auto result=packs::titlePackControl().requestResult(browser.catalog,selection,packs::titlePackNow(),action==ResourcePackAction::Reload);
        lastRequest=packs::titleRequestBlockJa(result.block);lastRequestToken=result.token;
        packs::packDiagnostic("RESOURCEPACK_UI event=apply_request action=%u accepted=%u reason=%u token=%llu generation=%llu count=%zu",
            unsigned(action),unsigned(result.accepted),unsigned(result.block),packs::diagNumber(result.token),packs::diagNumber(selection.generation),selection.order.size());
    }
    if(!lastRequest.empty())ImGui::TextWrapped("%s",lastRequest.c_str());
    if(lastRequestToken)ImGui::Text("要求番号: %llu",packs::diagNumber(lastRequestToken));
    if(backend.busy&&packs::titlePackControl().canCancel(backend.token)){
        ImGui::BeginDisabled(backend.phase!=packs::TitlePackPhase::Queued&&backend.phase!=packs::TitlePackPhase::Preparing&&backend.phase!=packs::TitlePackPhase::AwaitApply);
        if(ImGui::Button("開始前の取消"))packs::titlePackControl().cancel(backend.token);ImGui::EndDisabled();
    }
    if(!backend.r24AuthorizedClone||ImGui::CollapsingHeader("処理結果の詳細とLHoloの状態")){
    ImGui::SeparatorText(backend.busy?"現在進行中の適用":"前回の適用結果・現在の条件");
    if(backend.token)ImGui::Text("この結果の要求番号: %llu",packs::diagNumber(backend.token));
    ImGui::TextWrapped("%s",backend.status.c_str());
    auto const& observed=backend.observation;
    if(observed.world){
        ImGui::Text("ゲーム再読込: 開始 %s / 開始処理完了 %s / 終了 %s",observed.started?"確認済み":"未確認",observed.startReturned?"確認済み":"未確認",observed.finished?"確認済み":"未確認");
        if(observed.stackChecked)ImGui::Text("構成照合: %s / 予定 %zu・実際 %zu",observed.stackMatches?"一致":"不一致",observed.expectedCount,observed.observedCount);
        else ImGui::TextWrapped("構成照合: 再読込終了と安定した現在状態の確認を待っています。");
        for(unsigned i=0;i<observed.peers.size();++i){auto const& peer=observed.peers[i];if(!peer.observed)continue;
            if(peer.error||!peer.accepted)ImGui::Text("%s: 安全な準備を確認できません（状態 %u・理由 %u）",i?"LHolo":"Praxis",unsigned(peer.phase),peer.error);
            else if(peer.gpu==packs::OwnedGpuFence::Unknown||peer.gpu==packs::OwnedGpuFence::Pending)ImGui::Text("%s: 描画処理の安全確認を待っています。",i?"LHolo":"Praxis");
            else if(!peer.receiptMatched&&(peer.phase==packs::WorldPackReloadPhase::Prepared||peer.phase==packs::WorldPackReloadPhase::Ready))ImGui::Text("%s: 安全条件の一致を待っています。",i?"LHolo":"Praxis");
            else if(peer.readers||peer.jobs||peer.borrowers)ImGui::Text("%s: 処理の終了待ち（読込 %u・作業 %u・参照 %u）",i?"LHolo":"Praxis",peer.readers,peer.jobs,peer.borrowers);
        }
    }
    ImGui::TextWrapped("表示確認: 適用完了や構成の一致だけでは見た目の変化は確認できません。変更対象のブロックやアイテムを実際に確認してください。ワールド・サーバー側のパックが優先される場合もあります。");
    if(backend.inWorld)ImGui::TextWrapped("サーバーやワールド側でGlobalが制限される場合、その条件を維持します。反映できない変更は通常の管理機能を使い、必要なら再入場してください。");
    else ImGui::TextWrapped("タイトル限定: 起動後まだワールド未入場・通常タイトル・読込終了・登録済み texture が適用前提です。同じ構成の再読込はワールド内で行ってください。");
    ImGui::TextWrapped("字体・shader など非対応分類は通常の管理機能で適用し、必要なら再入場または再起動してください。");
    }
    if(!backend.r24AuthorizedClone||ImGui::CollapsingHeader("すべてのローカルパックと互換性診断")){
    ImGui::SeparatorText("ローカル一覧");
    ImGuiListClipper clipper;
    float rowHeight=backend.r24AuthorizedClone?104.f:152.f;
    clipper.Begin(int(catalog.packs.size()),rowHeight+ImGui::GetStyle().ItemSpacing.y);
    while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){
        auto const& p=catalog.packs[i];
        ImGui::PushID(p.key.c_str());
        ImGui::BeginChild("##pack",{0,rowHeight},ImGuiChildFlags_Borders);
        if(backend.r24AuthorizedClone){
            // Read-only details: changing a legacy proposal must not
            // silently affect the separate verified ON/OFF action.
            auto active=packs::r27UiToggleState(catalog,backend.current,p).targetActive;
            ImGui::TextWrapped("%s  v%s",p.name.c_str(),p.version.c_str());
            ImGui::TextColored(active?ImVec4(.35f,.80f,.46f,1.f)
                                     :ImVec4(.64f,.66f,.70f,1.f),
                "%s",!p.selectable?"管理対象外":active?"現在 ON":"現在 OFF");
            if(!p.visualCoverage.empty())
                ImGui::TextWrapped("画像内容: %s",p.visualCoverage.c_str());
            ImGui::TextWrapped("%s",packs::packReasonJa(p.reason));
        }else{
            bool selected=std::find(selection.order.begin(),selection.order.end(),p.key)!=selection.order.end();
            auto observedState=packs::packCurrentState(p,current);
            ImGui::BeginDisabled(!p.selectable||browser.busy||(!selected&&observedState==packs::PackCurrentState::ReadOnlyActive));
            if(ImGui::Checkbox("候補に含める",&selected)){
                selection.enable(catalog,p.key,selected);proposed=true;
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s %s",p.name.c_str(),p.version.c_str());
            ImGui::TextWrapped("%s",packs::packCurrentStateJa(observedState));
            ImGui::TextWrapped("分類: %s",p.categories.empty()?"分類情報なし":p.categories.c_str());
            ImGui::TextWrapped("%s",packs::packReasonJa(p.reason));
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    }
}
}
