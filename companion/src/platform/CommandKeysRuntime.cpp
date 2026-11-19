#include "platform/CommandKeysRuntime.h"
#include "core/CommandKeysPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/CommandRequestPacket.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CurrentCmdVersion.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace praxis::companion {
namespace {
std::mutex mutex;
CommandKeyScheduler scheduler;
std::vector<bool> previousDown;
std::array<ll::event::ListenerPtr, 2> listeners;
std::atomic_bool installed{};
std::string status{"CommandKeys disabled until profile explicitly applied"};
std::uint64_t priorGeneration{};
bool requireKeyRelease{true};

bool foreground() noexcept {
    DWORD pid{};
    auto hwnd = GetForegroundWindow();
    return hwnd && GetWindowThreadProcessId(hwnd, &pid)
        && pid == GetCurrentProcessId();
}
unsigned modifiers() noexcept {
    return modifierBits((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,
                        (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,
                        (GetAsyncKeyState(VK_MENU)&0x8000)!=0);
}
bool safeKey(unsigned key) noexcept {
    return key >= 0x30 && key <= 0x5a
        && key != VK_F10 && key != VK_F11;
}
bool sendAction(ClientInstance& client, LocalPlayer& player,
                CommandKeyAction const& action) {
    if (action.kind != CommandKeyActionKind::SendChat
        || !validCommandKeyText(action.text)) return false;
    if (action.text.front() == '/') {
        // Construct a vanilla command request with the actual local player's
        // origin, rather than sending a forged/guessed command byte payload.
        auto origin = std::make_unique<PlayerCommandOrigin>(
            player.getLevel(), player.getOrCreateUniqueID());
        CommandContext context(
            action.text, std::move(origin), static_cast<int>(CurrentCmdVersion::Latest));
        CommandRequestPacketPayload payload(context, false);
        CommandRequestPacket packet(std::move(payload));
        client.getPacketSender().sendToServer(packet);
    } else {
        auto packet = TextPacketPayload::createChat(
            player.getNameTag(), action.text, std::nullopt, "", "");
        client.getPacketSender().sendToServer(packet);
    }
    return true;
}
void resetLocked() noexcept {
    scheduler.onWorldExit();
    for (std::size_t i=0;i<previousDown.size();++i) previousDown[i]=false;
    priorGeneration=0;
    requireKeyRelease=true;
}
void tick() {
    std::lock_guard lock(mutex);
    if (!installed.load(std::memory_order_acquire)) return;
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    bool const gameplay = runtimeReady.load(std::memory_order_acquire)
        && inputAllowed.load(std::memory_order_acquire)
        && !guiInputCaptured() && foreground() && client && player
        && player->isAlive() && client->isInGameInputEnabled();
    auto const generation = worldGeneration.load(std::memory_order_acquire);
    if (priorGeneration && generation != priorGeneration) resetLocked();
    priorGeneration = generation;
    auto const now = GetTickCount64();
    CommandKeyContext context{generation,gameplay,false};
    auto const macros = scheduler.macros();
    if (previousDown.size() != macros.size()) previousDown.assign(macros.size(),false);
    auto const physicalModifiers = modifiers();
    bool anyDown=false;
    for (std::size_t i=0;i<macros.size();++i) {
        auto const& chord=macros[i].hotkey;
        bool const down=(GetAsyncKeyState(static_cast<int>(chord.key))&0x8000)!=0;
        anyDown |= down;
        if (!down) scheduler.release(i);
        bool const newlyDown=down && !previousDown[i];
        previousDown[i]=down; // Keep key history even when the game is unfocused.
        if (gameplay && !requireKeyRelease && newlyDown
            && modifiersMatch(chord.modifiers,physicalModifiers))
            scheduler.activate(i,context,now);
    }
    if (!anyDown) requireKeyRelease=false;
    if (!gameplay) { scheduler.cancel(); return; }
    if (auto action=scheduler.poll(context,now)) {
        if (!sendAction(*client,*player,*action)) {
            scheduler.cancel();
            status = "CommandKeys: unsupported action rejected";
        } else {
            status = "CommandKeys: sent vanilla packet for " + macros[action->macroIndex].name
                + " (server execution not yet confirmed)";
        }
    }
}
void exitWorld(ll::event::ClientExitLevelEvent&) {
    std::lock_guard lock(mutex);
    resetLocked();
    status="CommandKeys stopped on world exit";
}
} // namespace
bool configureCommandKeys(std::vector<CommandKeyMacro> macros) {
    if (!validCommandKeyProfile(macros)) return false;
    for (auto const& item : macros)
        if (!safeKey(item.hotkey.key) || item.mode==CommandKeyMode::Type) return false;
    std::lock_guard lock(mutex);
    if (!scheduler.configure(std::move(macros))) return false;
    previousDown.assign(scheduler.macros().size(),false);
    requireKeyRelease=true;
    status = scheduler.macros().empty() ? "CommandKeys disabled"
        : "CommandKeys active in this session; commands dispatch only on new key press";
    return true;
}
std::vector<CommandKeyMacro> commandKeysProfile() {
    std::lock_guard lock(mutex);
    auto macros=scheduler.macros();
    return {macros.begin(), macros.end()};
}
std::string commandKeysStatus() {
    std::lock_guard lock(mutex);
    return status;
}
void resetCommandKeysRuntime() noexcept {
    std::lock_guard lock(mutex);
    resetLocked();
}
bool startCommandKeysRuntime() {
    if (installed.exchange(true)) return true;
    try {
        auto& bus=ll::event::EventBus::getInstance();
        listeners[0]=bus.emplaceListener<ll::event::ClientLevelTickEvent>(
            [](ll::event::ClientLevelTickEvent&) { try{tick();}catch(...){} });
        listeners[1]=bus.emplaceListener<ll::event::ClientExitLevelEvent>(
            [](ll::event::ClientExitLevelEvent& event) { try{exitWorld(event);}catch(...){} });
        if (listeners[0]&&listeners[1]) return true;
    } catch (...) {}
    stopCommandKeysRuntime();
    return false;
}
bool stopCommandKeysRuntime() noexcept {
    installed.store(false);
    resetCommandKeysRuntime();
    bool ok=true;
    for (auto& listener:listeners) if(listener) {
        if (!ll::event::EventBus::getInstance().removeListener(listener)) ok=false;
        else listener.reset();
    }
    return ok;
}
} // namespace praxis::companion
