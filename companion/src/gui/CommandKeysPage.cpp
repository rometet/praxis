#include "gui/CommandKeysPage.h"
#include "platform/CommandKeysRuntime.h"
#include "core/CommandKeysOptions.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace praxis::companion {
void drawCommandKeysPage() {
    static std::vector<CommandKeyMacro> editing;
    static bool initialized{};
    static std::string message;
    if (!initialized) { editing = commandKeysProfile(); initialized = true; }
    ImGui::TextUnformatted("CommandKeys (experimental)");
    ImGui::TextWrapped("Explicit action required. Disabled by default. This profile is session-only: it is NOT saved to disk.");
    ImGui::TextWrapped("Commands use vanilla client request packets on the active game thread. Server execution and permissions are not confirmed until tested in-game.");
    if (ImGui::Button("Disable all command macros")) {
        if (configureCommandKeys({})) message = "Disabled for this session";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload applied profile")) {
        editing = commandKeysProfile();
        message.clear();
    }
    ImGui::SeparatorText("Edit macros (not active until Apply)");
    if (editing.size() < commandKeyMacroMax && ImGui::Button("Add macro")) {
        CommandKeyMacro macro;
        macro.name = "Macro " + std::to_string(editing.size() + 1);
        macro.hotkey = {static_cast<unsigned>('G'+std::min(editing.size(),std::size_t{18})),3u,1u};
        macro.mode = CommandKeyMode::Send;
        macro.payloads = {"/say hello"};
        editing.push_back(std::move(macro));
    }
    int remove = -1;
    for (std::size_t i=0;i<editing.size();++i) {
        auto& macro=editing[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(macro.name.empty() ? "(unnamed)" : macro.name.c_str());
        char name[commandKeyNameLengthMax+1]{};
        std::snprintf(name,sizeof(name),"%s",macro.name.c_str());
        if (ImGui::InputText("Name",name,sizeof(name))) macro.name=name;
        int key=static_cast<int>(macro.hotkey.key);
        if (ImGui::InputInt("Virtual key (VK)",&key)) macro.hotkey.key=static_cast<unsigned>(std::clamp(key,0,255));
        int modifiers=static_cast<int>(macro.hotkey.modifiers);
        if (ImGui::SliderInt("Modifiers (Ctrl=1,Shift=2,Alt=4,Any=8)",&modifiers,0,8))
            macro.hotkey.modifiers=static_cast<unsigned>(modifiers);
        int mode=static_cast<int>(macro.mode);
        if (ImGui::Combo("Mode",&mode,"Send\0Cycle\0Repeat\0\0")) {
            macro.mode=static_cast<CommandKeyMode>(mode);
            if (macro.mode != CommandKeyMode::Repeat) macro.repeats=1;
        }
        int delay=static_cast<int>(macro.delayMs);
        if (ImGui::SliderInt("Delay ms",&delay,100,5000)) macro.delayMs=static_cast<unsigned>(delay);
        if (macro.mode==CommandKeyMode::Repeat) {
            int repeats=static_cast<int>(macro.repeats);
            if (ImGui::SliderInt("Repeat limit",&repeats,1,64)) macro.repeats=static_cast<unsigned>(repeats);
            ImGui::Checkbox("Stop repeat on key release",&macro.repeatWhileHeld);
        }
        for (std::size_t j=0;j<macro.payloads.size();++j) {
            ImGui::PushID(static_cast<int>(j));
            char payload[commandKeyPayloadLengthMax+1]{};
            std::snprintf(payload,sizeof(payload),"%s",macro.payloads[j].c_str());
            if (ImGui::InputText("Command or chat text",payload,sizeof(payload)))
                macro.payloads[j]=payload;
            ImGui::PopID();
        }
        if (macro.payloads.size()<commandKeyPayloadsMax && ImGui::SmallButton("Add command line"))
            macro.payloads.push_back("/say hello");
        ImGui::SameLine();
        if (macro.payloads.size()>1 && ImGui::SmallButton("Remove last command"))
            macro.payloads.pop_back();
        if (ImGui::SmallButton("Remove this macro")) remove=static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove >= 0) editing.erase(editing.begin()+remove);
    bool valid = validCommandKeyProfile(editing);
    if (!valid) ImGui::TextDisabled("Invalid profile: check names, keys, conflicts and total 256-action budget");
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Apply and ENABLE these macros")) {
        message = configureCommandKeys(editing)
            ? "Applied. Return to gameplay and press the configured key."
            : "Rejected: reserved keys, unsupported mode or profile conflict.";
    }
    if (!valid) ImGui::EndDisabled();
    ImGui::TextWrapped("Edit result: %s",message.c_str());
    ImGui::TextWrapped("Live status: %s",commandKeysStatus().c_str());
    ImGui::TextDisabled("Supported runtime modes: Send / Cycle / Repeat. Type-to-chat is not implemented.");
}
} // namespace praxis::companion
