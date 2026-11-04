#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace praxis::companion {

// Owned-only snapshot: borrowed Minecraft Block pointers never cross callbacks.
// Serialized block identity includes every property but not world coordinates.
struct BlockStateSample {
    std::uint64_t generation{}, observedMs{};
    std::string label, serialized, full;
};
enum class BlockStateComparison {Unavailable,Same,Different};
inline std::optional<BlockStateSample> parseBlockStateSample(std::string_view raw,
                                                              std::uint64_t generation,
                                                              std::uint64_t timeMs) {
    if(raw.empty()||raw.size()>4096)return std::nullopt;
    auto const split=raw.find('\n');
    if(split==std::string_view::npos||split==0||split+1>=raw.size()
        ||raw.substr(0,split).find(" @ ")==std::string_view::npos)return std::nullopt;
    return BlockStateSample{generation,timeMs,std::string(raw.substr(0,split)),
                            std::string(raw.substr(split+1)),std::string(raw)};
}
inline bool recentBlockStateSample(BlockStateSample const& sample,std::uint64_t generation,
                                   std::uint64_t now,std::uint64_t maxAgeMs=2500) noexcept {
    return sample.generation==generation && now>=sample.observedMs
        && now-sample.observedMs<=maxAgeMs && !sample.serialized.empty();
}
inline BlockStateComparison compareBlockStates(BlockStateSample const* reference,
                                                BlockStateSample const* current) noexcept {
    if(!reference||!current||reference->generation!=current->generation
       ||reference->serialized.empty()||current->serialized.empty())return BlockStateComparison::Unavailable;
    return reference->serialized==current->serialized
        ?BlockStateComparison::Same:BlockStateComparison::Different;
}
} // namespace praxis::companion
