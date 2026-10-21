// SPDX-License-Identifier: GPL-3.0-only
// ChiyanMap terrain sampler adaptation (sunjing2 and contributors).
// Upstream 1785278a865e6dc93d38cea76ccbffaaf64f6659, PlayerHook.h
// 1146-1164,3177-3184,3280-3332,5853-5998. Modified 2026-10-02.
// Use Praxis' existing game tick and value publication; no new renderer owner.
#include "map/MapDataSource.h"
#include "map/chiyan/TerrainPalette.h"
#include "gui/GuiBridge.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/material/Material.h"
#include "mc/deps/core/string/HashedString.h"
#include <Windows.h>
#include <cstdio>

namespace praxis::companion::map {
namespace {
using MaterialType=SharedTypes::v1_26_20::MaterialType;
// SEH wrappers hold POD only: partially loaded native subchunks can raise an
// access violation which /EHs C++ catch does not handle. Never retain pointers
// beyond this synchronous, admitted local-player tick.
bool surface(BlockSource& source,int x,int z,int* above,int* min,int* max) noexcept {
    __try {
        if(!source.hasChunksAt(BlockPos{x,0,z},0,false))return false;
        *min=source.getMinHeight();*max=source.getMaxHeight();
        *above=source.getAboveTopSolidBlock(x,z,true,true);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct BlockValue { Block const* block{};std::string const* name{};MaterialType material{};bool air{}; };
bool blockAt(BlockSource& source,int x,int y,int z,BlockValue* out) noexcept {
    __try {
        if(y<source.getMinHeight()||y>=source.getMaxHeight()||!source.hasChunksAt(BlockPos{x,y,z},0,false))return false;
        out->block=&source.getBlock(BlockPos{x,y,z});
        out->air=out->block->isAir();out->name=&out->block->getTypeName();
        out->material=out->block->getMaterial().mType;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string const* biomeNameAt(BlockSource& source,int x,int y,int z) noexcept {
    __try {
        if(!source.hasChunksAt(BlockPos{x,y,z},0,false))return nullptr;
        return &source.getBiome(BlockPos{x,y,z}).mHash.get().getString();
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool pillarHorizontal(Block const& block) {
    static const HashedString axis("pillar_axis");
    auto const* states=block.getBlockType().mStateNameMap.operator->();
    if(states) {
        auto it=states->find(axis);
        if(it!=states->end()) {
            auto value=block.getState<int>(it->second);
            if(value)return *value!=0;
        }
    }
    return ((block.getData()>>2)&3)!=0;
}
bool safePillarHorizontal(Block const* block) noexcept {
    __try { return pillarHorizontal(*block); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool waterPassable(BlockValue const& b) {
    switch(b.material) {
    case MaterialType::Air:case MaterialType::Water:case MaterialType::Bubble:
    case MaterialType::Plant:case MaterialType::SolidPlant:case MaterialType::Leaves:
    case MaterialType::Glass:case MaterialType::Ice:case MaterialType::PowderSnow:
    case MaterialType::Cactus:case MaterialType::Fire:case MaterialType::Portal:
    case MaterialType::Grate:case MaterialType::StoneDecoration:
    case MaterialType::DecorationSolid:case MaterialType::NonSolid:case MaterialType::StructureVoid:return true;
    case MaterialType::Wood:return chiyan::IsPillarLogOrStem(*b.name);
    default:return false;
    }
}
template<class BiomeQuery> chiyan::Tints tintsAt(BiomeQuery&& query,int x,int y,int z,bool interpolate) {
    auto name=query(x,y,z);
    auto fallback=name.tints;
    if(!interpolate)return fallback;
    // Four adjacent biome cells, same actual dimension/Y. There is no world-
    // global cache and no masked coordinate key that can alias negative X/Z.
    double ux=(double(x)-1.5)*.25,uz=(double(z)-1.5)*.25;
    int cx=int(std::floor(ux)),cz=int(std::floor(uz));
    std::array<chiyan::Tints,4> t;
    for(int i=0;i<4;++i) {
        auto corner=query((cx+(i&1))*4+2,y,(cz+(i>>1))*4+2);
        t[i]=corner.valid?corner.tints:fallback;
    }
    return chiyan::blendTints(t,float(ux-cx),float(uz-cz));
}
}
std::optional<int> NativeDataSource::surfaceHeight(int x,int z) {
    auto column=surfaceColumn(x,z);
    return column?std::optional<int>{column->top}:std::nullopt;
}
std::optional<SurfaceColumn> NativeDataSource::surfaceColumn(int x,int z) {
    int above{},min{},max{};
    if(!surface(source_,x,z,&above,&min,&max)||min < -2048||max > 2048||min>=max||above<=min||above>max)return {};
    if(trace_&&tracedHeight_++<1) {
        auto& m=traceMessages_[0];
        std::snprintf(m.data(),m.size(),"Map Chiyan height: x=%d z=%d aboveTop=%d selectedBase=%d min=%d max=%d overlayProbe=%d",x,z,above,above-1,min,max,above<max?above:-32768);
    }
    // Correct above-top convention, including legal highest block max-1.
    // Probe above it for visible snow/other overlays; air/technical layers
    // fall through to the independently selected surface, never player Y.
    return SurfaceColumn{above-1,min,above<max?std::optional<int>{above}:std::nullopt};
}
std::optional<Voxel> NativeDataSource::voxel(int x,int y,int z) {
    BlockValue b;
    if(!blockAt(source_,x,y,z,&b))return {};
    // A client placeholder is an UNREADY column, not a transparent block.
    // Descending through it produced bogus underground heights in f6ac22e.
    if(b.material==MaterialType::ClientRequestPlaceholder||b.name->find("placeholder")!=std::string::npos
       ||b.name->find("unknown")!=std::string::npos||b.name->find("info_update")!=std::string::npos)return {};
    if(b.air)return Voxel{true,0};
    if(chiyan::IsInvisibleOrTechnicalOverlay(*b.name))return Voxel{false,0,false,false,false,true};
    auto name=*b.name;
    if(chiyan::IsPillarLogOrStem(name)&&safePillarHorizontal(b.block))name=chiyan::GetHorizontalWoodName(name);
    bool water=b.material==MaterialType::Water||b.material==MaterialType::Bubble;
    bool tinted=water||chiyan::IsBiomeTintedBlock(name);
    auto tint=tinted?tintsAt([this](int bx,int by,int bz)->auto const&{return cachedBiome(bx,by,bz);},x,y,z,!water):chiyan::biomeTints("");
    bool recognized{};
    auto color=chiyan::getBlockColor(name,tint.grass,tint.foliage,tint.water,&recognized);
    if(water)color=tint.water;
    auto packed=packMapColor(color.r,color.g,color.b,color.a);
    if(trace_&&tracedColor_<3) {
        auto& m=traceMessages_[++tracedColor_];
        std::snprintf(m.data(),m.size(),"Map Chiyan selected: x=%d y=%d z=%d block=%.96s palette=%.96s rgba=(%.4f,%.4f,%.4f,%.4f) packed=%08X recognized=%d water=%d",x,y,z,b.name->c_str(),name.c_str(),color.r,color.g,color.b,color.a,packed.value_or(0),recognized,water);
    }
    // Current cave UX treats vegetation/water as a gap, while visible glass,
    // leaves and logs remain a real floor. Water's seafloor search has its own
    // broader passability contract and cannot redefine cave collision.
    bool cavePassable=water||b.material==MaterialType::Plant||
        b.material==MaterialType::NonSolid||b.material==MaterialType::Fire;
    return Voxel{false,packed.value_or(0),cavePassable,water,bool(packed),waterPassable(b)};
}
std::array<char,64> NativeDataSource::biome(int x,int y,int z) {
    return cachedBiome(x,y,z).name;
}
NativeDataSource::BiomeValue const& NativeDataSource::cachedBiome(int x,int y,int z) {
    return biomeValues_.get(x,y,z,[this](int bx,int by,int bz) {
        BiomeValue out;auto name=biomeNameAt(source_,bx,by,bz);
        if(name)std::copy_n(name->begin(),std::min<std::size_t>(name->size(),63),out.name.begin());
        out.valid=bool(name);
        // Preserve the entire native identifier for tint rules before bounding
        // the published name; palette evaluation may allocate its own string.
        out.tints=name?chiyan::biomeTints(*name):chiyan::biomeTints("");
        return out;
    });
}
void NativeDataSource::completed(int x,int z,Sample const& s) {
    if(!trace_||tracedFinal_>=3)return;
    auto& m=traceMessages_[4+tracedFinal_++];
    std::array<long long,2> offsets{-1,-1};
    for(std::size_t i=0;i<traceViewCount_;++i) {
        auto const& v=traceViews_[i];int px=x-v.x+v.radius,pz=z-v.z+v.radius,side=2*v.radius+1;
        if(v.radius>=1&&v.radius<=256&&px>=0&&pz>=0&&px<side&&pz<side)
            offsets[i]=(static_cast<long long>(pz)*side+px)*4;
    }
    // These are the requested bake views. The actual published immutable
    // image origins/sides are separately logged by the owning runtime.
    std::snprintf(m.data(),m.size(),"Map Chiyan final base: x=%d z=%d height=%d rgba=%08X biome=%.63s cacheTile=(%d,%d) cacheIndex=%zu scanOffset=(%d,%d) requestedMiniRGBAByte=%lld requestedWorldRGBAByte=%lld",x,z,int(s.height),s.color,s.biome.data(),floorDiv(x,16),floorDiv(z,16),cellIndex(x,z),x-centerX_,z-centerZ_,offsets[0],offsets[1]);
}
void NativeDataSource::logTrace() const noexcept {
    for(auto const& m:traceMessages_)if(m[0])logGuiInfo(m.data());
}
} // namespace praxis::companion::map
