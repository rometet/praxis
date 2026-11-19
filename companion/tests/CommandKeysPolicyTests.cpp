#include "core/CommandKeysPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace praxis::companion;
namespace {
CommandKeyMacro makeMacro(CommandKeyMode mode, std::vector<std::string> payloads,
                          unsigned repeatCount = 1, unsigned key = 'K') {
    CommandKeyMacro m;
    m.name = "example";
    m.hotkey = {key, anyModifiers, 1};
    m.mode = mode;
    m.payloads = std::move(payloads);
    m.repeats = repeatCount;
    m.delayMs = 200;
    return m;
}
}
int main() {
    unsigned checks=0;
    auto require=[&](bool good, char const* why) {
        ++checks;
        if (!good) throw std::runtime_error(why);
    };
    try {
        CommandKeyScheduler runner;
        CommandKeyContext world{7,true,false};
        CommandKeyContext chat{7,true,true};
        require(!runner.activate(0,world,100), "no configuration");
        auto send=makeMacro(CommandKeyMode::Send,{"/say first","/say second"});
        require(validCommandKeyMacro(send), "valid send macro");
        require(runner.configure({send}), "configure one macro");
        require(!runner.activate(0,chat,100), "cannot activate while typing");
        require(!runner.activate(0,{0,true,false},100), "must be in world");
        require(!runner.activate(0,{7,false,false},100), "gameplay focus required");
        require(runner.activate(0,world,100), "starts on explicit key edge");
        require(!runner.activate(0,world,100), "cannot start twice");
        auto first=runner.poll(world,100);
        require(first && first->kind==CommandKeyActionKind::SendChat && first->text=="/say first", "first action");
        require(runner.active(), "multi step continues");
        require(!runner.poll(world,299), "no premature send");
        require(!runner.poll(chat,301), "chat focus change stops");
        require(!runner.active(), "cancel on chat focus");
        require(runner.activate(0,world,400), "restart send");
        require(runner.poll(world,400)->text=="/say first", "restart first payload");
        require(!runner.poll(world,599), "enforced 200ms spacing");
        require(runner.poll(world,600)->text=="/say second", "second payload at 200ms");
        require(!runner.active(), "done after send sequence");
        require(!runner.poll(world,700), "no duplicate after complete");
        require(runner.activate(0,world,900), "can activate again");
        runner.release(0);
        require(runner.poll(world,900).has_value(), "one-shot send survives release");
        runner.onWorldExit();
        require(!runner.poll(world,2000), "world exit cancels");

        auto cycle=makeMacro(CommandKeyMode::Cycle,{"one","two","three"});
        require(runner.configure({cycle}), "configure cycle");
        for (int i=0; i<7; ++i) {
            require(runner.activate(0,world,1000+i*200), "cycle activation");
            auto out=runner.poll(world,1000+i*200);
            require(out && out->text==std::vector<std::string>{"one","two","three"}[i%3], "cycle rotates");
            require(!runner.active(), "cycle finishes after one");
        }
        require(runner.configure({cycle}), "reconfigure resets cycle");
        require(runner.activate(0,world,5000) && runner.poll(world,5000)->text=="one", "cycle resets");

        auto repeat=makeMacro(CommandKeyMode::Repeat,{"a","b"},3);
        require(runner.configure({repeat}), "configure repeat");
        require(runner.activate(0,world,1000), "begin repeat");
        for (int i=0;i<6;++i) {
            require(!runner.poll(world,999), "earlier clock never fires");
            auto action=runner.poll(world,1000+i*200);
            require(action && action->text==(i%2?"b":"a"), "repeat sequence");
            if (i!=5) require(runner.active(), "repeat not complete early");
        }
        require(!runner.active(), "repeat completed at count");
        require(runner.activate(0,world,3000), "new repeat activation");
        require(runner.poll(world,3000).has_value(), "new repeat first action");
        runner.release(0);
        require(!runner.active() && !runner.poll(world,4000), "release stops repeat");
        require(runner.activate(0,world,5000), "activate again");
        runner.cancel();
        require(!runner.poll(world,5000), "explicit cancel prevents emission");
        require(runner.activate(0,world,6000), "restart after cancel");
        require(!runner.poll({8,true,false},6000), "cross-world generation stops");
        require(!runner.active(), "world switch cancellation");

        auto type=makeMacro(CommandKeyMode::Type,{"text only"});
        require(runner.configure({type}), "configure type");
        require(runner.activate(0,world,10), "activate type");
        auto action=runner.poll(world,10);
        require(action && action->kind==CommandKeyActionKind::TypeText && action->text=="text only", "text not auto-sent");
        require(!runner.active(), "type ends");

        auto bad=send;
        bad.payloads={"first\nsecond"};
        require(!validCommandKeyMacro(bad), "newline blocked");
        bad=send;bad.payloads={"x\r"};
        require(!validCommandKeyMacro(bad), "carriage return blocked");
        bad=send;bad.payloads={std::string(257,'a')};
        require(!validCommandKeyMacro(bad), "oversized text blocked");
        bad=send;bad.delayMs=99;
        require(!validCommandKeyMacro(bad), "short delay blocked");
        bad=send;bad.delayMs=60001;
        require(!validCommandKeyMacro(bad), "long delay blocked");
        bad=repeat;bad.repeats=65;
        require(!validCommandKeyMacro(bad), "excessive repeats blocked");
        bad=repeat;bad.payloads=std::vector<std::string>(8,"x");bad.repeats=64;
        require(!validCommandKeyMacro(bad), "action budget blocks 512 sends");
        bad=type;bad.payloads={"one","two"};
        require(!validCommandKeyMacro(bad), "type has one payload");
        bad=send;bad.hotkey.modifiers=9;
        require(!validCommandKeyMacro(bad), "invalid modifier blocked");

        auto other=makeMacro(CommandKeyMode::Send,{"hi"},1,'K');
        other.name="second";
        require(!validCommandKeyProfile(std::vector<CommandKeyMacro>{send,other}), "hotkey conflicts rejected");
        other.conflict=CommandKeyConflictPolicy::Both;
        auto both=send; both.conflict=CommandKeyConflictPolicy::Both;
        require(validCommandKeyProfile(std::vector<CommandKeyMacro>{both,other}), "explicit both conflict allowed");
        other.hotkey.key='J';
        require(validCommandKeyProfile(std::vector<CommandKeyMacro>{send,other}), "distinct hotkeys allowed");
        other.name="example";
        require(!validCommandKeyProfile(std::vector<CommandKeyMacro>{send,other}), "duplicate names disallowed");

        require(!runner.configure({bad}), "invalid new config rejected");
        require(runner.macros().size()==1, "rejected config does not overwrite previous");
        require(!runner.activate(0,world,std::numeric_limits<std::uint64_t>::max()), "time overflow blocked");
        std::cout << "CommandKeys policy checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "CommandKeys policy failed: " << error.what() << '\n';
        return 1;
    }
}
