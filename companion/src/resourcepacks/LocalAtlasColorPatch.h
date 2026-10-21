#pragma once
// CPU-only reversible RGBA sub-rectangle composer. The input must be a
// native-owned COPY of the current terrain atlas, never the live GPU texture.
// This does not generate padding/mipmaps or publish anything to Minecraft.
#include "resourcepacks/LocalTerrainAtlasProof.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace praxis::companion::packs {
struct LocalAtlasColorPatch {
    unsigned x{},y{},width{},height{},atlasWidth{},atlasHeight{};
    bool valid() const noexcept {
        return width>0&&height>0&&atlasWidth>0&&atlasHeight>0
            &&x<=atlasWidth&&y<=atlasHeight
            &&width<=atlasWidth-x&&height<=atlasHeight-y;
    }
};
inline std::optional<LocalAtlasColorPatch>
planLocalAtlasColorPatch(LocalTerrainAtlasProof const& source) noexcept {
    if(!source.mappingObserved()||!terrainUvGeometryValid(source)
       ||!source.uvTexW||!source.uvTexH
       ||source.uvTexW>8192||source.uvTexH>8192)return std::nullopt;
    auto pixel=[&](float coord,unsigned extent)->std::optional<unsigned>{
        auto v=double(coord)*double(extent);
        if(!std::isfinite(v)||v<0||v>extent)return std::nullopt;
        auto rounded=std::round(v);
        // Refuse uncertain UV boundaries; no arbitrary expansion into other tiles.
        if(std::abs(v-rounded)>.075)return std::nullopt;
        return static_cast<unsigned>(rounded);
    };
    auto x0=pixel(source.u0,source.uvTexW),y0=pixel(source.v0,source.uvTexH);
    auto x1=pixel(source.u1,source.uvTexW),y1=pixel(source.v1,source.uvTexH);
    if(!x0||!y0||!x1||!y1||*x0>=*x1||*y0>=*y1)return std::nullopt;
    LocalAtlasColorPatch plan{*x0,*y0,*x1-*x0,*y1-*y0,source.uvTexW,source.uvTexH};
    if(!plan.valid()||plan.width!=source.candidateW||plan.height!=source.candidateH)return std::nullopt;
    return plan;
}
inline bool applyLocalAtlasColorPatch(
    std::span<std::uint8_t> targetRGBA,
    std::span<std::uint8_t const> localSourceRGBA,
    LocalAtlasColorPatch const& p) noexcept {
    if(!p.valid()||p.atlasWidth>8192||p.atlasHeight>8192)return false;
    if(targetRGBA.size()!=std::size_t(p.atlasWidth)*p.atlasHeight*4
        ||localSourceRGBA.size()!=std::size_t(p.width)*p.height*4)return false;
    for(unsigned row=0;row<p.height;++row){
        auto to=(std::size_t(p.y)+row)*p.atlasWidth*4+std::size_t(p.x)*4;
        auto from=std::size_t(row)*p.width*4;
        std::memcpy(targetRGBA.data()+to,localSourceRGBA.data()+from,std::size_t(p.width)*4);
    }
    return true;
}
} // namespace praxis::companion::packs
