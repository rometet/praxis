// Owned font configuration for the headless Map presentation test. Keep core
// Settings out of the TU that includes MapRuntime.cpp's private Map Settings.
#include "runtime/Runtime.h"
namespace praxis::companion {
RuntimeConfig runtimeConfigSnapshot() { return {}; }
void logGuiError(char const*) noexcept {}
}
