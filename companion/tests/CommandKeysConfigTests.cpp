#include "core/CommandKeysConfig.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace praxis::companion;
namespace {
void put(CommandKeysConfig& c, unsigned slot, std::string name, int key, std::string commands,
         unsigned modifiers = anyModifiers, unsigned mode = 0) {
    c.names[slot] = std::move(name);
    c.keys[slot] = key;
    c.modifiers[slot] = modifiers;
    c.modes[slot] = mode;
    c.commands[slot] = std::move(commands);
}
}
int main() {
    unsigned checks = 0;
    auto require = [&](bool good, char const* why) {
        ++checks;
        if (!good) throw std::runtime_error(why);
    };
    try {
        // 1) default profile: off and valid, nothing armed
        {
            CommandKeysConfig c;
            require(!c.enableOnStart, "default off");
            require(validCommandKeysConfig(c), "empty profile valid");
            require(commandKeysFromConfig(c).empty(), "no macros from an empty profile");
        }
        // 2) the harness profile: Ctrl+Shift+G sends the score command once
        {
            CommandKeysConfig c;
            c.enableOnStart = true;
            put(c, 0, "qa", 0x47, "/scoreboard players set @s dsh_qa 7",
                encodeModifiers(true, true, false));
            require(validCommandKeysConfig(c), "harness profile valid");
            auto macros = commandKeysFromConfig(c);
            require(macros.size() == 1, "one macro");
            require(macros[0].name == "qa" && macros[0].hotkey.key == 0x47, "name and key");
            require(macros[0].hotkey.modifiers == 3u, "ctrl+shift");
            require(macros[0].mode == CommandKeyMode::Send, "send mode");
            require(macros[0].payloads.size() == 1, "one step");
            require(macros[0].payloads[0] == "/scoreboard players set @s dsh_qa 7", "payload");
            require(!macros[0].repeatWhileHeld, "never repeats while held");
            require(validCommandKeyProfile(macros), "runtime accepts it");
        }
        // 3) a multi-step macro is a newline-joined command list
        {
            CommandKeysConfig c;
            put(c, 0, "two", 0x48, "/say one\n/say two");
            auto macros = commandKeysFromConfig(c);
            require(macros.size() == 1, "one macro");
            require(macros[0].payloads.size() == 2, "two steps");
            require(macros[0].payloads[1] == "/say two", "second step");
            require(validCommandKeyProfile(macros), "multi-step accepted");
        }
        // 4) several slots stay independent
        {
            CommandKeysConfig c;
            put(c, 0, "a", 0x41, "/say a");
            put(c, 3, "b", 0x42, "/say b");
            auto macros = commandKeysFromConfig(c);
            require(macros.size() == 2, "two macros");
            require(commandKeysConfigBindingCount(c) == 2, "count");
            require(validCommandKeysConfig(c), "two slots valid");
        }
        // 5) invalid slots fail closed
        {
            CommandKeysConfig a;
            put(a, 0, "", 0x47, "/say x");
            require(!validCommandKeysConfig(a), "empty name rejected");
            CommandKeysConfig b;
            put(b, 0, "k", 0, "/say x");
            require(!validCommandKeysConfig(b), "missing key rejected");
            CommandKeysConfig d;
            put(d, 0, "k", 0x47, "bad\nchar\x01");
            require(!validCommandKeysConfig(d), "control character rejected");
            CommandKeysConfig e;
            put(e, 0, "k", 0x47, "/say x\n/say y\n/say z\n/say w\n/say too many");
            require(!validCommandKeysConfig(e), "too many steps rejected");
            CommandKeysConfig f;
            put(f, 0, "k", 0x47, "/say x", 99);
            require(!validCommandKeysConfig(f), "bad modifiers rejected");
            CommandKeysConfig g;
            put(g, 0, "k", 0x47, "/say x", anyModifiers, 9);
            require(!validCommandKeysConfig(g), "bad mode rejected");
        }
        // 6) splitting is exact: no empty steps, order kept
        {
            auto steps = splitCommandKeys("/a\n\n/b\n");
            require(steps.size() == 2, "empty pieces dropped");
            require(steps[0] == "/a" && steps[1] == "/b", "order kept");
        }
        std::cout << checks << " CommandKeysConfig checks passed\n";
    } catch (std::exception const& e) {
        std::cerr << "CommandKeysConfig FAILED after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
