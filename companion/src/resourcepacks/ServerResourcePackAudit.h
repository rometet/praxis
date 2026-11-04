#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
namespace praxis::companion::packs {
// Read-only facts about active native stacks. A Level pack is not necessarily
// server-delivered; origin does not confer permission to alter the pack.
enum class ServerAssetLayer : unsigned {
    Unknown=0, Level=1, Global=2, Treatment=3, Base=4, Multiple=5
};
struct ServerResourcePackSnapshot {
    bool multiplayer{}, useGlobal{}, pending{};
    std::size_t level{}, global{}, treatment{}, base{}, full{};
    constexpr bool serverOrWorldResourceContext() const noexcept {
        return multiplayer||level>0;
    }
    constexpr bool safeToAttemptGlobalComposerInWorld() const noexcept {
        return !serverOrWorldResourceContext()&&!pending;
    }
};
inline ServerAssetLayer serverResourceAssetLayer(
        std::span<std::string const> full,
        std::span<std::string const> level,
        std::span<std::string const> global,
        std::span<std::string const> treatment,
        std::span<std::string const> base,
        int sourceIndex) noexcept {
    if(sourceIndex<0||static_cast<std::size_t>(sourceIndex)>=full.size())return ServerAssetLayer::Unknown;
    auto const& identity=full[static_cast<std::size_t>(sourceIndex)];
    // Do not claim a single provenance if two native stacks contain the same
    // identity. This is strictly a display/diagnostic classification.
    unsigned hits=0;
    ServerAssetLayer owner=ServerAssetLayer::Unknown;
    auto check=[&](std::span<std::string const> s,ServerAssetLayer layer) {
        if(std::find(s.begin(),s.end(),identity)!=s.end()){++hits;owner=layer;}
    };
    check(level,ServerAssetLayer::Level);
    check(global,ServerAssetLayer::Global);
    check(treatment,ServerAssetLayer::Treatment);
    check(base,ServerAssetLayer::Base);
    return hits>1?ServerAssetLayer::Multiple:hits==1?owner:ServerAssetLayer::Unknown;
}
inline char const* serverAssetLayerJa(ServerAssetLayer layer) noexcept {
    switch(layer) {
    case ServerAssetLayer::Level:return "サーバー／ワールド側のリソパ";
    case ServerAssetLayer::Global:return "Global（ローカル側）";
    case ServerAssetLayer::Treatment:return "Treatment";
    case ServerAssetLayer::Base:return "Base";
    case ServerAssetLayer::Multiple:return "複数スタックに同一ID";
    default:return "取得できない／対応未確認";
    }
}
} // namespace praxis::companion::packs
