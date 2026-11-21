#pragma once
#include "core/CommandKeysOptions.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace praxis::companion {
struct CommandKeyContext {
    std::uint64_t worldGeneration{};
    bool gameplayFocused{};
    bool chatFocused{};
    bool operator==(CommandKeyContext const&) const = default;
};
enum class CommandKeyActionKind { SendChat, TypeText };
struct CommandKeyAction {
    CommandKeyActionKind kind{};
    std::string text;
    std::size_t macroIndex{};
};
class CommandKeyScheduler {
public:
    bool configure(std::vector<CommandKeyMacro> macros) {
        if (!validCommandKeyProfile(macros)) return false;
        cancel();
        macros_ = std::move(macros);
        cycleIndex_.assign(macros_.size(),0);
        return true;
    }
    std::span<CommandKeyMacro const> macros() const noexcept { return macros_; }
    bool active() const noexcept { return active_; }
    unsigned issued() const noexcept { return issued_; }

    // The caller owns the input edge detector, key/modifier matcher and
    // Minecraft GUI focus check. No actions run on process startup.
    bool activate(std::size_t index, CommandKeyContext const& context, std::uint64_t nowMs) noexcept {
        if (active_ || !usable(context) || index >= macros_.size()) return false;
        if (nowMs > std::numeric_limits<std::uint64_t>::max() - commandKeyDelayMaxMs) return false;
        auto const& macro = macros_[index];
        if (!validCommandKeyMacro(macro)) return false;
        active_ = true;
        held_ = true;
        worldGeneration_ = context.worldGeneration;
        index_ = index;
        step_ = 0;
        remainingCycles_ = macro.mode == CommandKeyMode::Repeat ? macro.repeats : 1;
        nextActionMs_ = nowMs;
        issued_ = 0;
        return true;
    }
    // Release cancels hold-to-repeat, but not an already authorized Send/Cycle/Type.
    void release(std::size_t index) noexcept {
        if (!active_ || index_ != index) return;
        held_ = false;
        auto const& macro = macros_[index_];
        if (macro.mode == CommandKeyMode::Repeat && macro.repeatWhileHeld) cancel();
    }
    void cancel() noexcept {
        active_ = false;
        held_ = false;
        step_ = remainingCycles_ = issued_ = 0;
    }
    void onWorldExit() noexcept { cancel(); worldGeneration_ = 0; }

    // Delivers at most one action per polling call; never catches up with
    // a burst of actions after lag. Sending or typing remains the adapter's job.
    std::optional<CommandKeyAction> poll(CommandKeyContext const& context, std::uint64_t nowMs) {
        if (!active_) return std::nullopt;
        if (!usable(context) || context.worldGeneration != worldGeneration_) {
            cancel();
            return std::nullopt;
        }
        if (nowMs < nextActionMs_) return std::nullopt;
        auto const& macro = macros_[index_];
        if (issued_ >= commandKeyRateActionsMax
            || (macro.mode == CommandKeyMode::Repeat && macro.repeatWhileHeld && !held_)) {
            cancel();
            return std::nullopt;
        }
        std::size_t const item = macro.mode == CommandKeyMode::Cycle
            ? cycleIndex_[index_] % macro.payloads.size() : step_;
        CommandKeyAction result {
            macro.mode == CommandKeyMode::Type ? CommandKeyActionKind::TypeText : CommandKeyActionKind::SendChat,
            macro.payloads[item], index_
        };
        ++issued_;
        if (macro.mode == CommandKeyMode::Cycle) {
            cycleIndex_[index_] = (cycleIndex_[index_] + 1) % macro.payloads.size();
            active_ = false;
        } else if (macro.mode == CommandKeyMode::Type) {
            active_ = false;
        } else {
            ++step_;
            if (step_ >= macro.payloads.size()) {
                step_ = 0;
                --remainingCycles_;
            }
            if (!remainingCycles_) {
                active_ = false;
            } else {
                nextActionMs_ = nowMs + macro.delayMs;
            }
        }
        return result;
    }
private:
    static bool usable(CommandKeyContext const& c) noexcept {
        return c.worldGeneration != 0 && c.gameplayFocused && !c.chatFocused;
    }
    std::vector<CommandKeyMacro> macros_;
    std::vector<std::size_t> cycleIndex_;
    std::size_t index_{};
    std::size_t step_{};
    std::uint64_t worldGeneration_{};
    std::uint64_t nextActionMs_{};
    unsigned remainingCycles_{};
    unsigned issued_{};
    bool active_{}, held_{};
};
} // namespace praxis::companion
