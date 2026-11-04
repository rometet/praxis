#pragma once
// Only the unavailable LeviLamina serialization/file wrappers are substituted.
// The production queue, thread, failure retention and Win32 atomic replacement run unchanged.
#include "support/SdkCodecApi.h"
#include <fstream>
#include <sstream>
namespace ll {
namespace file_utils {
inline std::optional<std::string> readFile(std::filesystem::path const& p) {
    std::ifstream file(p,std::ios::binary); if(!file)return {};
    std::ostringstream stream; stream<<file.rdbuf(); return stream.str();
}
}
namespace config {
inline bool saveConfig(praxis::companion::RuntimeConfig const& c,std::filesystem::path const& p) {
    std::ofstream file(p,std::ios::binary|std::ios::trunc);
    file<<reflection::serialize<nlohmann::ordered_json>(c).value().dump(2); file.close(); return bool(file);
}
}
}
