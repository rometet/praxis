#include "resourcepacks/TitlePackSwitch.h"
#include "resourcepacks/TitlePackControl.h"
#include "resourcepacks/Title2660Receipt.h"
#include "resourcepacks/Title2660GuiReorder.h"
#include "core/NativeHookLifetime.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/deps/core/resource/ResourcePackStackType.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/PackAccessStrategy.h"
#include "mc/resources/Pack.h"
#include "mc/scripting/modules/PackManifest.h"
#include "mc/world/level/PackInstanceId.h"
#include "mc/deps/core/utility/NonOwnerPointer.h"
#include "mc/resources/IResourcePackRepository.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>

namespace praxis::companion::packs {
namespace {
auto& lifetime = *new NativeHookLifetime;

std::atomic_bool attempted{}, installed{};
std::atomic<std::uint64_t> updateSerial{}, activeSerial{}, finishSerial{};
std::atomic<std::uint64_t> lastTickMs{}, sequence{};
std::atomic<std::uint64_t> reportedActive{}, reportedFinish{};
std::atomic<std::uint64_t> setterGlobalCalls{},setterGlobalAccepted{};
std::atomic<std::uint64_t> userNativeSetters{};
std::atomic<std::uintptr_t> lastGlobalSetManager{},lastActiveManager{},lastFinishManager{};
std::atomic<std::uint64_t> lastSetterObserve{},lastAcceptedObserve{};
std::atomic<std::uint64_t> lastSnapshotDescriptor{~std::uint64_t{0}};
std::atomic_bool sawWorld{};
std::atomic_bool roundTripAttempted{};
struct TitleNativeReorder {
    enum class Step { None, Settling, SettlingRollback };
    Step step{Step::None};
    std::uint64_t token{}, owner{}, deadline{};
    std::vector<PackInstanceId> originalIDs;
    std::vector<AppliedPack> target;
    ResourcePackStack const* level{};
    ResourcePackStack const* base{};
    ResourcePackStack const* treatment{};
    std::size_t levelCount{}, baseCount{}, treatmentCount{};
    bool active{};
};
auto& titleOperation=*new TitleNativeReorder;
// Passive in production: without an admitted user request, the receipt has
// no native call scope. Its hook observers do not mutate any resources.
auto& nativeReceipt=*new Title2660Receipt;
std::atomic<DWORD> nativeReceiptThread{};
#if defined(PRAXIS_2660_AUDIT_SAME_GLOBAL_CANARY)
auto& canaryBackup=*new std::unique_ptr<ResourcePackStack>;
std::atomic_bool canaryRestorePending{};
std::atomic<std::uint64_t> canaryStartMs{};
#endif
std::atomic_flag tickGuard = ATOMIC_FLAG_INIT;
std::vector<AppliedPack> lastStableStack;
unsigned stableFrameCount{};
void anchor() {}
bool pin() noexcept {
    HMODULE module{};
    return GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&anchor), &module) != 0;
}

// Read the native title Global stack without retaining SDK objects across
// ticks. An explicitly submitted GUI request may initiate a reorder-only
// title transaction, requiring native callbacks and settled identities.
// Reconstruct an indexed subpack only when the SDK-provided subresource
// access strategy exposes a validated relative subpath.
// Never infer an index-to-name mapping from JSON order, a guessed basename,
// or an untyped SubpackInfoCollection layout.
std::optional<std::string> selectedSubpack(ResourcePack const& pack, int index) {
    if (!pack.mImpl.get() || !pack.mImpl->mPack.get()) return std::nullopt;
    auto const& children=pack.mImpl->mSubResourcePacks.get();
    auto const& subsidiary=pack.mImpl->mSubPacks.get();
    if (children.empty() && subsidiary.empty()) {
        // Vanilla uses either -1 or 0 for an ordinary pack without a
        // subpack. Both are unambiguous only while the SDK collections
        // confirm that this pack contains NO subpack entries.
        if (index == -1 || index == 0) return std::string{};
        return std::nullopt;
    }
    if (index < 0) return std::string{};
    if (static_cast<std::size_t>(index) >= children.size()) return std::nullopt;
    auto const& child=children[static_cast<std::size_t>(index)];
    if (!child || !child->mImpl.get() || !child->mImpl->mPack.get() ||
        !child->mImpl->mPack->mAccessStrategy) return std::nullopt;

    auto convert = [](std::string const& value) {
        return std::filesystem::path(std::u8string_view(
            reinterpret_cast<char8_t const*>(value.data()), value.size())).lexically_normal();
    };
    auto const* childStrategy=child->mImpl->mPack->mAccessStrategy.get();
    // getSubPath is a declared virtual SDK API. A subresource access
    // strategy can share the parent's pack location; in that case its
    // logical subpath is the only meaningful relative identity.
    auto sub=convert(childStrategy->getSubPath().value);
    if (sub.empty() || sub == ".") {
        if (!pack.mImpl->mPack->mAccessStrategy) return std::nullopt;
        auto const& rootPath=pack.mImpl->mPack->mAccessStrategy->getPackLocation().mPath.get().value;
        auto const& childPath=childStrategy->getPackLocation().mPath.get().value;
        if (rootPath.empty() || childPath.empty()) return std::nullopt;
        auto root=convert(rootPath);
        auto candidate=convert(childPath);
        if (candidate == root) return std::nullopt;
        sub=candidate.lexically_relative(root);
    }
    // Both "subpacks/default" and "default" are supported; the latter
    // is only accepted if a typed child in the engine's subpack array
    // supplied the name. Avoid trailing separator and parent traversal.
    if (sub.filename().empty()) sub=sub.parent_path();
    if (sub.filename().empty() || sub == "." || sub.is_absolute()) return std::nullopt;
    auto relative=sub.generic_string();
    if (relative.find("..") != std::string::npos) return std::nullopt;
    auto parts=std::vector<std::string>{};
    for (auto const& part:sub) {
        auto p=part.generic_string();
        if (!p.empty() && p != ".") parts.emplace_back(std::move(p));
    }
    if (parts.size()!=1 && !(parts.size()==2 && parts[0]=="subpacks")) return std::nullopt;
    auto name=parts.back();
    if (name.empty() || name.size()>64) return std::nullopt;
    for (auto ch:name) {
        auto c=static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && c!='-' && c!='_') return std::nullopt;
    }
    return name;
}
AppliedStackView snapshot(ResourcePackStack const* stack, unsigned& reason) {
    AppliedStackView out;
    if (!stack) {reason=1;return out;}
    if (stack->mStack->size() > 512) {reason=2;return out;}
    for (auto const& instance : *stack->mStack) {
        auto pack = instance.mPack.get();
        if (!pack || !pack->mImpl.get() || !pack->mImpl->mPack.get() ||
            !pack->mImpl->mPack->mManifest) {reason=3;return {};}
        // PackInstance stores an index, not a string. Resolve only a direct
        // subpacks/<folder> SDK resource path; leave unknown packs closed.
        auto subpack = selectedSubpack(*pack, instance.mSubpackIndex);
        if (!subpack) {reason=4;return {};}
        auto const& identity = pack->mImpl->mPack->mManifest->mIdentity.get();
        out.order.push_back({
            identity.mId->asString(),
            identity.mVersion->asString(),
            *subpack,
            static_cast<unsigned>(identity.mPackType)
        });
    }
    out.known = true;
    return out;
}
// Reconstruct exactly the observed native Global pack identities into a
// TEMPORARY candidate, without calling setStack or mutating the live game.
// This verifies the SDK constructor/owner/subpack round trip required by
// a future 26.60 title-only apply transaction.
unsigned stageReadOnlyRoundTrip(MinecraftGame& game,AppliedStackView const& observed) noexcept {
    try {
        auto& manager=game.getResourcePackManager();
        auto* native=manager.mGlobalStack.get();
        if(!native || !observed.known || native->mStack->size()!=observed.order.size()
           || observed.order.empty() || observed.order.size()>512) return 1;
        std::vector<PackInstanceId> identities;
        identities.reserve(native->mStack->size());
        for(auto const& instance:*native->mStack) {
            auto pack=instance.mPack.get();
            if(!pack || !pack->mImpl.get() || !pack->mImpl->mPack.get()
                || !pack->mImpl->mPack->mManifest) return 2;
            auto selected=selectedSubpack(*pack,instance.mSubpackIndex);
            if(!selected) return 3;
            auto const& identity=pack->mImpl->mPack->mManifest->mIdentity.get();
            identities.emplace_back(identity,*selected);
        }
        auto& repository=game.getResourcePackRepository();
        auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
            Bedrock::NonOwnerPointer<IResourcePackRepository const>(repository)};
        auto candidate=std::make_unique<ResourcePackStack>(identities,repo,false);
        unsigned reason{};
        auto copy=snapshot(candidate.get(),reason);
        if(!copy.known || copy.order.size()!=observed.order.size()) return 4;
        for(std::size_t i=0;i<copy.order.size();++i){
            auto const& expected=observed.order[i];
            auto const& actual=copy.order[i];
            if(expected.uuid!=actual.uuid || expected.version!=actual.version
               || expected.subpack!=actual.subpack || expected.packType!=actual.packType) return 5;
        }
#if defined(PRAXIS_2660_AUDIT_SAME_GLOBAL_CANARY)
        // ONE-TIME AUDIT CANARY ONLY. Called after five stable title frames,
        // with the exact same native identities and subpack order, and with
        // live World/Level/Treatment/Base stacks untouched. Never enable
        // this macro in distribution binaries.
        if(!manager.mUseGlobalPackStack || manager.mGameplayResourcesLoaded ||
            manager.mInitializing || manager.mPendingRestack ||
            game.isInGame() || !game.isReadyToRender()) return 7;
        // One different but equivalent-valid Global stack: reverse the
        // priority of the same three existing resource packs. The exact
        // original stack remains owned for an attempted in-client rollback.
        if(identities.size()!=3) return 9;
        std::vector<PackInstanceId> reordered;
        reordered.reserve(identities.size());
        for(auto it=identities.rbegin();it!=identities.rend();++it)
            reordered.emplace_back(it->mPackId.get(),it->mSubpackName.get());
        auto changed=std::make_unique<ResourcePackStack>(reordered,repo,false);
        unsigned testReason{};
        auto ordered=snapshot(changed.get(),testReason);
        if(!ordered.known || ordered.order.size()!=observed.order.size()) return 10;
        for(std::size_t i=0;i<ordered.order.size();++i){
            auto const& source=observed.order[observed.order.size()-i-1];
            auto const& target=ordered.order[i];
            if(source.uuid!=target.uuid || source.version!=target.version ||
                source.subpack!=target.subpack || source.packType!=target.packType) return 11;
        }
        if(manager.mPendingRestack || !game.isReadyToRender() || game.isInGame())return 12;
        auto beforeSetter=setterGlobalCalls.load(std::memory_order_acquire);
        auto beforeActive=activeSerial.load(std::memory_order_acquire);
        auto beforeFinish=finishSerial.load(std::memory_order_acquire);
        logGuiInfo("RESOURCEPACK_TITLE2660 canary_before_setter same_three_packs_reordered=1");
        auto const owner=static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&manager));
        std::vector<std::string> beforeNames,afterNames;
        beforeNames.reserve(observed.order.size());
        afterNames.reserve(ordered.order.size());
        auto canonical=[](AppliedPack const& x) {
            return std::to_string(x.packType)+"/"+x.uuid+"/"+x.version+"/"+x.subpack;
        };
        for(auto const& x:observed.order)beforeNames.emplace_back(canonical(x));
        for(auto const& x:ordered.order)afterNames.emplace_back(canonical(x));
        if(!nativeReceipt.prepare(0x2660u,owner,std::move(beforeNames),
                                  std::move(afterNames),titlePackNow()+30000) ||
           !nativeReceipt.beginNativeCall(owner,titlePackNow()))return 13;
        nativeReceiptThread.store(GetCurrentThreadId(),std::memory_order_release);
        struct NativeCallScopeExit {
            ~NativeCallScopeExit() {
                nativeReceiptThread.store(0,std::memory_order_release);
            }
        } scopeExit;
        bool accepted=manager.setStack(std::move(changed),ResourcePackStackType::Global);
        nativeReceipt.setterResult(owner,accepted);
    if(accepted)userNativeSetters.fetch_add(1,std::memory_order_acq_rel);
        logGuiInfo("RESOURCEPACK_TITLE2660 canary_after_setter_returned=1");
        if(accepted){
            canaryBackup=std::move(candidate);
            canaryStartMs.store(titlePackNow(),std::memory_order_release);
            canaryRestorePending.store(true,std::memory_order_release);
            // setStack only changes the manager's configured stack.
            // The existing world port uses the public virtual ResourcePackListener
            // notification to tell MinecraftGame to rebuild the active resources.
            // This is an explicit one-time TITLE-only audit, never a release.
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_compose_before=1");
            manager._composeFullStack();
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_compose_returned=1");
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_notify_before=1");
            game.onActiveResourcePacksChanged(manager);
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_notify_returned=1");
            bool receiptAccepted=nativeReceipt.endNativeCall(owner,titlePackNow());
            logGuiInfo(receiptAccepted
                ? "RESOURCEPACK_TITLE2660 canary_receipt_callbacks_bound=1"
                : "RESOURCEPACK_TITLE2660 canary_receipt_callbacks_bound=0");
        }
        auto afterSetter=setterGlobalCalls.load(std::memory_order_acquire);
        auto afterActive=activeSerial.load(std::memory_order_acquire);
        auto afterFinish=finishSerial.load(std::memory_order_acquire);
        char record[192]{};
        std::snprintf(record,sizeof(record),
            "RESOURCEPACK_TITLE2660 canary_reorder_three=1 set_returned=%u setter_delta=%llu active_delta=%llu finish_delta=%llu",
            unsigned(accepted),
            static_cast<unsigned long long>(afterSetter-beforeSetter),
            static_cast<unsigned long long>(afterActive-beforeActive),
            static_cast<unsigned long long>(afterFinish-beforeFinish));
        logGuiInfo(record);
        return accepted ? 0U : 8U;
#else
        return 0;
#endif
    }catch(...){return 6;}
}
std::string title2660Key(AppliedPack const& x) {
    return std::to_string(x.packType)+"/"+x.uuid+"/"+x.version+"/"+x.subpack;
}
std::vector<std::string> title2660Keys(std::vector<AppliedPack> const& order) {
    std::vector<std::string> out;
    out.reserve(order.size());
    for(auto const& p:order)out.push_back(title2660Key(p));
    return out;
}
bool sameOtherLayers(ResourcePackManager& manager) noexcept {
    auto const& op=titleOperation;
    auto* level=manager.mLevelStack.get();
    auto* base=manager.mBaseGameStack.get();
    auto* treatment=manager.mTreatmentStack.get();
    auto count=[](ResourcePackStack const* stack)->std::size_t {
        return stack ? stack->mStack->size() : 0;
    };
    return op.level==level && op.base==base && op.treatment==treatment
        && op.levelCount==count(level) && op.baseCount==count(base)
        && op.treatmentCount==count(treatment);
}
bool title2660Admission(MinecraftGame& game,ResourcePackManager& manager) {
    if(!installed.load(std::memory_order_acquire) ||
       !runtimeReady.load(std::memory_order_acquire) ||
       game.isInGame() || worldGeneration.load() ||
       sawWorld.load(std::memory_order_acquire) ||
       game.isEditorModeEnabled() || !game.isReadyToRender() ||
       manager.mInitializing || manager.mPendingRestack ||
       !manager.mUseGlobalPackStack || manager.mGameplayResourcesLoaded ||
       !manager.mGlobalStack.get())return false;
    auto clients=game.getClientInstanceMap();
    return clients.size()==1 && clients.begin()->second &&
       !clients.begin()->second->getLocalPlayer() &&
       clients.begin()->second->getScreenName()=="start_screen";
}
void title2660StopOperation(Title2660GuiReorderState state,
                           char const* message) {
    auto token=titleOperation.token;
    title2660GuiReorder().report(token,state,message);
    titleOperation.active=false;
    titleOperation.step=TitleNativeReorder::Step::None;
    titleOperation.originalIDs.clear();
    titleOperation.target.clear();
}
bool title2660ScopedReload(MinecraftGame& game,ResourcePackManager& manager,
                           std::vector<PackInstanceId> const& identities) {
    auto owner=static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&manager));
    if(!title2660Admission(game,manager) || !sameOtherLayers(manager) ||
       owner!=titleOperation.owner ||
       identities.empty() || titleOperation.deadline<titlePackNow())return false;
    // Allocate before claiming the native call: constructor failure
    // cannot leave the receipt armed while the original stack is intact.
    auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
        Bedrock::NonOwnerPointer<IResourcePackRepository const>(
            game.getResourcePackRepository())};
    auto candidate=std::make_unique<ResourcePackStack>(identities,repo,false);
    if(!nativeReceipt.beginNativeCall(owner,titlePackNow()))return false;
    nativeReceiptThread.store(GetCurrentThreadId(),std::memory_order_release);
    struct NativeScopeExit {
        ~NativeScopeExit(){ nativeReceiptThread.store(0,std::memory_order_release); }
    } scopeExit;
    bool accepted=manager.setStack(std::move(candidate),ResourcePackStackType::Global);
    nativeReceipt.setterResult(owner,accepted);
    if(accepted)userNativeSetters.fetch_add(1,std::memory_order_acq_rel);
    if(!accepted)return false;
    manager._composeFullStack();
    game.onActiveResourcePacksChanged(manager);
    return nativeReceipt.endNativeCall(owner,titlePackNow());
}
void title2660Advance(MinecraftGame& game,ResourcePackManager& manager,
                      AppliedStackView const& current,std::uint64_t now) {
    if(!titleOperation.active)return;
    auto& op=titleOperation;
    auto owner=static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&manager));
    // A native reload can temporarily hide the title surface. Do not
    // treat an expected rendering pause as a failed settlement. An actual
    // world transition or manager change is never allowed to apply.
    auto titleReady=title2660Admission(game,manager);
    if(!titleReady && owner==op.owner && !game.isInGame() &&
       !worldGeneration.load() && !sawWorld.load(std::memory_order_acquire)
       && now<=op.deadline)return;
    auto verdict=nativeReceipt.poll(owner,updateSerial.load(std::memory_order_acquire),
        now,title2660Keys(current.order),titleReady,manager.mPendingRestack,
        sameOtherLayers(manager));
    if(verdict==Title2660Receipt::Poll::Applied) {
        title2660StopOperation(Title2660GuiReorderState::Applied,
            "タイトル画面で有効パックの順序変更を確認しました。次回起動時の保存状態は未検証です。");
        return;
    }
    if(verdict==Title2660Receipt::Poll::Restored){
        title2660StopOperation(Title2660GuiReorderState::Restored,
            "適用を中止し、元のパック順序への復元を確認しました。");
        return;
    }
    if(verdict==Title2660Receipt::Poll::RecoveryRequired){
        title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
            "リソースパックの安全な復元を証明できません。Minecraftを終了し、バックアップを確認してください。");
        return;
    }
    if(verdict!=Title2660Receipt::Poll::NeedRollback)return;
    // A completed or uncertain apply can never be labelled success. Try
    // one rollback through the same native API only while the original
    // manager, title client and every other pack layer are still intact.
    if(!titleReady || !sameOtherLayers(manager) ||
       !nativeReceipt.beginRollback(owner,now+30000)) {
        nativeReceipt.markRecoveryRequired();
        title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
            "ワールドや設定が変化したため自動復元できません。再起動が必要です。");
        return;
    }
    op.deadline=now+30000;
    title2660GuiReorder().report(op.token,Title2660GuiReorderState::Restoring,
        "再読み込みが一致しなかったため、元のパック順序へ復元中です。");
    try {
        if(!title2660ScopedReload(game,manager,op.originalIDs)){
            nativeReceipt.markRecoveryRequired();
            title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
                "元のパック順序へ戻す処理を完了できません。再起動が必要です。");
            return;
        }
        op.step=TitleNativeReorder::Step::SettlingRollback;
    }catch(...){
        nativeReceipt.markRecoveryRequired();
        title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
            "復元中にエラーが起きました。パックの状態を確認してください。");
    }
}
void title2660HandleRequest(MinecraftGame& game,ResourcePackManager& manager,
                            AppliedStackView const& observed,std::uint64_t now,
                            unsigned stableFrames){
    if(titleOperation.active)return;
    auto request=title2660GuiReorder().take();
    if(!request)return;
    auto reject=[&](char const* reason){
        title2660GuiReorder().report(request->token,Title2660GuiReorderState::Rejected,reason);
    };
    if(now>request->deadline || stableFrames<5 ||
       !title2660Admission(game,manager) || !observed.known ||
       !sameTitle2660Stack(observed.order,request->original) ||
       request->target.size()!=observed.order.size() ||
       observed.order.size()<2 || observed.order.size()>512){
        reject("実行直前のパック構成が変わりました。再確認してから操作してください。");
        return;
    }
    // The queue owner validates a strict native permutation. Revalidate
    // the same set here to reject stale UI data or a replaced pack owner.
    std::vector<bool> matched(observed.order.size());
    for(auto const& item:request->target) {
        bool found{};
        for(std::size_t i=0;i<observed.order.size();++i){
            if(!matched[i] && sameTitle2660Pack(item,observed.order[i])) {
                matched[i]=true;found=true;break;
            }
        }
        if(!found){reject("有効パック以外を含む変更は26.60では未対応です。");return;}
    }
    if(sameTitle2660Stack(request->target,observed.order)){
        reject("パック順序に変更がありません。");return;
    }
    try {
        auto* original=manager.mGlobalStack.get();
        if(!original || original->mStack->size()!=observed.order.size()) {
            reject("元のパックを確認できません。");return;
        }
        std::vector<PackInstanceId> baseline;
        baseline.reserve(observed.order.size());
        for(auto const& instance:*original->mStack) {
            auto pack=instance.mPack.get();
            if(!pack || !pack->mImpl.get() || !pack->mImpl->mPack.get() ||
               !pack->mImpl->mPack->mManifest) {
                reject("ゲームのパック情報に不足があります。");return;
            }
            auto subpack=selectedSubpack(*pack,instance.mSubpackIndex);
            if(!subpack) {
                reject("サブパック名を確認できません。");return;
            }
            baseline.emplace_back(pack->mImpl->mPack->mManifest->mIdentity.get(),*subpack);
        }
        std::vector<PackInstanceId> proposed;
        proposed.reserve(request->target.size());
        std::vector<bool> used(observed.order.size());
        for(auto const& desired:request->target) {
            bool found{};
            for(std::size_t i=0;i<observed.order.size();++i) {
                if(!used[i] && sameTitle2660Pack(desired,observed.order[i])){
                    proposed.emplace_back(baseline[i].mPackId.get(),
                                           baseline[i].mSubpackName.get());
                    used[i]=true;found=true;break;
                }
            }
            if(!found){reject("操作対象が一致しません。");return;}
        }
        auto repo=Bedrock::NotNullNonOwnerPtr<IResourcePackRepository const>{
            Bedrock::NonOwnerPointer<IResourcePackRepository const>(
                game.getResourcePackRepository())};
        auto targetStack=std::make_unique<ResourcePackStack>(proposed,repo,false);
        unsigned reason{};
        auto targetSnapshot=snapshot(targetStack.get(),reason);
        if(!targetSnapshot.known ||
           !sameTitle2660Stack(targetSnapshot.order,request->target)) {
            reject("候補をMinecraftのパックスタックとして再構成できません。");return;
        }
        // Prebuild the rollback stack before any native mutation, so
        // ordinary allocation failure cannot strand the active Global.
        auto restoreStack=std::make_unique<ResourcePackStack>(baseline,repo,false);
        auto restoreSnapshot=snapshot(restoreStack.get(),reason);
        if(!restoreSnapshot.known ||
           !sameTitle2660Stack(restoreSnapshot.order,request->original)) {
            reject("安全な復元候補を作れません。元の構成は維持します。");return;
        }
        // Constructing the stacks above is a preflight. The setter below
        // rebuilds from the same exact IDs and is the only mutation point.
        auto owner=static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&manager));
        nativeReceipt=Title2660Receipt{};
        if(!nativeReceipt.prepare(request->token,owner,
                title2660Keys(request->original),title2660Keys(request->target),
                now+30000)){
            reject("適用要求の所有者を確認できません。");return;
        }
        titleOperation={};
        auto& op=titleOperation;
        op.active=true;
        op.token=request->token;
        op.owner=owner;
        op.deadline=now+30000;
        op.originalIDs=std::move(baseline);
        op.target=request->target;
        op.level=manager.mLevelStack.get();
        op.base=manager.mBaseGameStack.get();
        op.treatment=manager.mTreatmentStack.get();
        op.levelCount=op.level?op.level->mStack->size():0;
        op.baseCount=op.base?op.base->mStack->size():0;
        op.treatmentCount=op.treatment?op.treatment->mStack->size():0;
        title2660GuiReorder().report(op.token,Title2660GuiReorderState::Applying,
            "タイトル画面でパックを再読み込み中です。操作を重ねないでください。");
        if(!title2660ScopedReload(game,manager,proposed)){
            // If the setter refused, the original Global may be unchanged;
            // recheck from the engine owner before deciding on rollback.
            unsigned latestReason{};
            auto latest=snapshot(manager.mGlobalStack.get(),latestReason);
            if(nativeReceipt.phase()==Title2660Receipt::Phase::Rejected &&
               latest.known &&
               sameTitle2660Stack(latest.order,request->original)){
                title2660StopOperation(Title2660GuiReorderState::Rejected,
                    "Minecraftが変更を受け付けませんでした。元のパック構成を保持しました。");
            }else{
                // NeedRollback is processed by the next safe game tick.
                op.step=TitleNativeReorder::Step::Settling;
            }
            return;
        }
        op.step=TitleNativeReorder::Step::Settling;
    }catch(...){
        if(titleOperation.active){
            nativeReceipt.markRecoveryRequired();
            title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
                "変更中にエラーが起きました。元の構成への復元を確認できません。");
        }else reject("適用の準備に失敗しました。ゲーム側には変更していません。");
    }
}
void tick(MinecraftGame& game) noexcept {
    if (!installed.load(std::memory_order_acquire) ||
        titlePackControl().stopped() || tickGuard.test_and_set(std::memory_order_acquire)) return;
    struct TickExit {
        ~TickExit() { tickGuard.clear(std::memory_order_release); }
    } exit;
    auto now = titlePackNow();
    if (now - lastTickMs.load(std::memory_order_relaxed) < 300) return;
    lastTickMs.store(now, std::memory_order_relaxed);
#if defined(PRAXIS_2660_AUDIT_SAME_GLOBAL_CANARY)
    if(canaryRestorePending.load(std::memory_order_acquire)
        && now-canaryStartMs.load(std::memory_order_acquire)>=1500
        && !game.isInGame() && !worldGeneration.load() && game.isReadyToRender()){
        auto clients=game.getClientInstanceMap();
        auto& mgr=game.getResourcePackManager();
        if(clients.size()==1 && clients.begin()->second &&
            !clients.begin()->second->getLocalPlayer() &&
            clients.begin()->second->getScreenName()=="start_screen" &&
            !mgr.mInitializing && !mgr.mPendingRestack && canaryBackup){
            canaryRestorePending.store(false,std::memory_order_release);
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_before_restore original_three=1");
            bool restored=mgr.setStack(std::move(canaryBackup),ResourcePackStackType::Global);
            if(restored){
                logGuiInfo("RESOURCEPACK_TITLE2660 canary_restore_compose_before=1");
                mgr._composeFullStack();
                logGuiInfo("RESOURCEPACK_TITLE2660 canary_restore_compose_returned=1");
                logGuiInfo("RESOURCEPACK_TITLE2660 canary_restore_notify_before=1");
                game.onActiveResourcePacksChanged(mgr);
                logGuiInfo("RESOURCEPACK_TITLE2660 canary_restore_notify_returned=1");
            }
            char message[125]{};
            std::snprintf(message,sizeof(message),
                "RESOURCEPACK_TITLE2660 canary_restore_setter_returned=%u",unsigned(restored));
            logGuiInfo(message);
        }
    }
#endif
    // This only counts callbacks AFTER their original engine functions have
    // returned. It does not establish causal identity of a given pack apply.
    auto const active = activeSerial.load(std::memory_order_acquire);
    auto const finished = finishSerial.load(std::memory_order_acquire);
    if (active != reportedActive.load(std::memory_order_relaxed) ||
        finished != reportedFinish.load(std::memory_order_relaxed)) {
        reportedActive.store(active, std::memory_order_relaxed);
        reportedFinish.store(finished, std::memory_order_relaxed);
        char message[192]{};
        std::snprintf(message, sizeof(message),
                      "RESOURCEPACK_TITLE2660 native_active=%llu native_finish=%llu user_setters=%llu",
                      static_cast<unsigned long long>(active),
                      static_cast<unsigned long long>(finished),
                      static_cast<unsigned long long>(userNativeSetters.load(std::memory_order_acquire)));
        logGuiInfo(message);
    }
    // A vanilla setter call and a native reload callback are distinct
    // facts, not one proven apply cycle. Only log counters for the live
    // title manager; neither a pointer nor a pack identifier is emitted.
    auto& currentManager=game.getResourcePackManager();
    auto const owner=reinterpret_cast<std::uintptr_t>(&currentManager);
    auto const seenSetter=setterGlobalCalls.load(std::memory_order_acquire);
    auto const seenAccepted=setterGlobalAccepted.load(std::memory_order_acquire);
    if(seenSetter!=lastSetterObserve.load(std::memory_order_relaxed) ||
       seenAccepted!=lastAcceptedObserve.load(std::memory_order_relaxed)) {
        lastSetterObserve.store(seenSetter,std::memory_order_relaxed);
        lastAcceptedObserve.store(seenAccepted,std::memory_order_relaxed);
        char message[245]{};
        std::snprintf(message,sizeof(message),
            "RESOURCEPACK_TITLE2660 setter_seen=%llu accepted=%llu owner_match=%u active_owner_match=%u finish_owner_match=%u user_setters=%llu",
            static_cast<unsigned long long>(seenSetter),
            static_cast<unsigned long long>(seenAccepted),
            unsigned(lastGlobalSetManager.load()==owner),
            unsigned(lastActiveManager.load()==owner),
            unsigned(lastFinishManager.load()==owner),
            static_cast<unsigned long long>(userNativeSetters.load(std::memory_order_acquire)));
        logGuiInfo(message);
    }

    TitlePackView view;

    view.until = now + 1500;
    view.revision = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    view.available = false;
    view.busy = false;
    view.canReload = false;
    view.status = "26.60: 現在のリソースパックを確認中です。適用機能は安全性検証待ちです。";
    try {
        if (!runtimeReady.load(std::memory_order_acquire)) {
            titlePackControl().publish(std::move(view));
            return;
        }
        if (game.isInGame() || worldGeneration.load()) {
            sawWorld.store(true, std::memory_order_release);
            if(titleOperation.active) {
                nativeReceipt.markRecoveryRequired();
                title2660StopOperation(Title2660GuiReorderState::RecoveryRequired,
                    "Minecraft world transition interrupted the title transaction. Restart to recover.");
            }
            // Let the 26.60 world observer publish the in-world view.
            return;
        }
        if (sawWorld.load(std::memory_order_acquire) ||
            !game.isReadyToRender() || game.isEditorModeEnabled())
        {
            titlePackControl().publish(std::move(view));
            return;
        }
        auto clients = game.getClientInstanceMap();
        if (clients.size() != 1 || !clients.begin()->second ||
            clients.begin()->second->getLocalPlayer() ||
            clients.begin()->second->getScreenName() != "start_screen") {
            titlePackControl().publish(std::move(view));
            return;
        }
        auto& manager = game.getResourcePackManager();
        if (manager.mInitializing || manager.mPendingRestack ||
            !manager.mGlobalStack.get()) {
            titlePackControl().publish(std::move(view));
            return;
        }
        unsigned reason{};
        view.current = snapshot(manager.mGlobalStack.get(), reason);
        auto count = view.current.known ? view.current.order.size() : 0U;
        auto const descriptor = (std::uint64_t(reason) << 48) |
            (std::uint64_t(view.current.known) << 47) |
            (std::uint64_t(manager.mUseGlobalPackStack) << 46) |
            (std::uint64_t(manager.mGameplayResourcesLoaded) << 45) |
            (std::uint64_t(count) & 0xFFFF);
        if (lastSnapshotDescriptor.exchange(descriptor) != descriptor) {
            char message[256]{};
            std::snprintf(message, sizeof(message),
                          "RESOURCEPACK_TITLE2660 title_snapshot_known=%u global_count=%zu unavailable_reason=%u use_global=%u gameplay_resources_loaded=%u apply_enabled=0",
                          unsigned(view.current.known),count,reason,
                          unsigned(manager.mUseGlobalPackStack),
                          unsigned(manager.mGameplayResourcesLoaded));
            logGuiInfo(message);
        }
        // Do not stage from the first visible nonempty stack during loading.
        // Require repeated exact identities while the native manager is idle.
        bool same=lastStableStack.size()==view.current.order.size();
        if(same){
            for(std::size_t i=0;i<view.current.order.size();++i){
                auto const& old=lastStableStack[i];
                auto const& now= view.current.order[i];
                if(old.uuid!=now.uuid || old.version!=now.version ||
                   old.subpack!=now.subpack || old.packType!=now.packType){same=false;break;}
            }
        }
        if(!view.current.known || view.current.order.empty()){
            stableFrameCount=0;
            lastStableStack.clear();
        }else if(!same){
            lastStableStack=view.current.order;
            stableFrameCount=1;
        }else if(stableFrameCount<6)++stableFrameCount;
#if defined(PRAXIS_2660_AUDIT_SAME_GLOBAL_CANARY)
        if(stableFrameCount==5 && !roundTripAttempted.load(std::memory_order_acquire))
            logGuiInfo("RESOURCEPACK_TITLE2660 canary_stable_title_frames=5");
#endif
        if (stableFrameCount>=5 &&
            !roundTripAttempted.exchange(true,std::memory_order_acq_rel)) {
            auto const code=stageReadOnlyRoundTrip(game,view.current);
            char message[144]{};
#if defined(PRAXIS_2660_AUDIT_SAME_GLOBAL_CANARY)
            std::snprintf(message,sizeof(message),
                "RESOURCEPACK_TITLE2660 stage_roundtrip_or_canary=%u identities=%zu audit_reorder_only=1 production_apply_enabled=0",
                code,view.current.order.size());
#else
            std::snprintf(message,sizeof(message),
                "RESOURCEPACK_TITLE2660 stage_roundtrip=%u identities=%zu stack_changed=0 apply_enabled=0",
                code,view.current.order.size());
#endif
            logGuiInfo(message);
        }
        // Native Global selection is owned by the engine. Only after at
        // least five stable title observations does the explicit GUI
        // reorder control become available. The legacy broad apply path
        // remains disabled and all world-side changes remain unavailable.
        bool const stableTitle=stableFrameCount>=5 &&
            view.current.known && view.current.order.size()>=2 &&
            manager.mUseGlobalPackStack && !manager.mGameplayResourcesLoaded;
        if(stableTitle) {
            bool const existingOperation=titleOperation.active;
            if(existingOperation) {
                title2660Advance(game,manager,view.current,now);
            } else {
                title2660HandleRequest(game,manager,view.current,now,stableFrameCount);
            }
            view.nativeReorderOnly=true;
            view.busy=title2660GuiReorder().isBusy();
            view.available=!view.busy&&!title2660GuiReorder().needsRecovery();
            view.canReload=false;

            view.status="26.60: タイトル画面の既存パック並び替えに限定。追加・削除やワールド切り替えは未対応です。";
        }else if(titleOperation.active){
            // Do not claim a successful title-only operation after leaving
            // its admission context, even if a finish callback was seen.
            title2660Advance(game,manager,view.current,now);
            view.busy=title2660GuiReorder().isBusy();
            view.available=false;
        }
        if (view.current.known) {
            auto& repository = game.getResourcePackRepository();
            view.folders = {
                repository.getResourcePacksPath().value,
                repository.getDevelopmentResourcePacksPath().value
            };
            if(!view.nativeReorderOnly)
                view.status = "26.60: 現在の構成を確認しました。既存パックの変更はタイトル画面でのみ利用できます。";
        }
        titlePackControl().publish(std::move(view));
    } catch (...) {
        // Do not allow a UI/controller transition to terminate the game.
        view.available = false;
        view.current = {};
        view.status = "26.60: 構成の取得に失敗したため、適用は無効です。";
        titlePackControl().publish(std::move(view));
    }
}
LL_TYPE_INSTANCE_HOOK(Title2660ReadOnlyUpdateHook,
                      ll::memory::HookPriority::Normal,
                      MinecraftGame, &MinecraftGame::$update, void) {
    NativeHookLifetime::Lease lease(lifetime);
    origin();
    if (lease.admitted()) {
        updateSerial.fetch_add(1, std::memory_order_relaxed);
        tick(*this);
    }
}
// This is a pure observer: always delegates the exact argument ownership to
// Minecraft. It cannot initiate a setter call or change its return value.
LL_TYPE_INSTANCE_HOOK(Title2660ReadOnlySetHook,
                      ll::memory::HookPriority::Normal,
                      ResourcePackManager, &ResourcePackManager::setStack,
                      bool, std::unique_ptr<ResourcePackStack> stack,
                      ResourcePackStackType type) {
    NativeHookLifetime::Lease lease(lifetime);
    bool const observed=lease.admitted() && type==ResourcePackStackType::Global;
    if(observed){
        setterGlobalCalls.fetch_add(1,std::memory_order_acq_rel);
        lastGlobalSetManager.store(reinterpret_cast<std::uintptr_t>(this),
                                   std::memory_order_release);
    }
    auto accepted=origin(std::move(stack),type);
    if(observed && accepted) setterGlobalAccepted.fetch_add(1,std::memory_order_acq_rel);
    return accepted;
}
LL_TYPE_INSTANCE_HOOK(Title2660ReadOnlyActiveHook,
                      ll::memory::HookPriority::Normal,
                      MinecraftGame, &MinecraftGame::_onActiveResourcePacksChanged,
                      void, ResourcePackManager& manager, bool fullVanilla) {
    NativeHookLifetime::Lease lease(lifetime);
    // The finish callback can run *inside* origin() onActive; mark active
    // at the native entry boundary rather than after the entire nested call.
    if(lease.admitted() && nativeReceiptThread.load(std::memory_order_acquire)==GetCurrentThreadId()
       && nativeReceipt.isInNativeCall())
        nativeReceipt.onActive(static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&manager)));
    origin(manager, fullVanilla);
    if (lease.admitted()) {
        lastActiveManager.store(reinterpret_cast<std::uintptr_t>(&manager),std::memory_order_release);
        activeSerial.fetch_add(1,std::memory_order_acq_rel);
    }
}
LL_TYPE_INSTANCE_HOOK(Title2660ReadOnlyFinishHook,
                      ll::memory::HookPriority::Normal,
                      MinecraftGame, &MinecraftGame::_finishReloadingResources,
                      void, ResourcePackManager& manager, bool fullVanilla) {
    NativeHookLifetime::Lease lease(lifetime);
    origin(manager, fullVanilla);
    if(lease.admitted() && nativeReceiptThread.load(std::memory_order_acquire)==GetCurrentThreadId()
       && nativeReceipt.isInNativeCall())
        nativeReceipt.onFinish(static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&manager)));
    if (lease.admitted()) {
        lastFinishManager.store(reinterpret_cast<std::uintptr_t>(&manager),std::memory_order_release);
        finishSerial.fetch_add(1,std::memory_order_acq_rel);
    }
}
} // namespace

bool installTitlePackSwitch() noexcept {
    if (attempted.exchange(true)) return installed.load();
    try {
        if (!lifetime.prepare(pin)) return false;
        lifetime.registrationStarted();
        if (Title2660ReadOnlyUpdateHook::hook() != 0 ||
            Title2660ReadOnlySetHook::hook() != 0 ||
            Title2660ReadOnlyActiveHook::hook() != 0 ||
            Title2660ReadOnlyFinishHook::hook() != 0 ||
            !lifetime.open()) {
            lifetime.close();
            return false;
        }
        installed.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        lifetime.close();
        return false;
    }
}
bool stopTitlePackSwitch() noexcept {
    installed.store(false, std::memory_order_release);
    titlePackControl().close();
    lifetime.close();
    return lifetime.readers() == 0;
}
// Strict fail-closed gate for native pack apply, even when title observations
// succeed. The old 26.51 executable hash is NOT a 26.60 ABI proof.
bool resourcePackImageVerified() noexcept { return false; }
} // namespace praxis::companion::packs
