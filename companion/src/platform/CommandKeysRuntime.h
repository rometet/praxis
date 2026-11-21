#pragma once
#include "core/CommandKeysOptions.h"
#include <string>
#include <vector>

namespace praxis::companion {
// Process-local, explicit session opt-in. Never changes numeric Feature slots.
bool configureCommandKeys(std::vector<CommandKeyMacro> macros);
std::vector<CommandKeyMacro> commandKeysProfile();
std::string commandKeysStatus();
bool startCommandKeysRuntime();
bool stopCommandKeysRuntime() noexcept;
void resetCommandKeysRuntime() noexcept;
} // namespace praxis::companion
