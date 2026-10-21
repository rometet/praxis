#pragma once
#include "core/PlayerHudPolicy.h"
#include "core/InspectorPolicy.h"
#include "core/HudLayoutPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace praxis::companion {
inline constexpr std::array<char const*,23> informationLineNames{
    "Coordinates","Scaled coordinates","Dimension","Biome","Difficulty","Facing",
    "Yaw","Pitch","Sprinting","FPS (HUD host)","Frame time (HUD host)","Light","Ping",
    "Rotation","Block","Chunk","Speed","Horizontal speed","Vertical speed",
    "Time","Real time","Weather","Moon"};
struct InformationOptions {
    PlayerHudOptions playerHud{};
    HudLayoutOptions hudLayouts{};
    bool infoHud{};
    bool durabilityHud{};
    bool bundlePreview{};
    bool durabilityInformation{};
    bool advancedTooltips{};
    InspectorOptions inspector{};
    bool bundleShowInHotbar{true};
    bool bundleShowEmpty{true};
    bool bundleRequireShift{};
    bool bundleShowItemCount{true};
    bool bundleShowSlotNumbers{};
    float bundleScale{1.0f};
    bool showUnavailable{};
    bool durabilityShowFull{true};
    // 0: bar + number, 1: number, 2: bar.
    int durabilityMode{};
    float x{12.0f}, y{80.0f}, scale{1.0f};
    float durabilityX{12.0f},durabilityY{300.0f};
    std::uint32_t textColor{0xFFF2F2F2};
    std::array<bool,23> lines{true,false,true,false,false,true,false,false,false,true,
        false,false,false,false,false,true,false,false,false,false,false,false,false};
    bool operator==(InformationOptions const&) const = default;
};
inline bool validInformationOptions(InformationOptions const& o) noexcept {
    return validHudLayouts(o.hudLayouts)&&validPlayerHudOptions(o.playerHud)&&std::isfinite(o.x)&&std::isfinite(o.y)&&o.x>=0&&o.y>=0&&o.x<=16384&&o.y<=16384
        &&std::isfinite(o.durabilityX)&&std::isfinite(o.durabilityY)
        &&o.durabilityX>=0&&o.durabilityY>=0&&o.durabilityX<=16384&&o.durabilityY<=16384
        &&std::isfinite(o.scale)&&o.scale>=0.5f&&o.scale<=3.0f
        &&std::isfinite(o.bundleScale)&&o.bundleScale>=0.75f&&o.bundleScale<=2.0f
        &&o.durabilityMode>=0&&o.durabilityMode<=2;
}
struct DurabilityValue {
    std::string name;
    int remaining{}, maximum{};
    float ratio{};
    bool operator==(DurabilityValue const&) const = default;
};
inline std::optional<DurabilityValue> durabilityValue(std::string name,int damage,int maximum) {
    if(maximum<=0||damage<0)return std::nullopt;
    int const remaining=maximum-std::min(damage,maximum);
    return DurabilityValue{std::move(name),remaining,maximum,static_cast<float>(remaining)/maximum};
}
inline std::string durabilityText(DurabilityValue const& value) {
    return std::to_string(value.remaining)+" / "+std::to_string(value.maximum)+" ("
        +std::to_string(static_cast<int>(std::round(value.ratio*100)))+"%)";
}
struct InformationSnapshot {
    std::uint64_t generation{}, observedMs{};
    bool valid{};
    FoodSnapshot food{};
    InspectorSnapshot inspector{};
    float logicalWidth{},logicalHeight{};
    std::array<std::string,23> values;
    // Main hand, offhand, helmet, chestplate, leggings, boots. Plain owned data.
    std::array<std::optional<DurabilityValue>,6> durability;
};
inline bool informationCurrent(InformationSnapshot const& s,std::uint64_t generation,std::uint64_t now) noexcept {
    return s.valid&&s.generation==generation&&now>=s.observedMs&&now-s.observedMs<=1000;
}
inline std::vector<std::string> formatInformationLines(InformationSnapshot const& s,InformationOptions const& o) {
    std::vector<std::string> result;
    for(std::size_t i=0;i<o.lines.size();++i)
        if(o.lines[i]&&(!s.values[i].empty()||o.showUnavailable))
            result.emplace_back(std::string(informationLineNames[i])+": "+(s.values[i].empty()?"Unavailable":s.values[i]));
    return result;
}
inline std::vector<std::pair<unsigned,DurabilityValue>> selectDurabilityRows(InformationSnapshot const& s,InformationOptions const& o) {
    std::vector<std::pair<unsigned,DurabilityValue>> result;
    for(unsigned i=0;i<s.durability.size();++i)
        if(s.durability[i]&&(o.durabilityShowFull||s.durability[i]->remaining<s.durability[i]->maximum))
            result.emplace_back(i,*s.durability[i]);
    return result;
}
inline std::optional<int> informationBlock(double value) noexcept {
    if(!std::isfinite(value)||value<-2147483648.0||value>=2147483648.0)return std::nullopt;
    return static_cast<int>(std::floor(value));
}
inline int informationChunk(int block) noexcept { return block/16-(block<0&&block%16!=0); }
inline std::optional<double> dimensionCoordinateScale(int dimension) noexcept {
    if(dimension==0)return 0.125;
    if(dimension==1)return 8.0;
    return std::nullopt;
}
struct DurabilityHudLayout { float x{},y{},width{},height{},scale{}; };
inline DurabilityHudLayout durabilityHudLayout(unsigned rows,float width,float height,InformationOptions const& o) noexcept {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!validInformationOptions(o))return {};
    rows=std::min(rows,6u);
    float baseHeight=16+rows*42.0f;
    auto const& transform=o.hudLayouts.panels[static_cast<unsigned>(HudId::Equipment)];
    float scale=std::min({transform.custom?transform.scale:o.scale,width/324.0f,height/baseHeight});
    float w=316*scale,h=baseHeight*scale;
    return {std::clamp(o.durabilityX,0.0f,std::max(0.0f,width-w)),
        std::clamp(o.durabilityY,0.0f,std::max(0.0f,height-h)),w,h,scale};
}
} // namespace praxis::companion
