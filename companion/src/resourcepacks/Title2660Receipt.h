#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace praxis::companion::packs {

// CPU-only contract for a single 26.60 title Global-stack transaction.
// It does not call Minecraft; a native owner must explicitly establish the
// manager address, callback order, unchanged other stack layers, and exact
// identities from SDK-backed snapshots.
class Title2660Receipt {
public:
    enum class Phase {
        Idle, Prepared, SetterPending, Notifying, Settling,
        Complete, Rejected, RollbackRequired, Restored, RecoveryRequired
    };
    enum class Poll { Wait, Applied, NeedRollback, Restored, RecoveryRequired };

private:
    Phase state_{Phase::Idle};
    std::uint64_t token_{}, owner_{}, deadline_{}, lastTick_{};
    std::vector<std::string> original_, target_;
    bool inCall_{}, accepted_{}, rollback_{};
    unsigned active_{}, finished_{}, stable_{}, mismatchStreak_{};

    void resetCycle() noexcept {
        inCall_=false; accepted_=false; active_=finished_=stable_=mismatchStreak_=0;
        lastTick_=0;
    }
    std::vector<std::string> const& expected() const noexcept {
        return rollback_?original_:target_;
    }
    void unsafeExit() noexcept {
        state_ = rollback_ ? Phase::RecoveryRequired : Phase::RollbackRequired;
        inCall_=false;
    }

public:
    Phase phase() const noexcept { return state_; }
    std::uint64_t token() const noexcept { return token_; }
    std::uint64_t owner() const noexcept { return owner_; }
    bool isInNativeCall() const noexcept { return inCall_; }
    bool isRollback() const noexcept { return rollback_; }
    bool hasFinish() const noexcept { return finished_==1; }

    bool prepare(std::uint64_t token,std::uint64_t owner,
                 std::vector<std::string> oldIds,
                 std::vector<std::string> newIds,
                 std::uint64_t deadline,bool explicitReload=false,
                 bool allowEmptyStacks=false) {
        if(state_!=Phase::Idle || !token || !owner ||
           (!allowEmptyStacks && (oldIds.empty() || newIds.empty())) ||
           (!explicitReload && oldIds==newIds) || oldIds.size()>512 ||
           newIds.size()>512 || !deadline) return false;
        token_=token;owner_=owner;deadline_=deadline;
        original_=std::move(oldIds);target_=std::move(newIds);
        state_=Phase::Prepared;resetCycle();
        return true;
    }
    // A cancellation before the setter must never result in a native write.
    bool cancelBeforeSetter() noexcept {
        if(state_!=Phase::Prepared)return false;
        state_=Phase::Rejected;return true;
    }
    bool beginNativeCall(std::uint64_t owner,std::uint64_t now) noexcept {
        if(state_!=Phase::Prepared || inCall_ || owner!=owner_ ||
           now>deadline_) { if(state_==Phase::Prepared)unsafeExit();return false; }
        state_=Phase::SetterPending;inCall_=true;
        return true;
    }
    void setterResult(std::uint64_t owner,bool accepted) noexcept {
        if(state_!=Phase::SetterPending || !inCall_ || owner!=owner_) {
            unsafeExit();return;
        }
        accepted_=accepted;
        if(!accepted) {
            inCall_=false;
            state_=rollback_ ? Phase::RecoveryRequired : Phase::Rejected;
            return;
        }
        state_=Phase::Notifying;
    }
    // Called only from the native _onActiveResourcePacksChanged hook.
    void onActive(std::uint64_t owner) noexcept {
        if(!inCall_ || state_!=Phase::Notifying || owner!=owner_ ||
           !accepted_ || active_!=0 || finished_!=0) {unsafeExit();return;}
        ++active_;
    }
    // Called only from the native _finishReloadingResources hook.
    void onFinish(std::uint64_t owner) noexcept {
        if(!inCall_ || state_!=Phase::Notifying || owner!=owner_ ||
           !accepted_ || active_!=1 || finished_!=0) {unsafeExit();return;}
        ++finished_;
    }
    bool endNativeCall(std::uint64_t owner,std::uint64_t now) noexcept {
        if(state_!=Phase::Notifying || !inCall_ || !accepted_ ||
           owner!=owner_ || now>deadline_ ||
           active_!=1 || finished_!=1) {
            if(state_!=Phase::Rejected)unsafeExit();
            return false;
        }
        inCall_=false; state_=Phase::Settling;
        return true;
    }
    // Poll uses stable, owner-confirmed post-reload snapshots from DIFFERENT
    // game ticks; two reads of the same frame cannot certify a transaction.
    Poll poll(std::uint64_t owner,std::uint64_t tick,std::uint64_t now,
              std::vector<std::string> const& actual,
              bool titleReady,bool pending,bool otherLayersUnchanged) noexcept {
        if(state_==Phase::RecoveryRequired)return Poll::RecoveryRequired;
        if(state_==Phase::RollbackRequired)return Poll::NeedRollback;
        if(state_==Phase::Complete)return Poll::Applied;
        if(state_==Phase::Restored)return Poll::Restored;
        if(state_!=Phase::Settling)return Poll::Wait;
        if(owner!=owner_ || !titleReady || !otherLayersUnchanged ||
           now>deadline_) {unsafeExit();return state_==Phase::RecoveryRequired
               ?Poll::RecoveryRequired:Poll::NeedRollback;}
        if(pending || !tick || tick<=lastTick_)return Poll::Wait;
        lastTick_=tick;
        if(actual!=expected()) {
            stable_=0;
            if(++mismatchStreak_>=2){unsafeExit();return state_==Phase::RecoveryRequired
                ?Poll::RecoveryRequired:Poll::NeedRollback;}
            return Poll::Wait;
        }
        // A transient mismatch requires one recovery frame plus two further
        // identical snapshots; it can never be counted as a success frame.
        if(mismatchStreak_){mismatchStreak_=0;stable_=0;return Poll::Wait;}
        if(++stable_<2)return Poll::Wait;
        state_=rollback_?Phase::Restored:Phase::Complete;
        return rollback_?Poll::Restored:Poll::Applied;
    }
    // Request rollback only from a confirmed failed/incomplete apply, never
    // from a user cancellation after the first native setter.
    bool beginRollback(std::uint64_t owner,std::uint64_t deadline) noexcept {
        if(state_!=Phase::RollbackRequired || owner!=owner_ || !deadline)
            return false;
        rollback_=true;deadline_=deadline;resetCycle();state_=Phase::Prepared;
        return true;
    }
    void markRecoveryRequired() noexcept { state_=Phase::RecoveryRequired;inCall_=false; }
};

} // namespace praxis::companion::packs
