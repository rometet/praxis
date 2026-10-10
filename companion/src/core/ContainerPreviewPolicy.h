#pragma once
#include "core/ShulkerPreviewPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <limits>
#include <optional>
namespace praxis::companion {
enum class PreviewFamily { None, Shulker, Bundle };
inline PreviewFamily previewFamily(std::string_view name) noexcept {
    if(shulkerIdentity(name))return PreviewFamily::Shulker;
    if(name.starts_with("minecraft:"))name.remove_prefix(10);
    if(name=="bundle"||(name.size()>7&&name.ends_with("_bundle")))return PreviewFamily::Bundle;
    return PreviewFamily::None;
}
struct PreviewGrid { int columns{},rows{},cells{}; bool operator==(PreviewGrid const&) const=default; };
inline PreviewGrid bundleGrid(unsigned entries) noexcept {
    entries=std::min(entries,64u);
    int columns=std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(entries)))),3,8);
    int rows=std::max(1,(static_cast<int>(entries)+columns-1)/columns);
    return {columns,rows,columns*rows};
}
inline PreviewGrid containerGrid(PreviewFamily family,unsigned entries) noexcept {
    return family==PreviewFamily::Shulker?PreviewGrid{9,3,27}:bundleGrid(entries);
}
inline float containerPreviewScale(PreviewGrid grid,float width,float height,float requested) noexcept {
    float const panelWidth=grid.columns*72.0f+(grid.columns-1)*6.0f+16.0f;
    float const panelHeight=grid.rows*72.0f+(grid.rows-1)*6.0f+24.0f+6.0f+18.0f;
    float fit=2.0f;
    if(width>0&&height>0)fit=std::min(width*0.92f/panelWidth,height*0.86f/panelHeight);
    // Preserve the proven Shulker minimum. Large variable Bundle grids may shrink to fit a small viewport.
    float minimum=grid==PreviewGrid{9,3,27}?0.75f:0.15f;
    return std::clamp(std::min(requested*1.08f,fit),minimum,2.0f);
}
inline bool validatePreviewEntry(std::array<bool,64>& seen,int slot,int count) noexcept {
    if(slot<0||slot>=64||count<=0||count>127||seen[slot])return false;
    seen[slot]=true;return true;
}
inline bool preferContainerPreview(bool valid,bool fromHotbar,bool sameGeneration,long long ageMs) noexcept {
    return valid&&!fromHotbar&&sameGeneration&&ageMs>=0&&ageMs<=220;
}
enum class InspectionVisit { Defer, Retire, Refresh };
inline InspectionVisit inspectionVisit(bool ownerAlive,bool sameScreen,bool slotMatches,bool hoverShown) noexcept {
    if(!ownerAlive)return InspectionVisit::Retire;
    if(!sameScreen)return InspectionVisit::Defer;
    return slotMatches&&hoverShown?InspectionVisit::Refresh:InspectionVisit::Retire;
}
// Shared across an entire decode, including every item's nested user data.
// Budget checks happen before native hash/copy/fromTag calls.
struct PreviewNbtBudget {
    static constexpr unsigned maximumDepth=16;
    static constexpr std::size_t maximumNodes=4096,maximumBytes=512*1024;
    std::size_t nodes{},bytes{};
    bool consume(unsigned depth,std::size_t payloadBytes=0) noexcept {
        if(depth>maximumDepth||nodes>=maximumNodes||payloadBytes>maximumBytes-bytes)return false;
        ++nodes;bytes+=payloadBytes;return true;
    }
    bool arrayBytes(std::size_t elements,std::size_t elementSize) noexcept {
        if(elementSize&&elements>(maximumBytes-bytes)/elementSize)return false;
        bytes+=elements*elementSize;return true;
    }
};
inline std::optional<std::uint32_t> bundleDynamicId(std::int64_t value) noexcept {
    if(value<0||static_cast<std::uint64_t>(value)>std::numeric_limits<std::uint32_t>::max())return std::nullopt;
    return static_cast<std::uint32_t>(value);
}
inline bool previewIconThreadAllowed(std::uint32_t owner,std::uint32_t current) noexcept {
    return owner==0||owner==current;
}
inline bool inspectionRefreshDue(std::uint64_t last,std::uint64_t now) noexcept {
    return last==0||(now>=last&&now-last>=100);
}
} // namespace praxis::companion
