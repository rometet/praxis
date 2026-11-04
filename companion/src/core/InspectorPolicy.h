#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace praxis::companion {
enum class InformationView {Defer,Retire,Collect};
inline InformationView informationView(std::string_view screen,std::string_view top) noexcept {
    if(top!="hud_screen"&&top!="in_game_play_screen")return InformationView::Retire;
    return screen=="hud_screen"||screen=="in_game_play_screen"?InformationView::Collect:InformationView::Defer;
}
struct InspectorOptions {
    bool blockState{},tags{},fluid{},entity{},memory{};
    bool operator==(InspectorOptions const&) const = default;
};
inline bool inspectorNeeded(InspectorOptions const& o) noexcept {return o.blockState||o.tags||o.fluid||o.entity||o.memory;}
struct InspectorSnapshot {
    // Plain client-visible owned strings. Empty means unknown/unavailable,
    // never an inferred state. No Actor, Block, NBT or SDK pointer publication.
    std::array<std::string,4> looking;
    std::string memory;
};
inline std::string boundedInspectorText(std::string value,std::size_t maximum=4096) {
    return value.size()<=maximum?std::move(value):std::string{};
}
inline std::vector<std::string> inspectorLinesJa(InspectorSnapshot const& s,InspectorOptions const& o) {
    std::vector<std::string> out;std::array selected{o.blockState,o.tags,o.fluid,o.entity};
    constexpr std::array labels{"見ているブロック状態","ブロックタグ (端末)","液体層 (端末)","見ているエンティティ"};
    for(unsigned i=0;i<4;++i)if(selected[i]) {
        auto value=s.looking[i].empty()?std::string("不明"):s.looking[i];
        // A clipboard value may contain structured line breaks; a HUD row is
        // one visual line so its measured height stays correct.
        std::replace(value.begin(),value.end(),'\n',' ');std::replace(value.begin(),value.end(),'\r',' ');
        out.emplace_back(std::string(labels[i])+": "+value);
    }
    if(o.memory)out.emplace_back("メモリ (プロセス常駐): "+(s.memory.empty()?std::string("不明"):s.memory));
    return out;
}
inline std::string inspectorCopyText(InspectorSnapshot const& s,unsigned row,bool current) {
    return current&&row<s.looking.size()?s.looking[row]:std::string{};
}
inline std::string advancedItemText(std::string type,int auxiliary,unsigned count,int damage,int maximum) {
    if(type.empty()||type.size()>512||count>255)return {};
    std::string text="ID: "+type+"\n補助値: "+std::to_string(auxiliary)+" / 個数: "+std::to_string(count);
    if(maximum>0&&damage>=0)text+="\n耐久値: "+std::to_string(maximum-std::min(damage,maximum))+" / "+std::to_string(maximum);
    return text;
}
}
