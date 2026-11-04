#pragma once
#include "resourcepacks/PackCatalog.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace praxis::companion::packs {
enum class LocalOverlayStagePhase:unsigned {Idle=0,Queued=1,Staging=2,Decoded=3,Rejected=4};
struct LocalOverlayStageRequest {
    std::uint64_t token{};
    std::shared_ptr<Catalog const> catalog;
    std::string key;
};
struct LocalOverlayStageView {
    std::uint64_t token{};
    LocalOverlayStagePhase phase{LocalOverlayStagePhase::Idle};
    std::size_t bytes{};
    unsigned width{},height{};
    bool cpuImageValid{};
    std::string status{"候補画像を未検証です。"};
};
struct LocalOverlayRgbaCopy {
    std::uint64_t token{};
    unsigned width{},height{};
    std::vector<std::uint8_t> rgba;
    std::string png;
    bool valid() const noexcept {return width&&height&&rgba.size()==std::size_t(width)*height*4;}
};
struct LocalOverlayPngMetadata {
    unsigned width{},height{},bitDepth{},colorType{};
    bool valid{};
};
inline LocalOverlayPngMetadata localOverlayPngMetadata(std::string const& input) noexcept {
    LocalOverlayPngMetadata out{};
    if(input.size()<33||input.size()>8*1024*1024)return out;
    auto const* p=reinterpret_cast<unsigned char const*>(input.data());
    constexpr unsigned char pngSignature[8]{137,80,78,71,13,10,26,10};
    if(!std::equal(p,p+8,pngSignature))return out;
    auto const readBE=[&](int at)->unsigned{
        return (unsigned(p[at])<<24)|(unsigned(p[at+1])<<16)|(unsigned(p[at+2])<<8)|unsigned(p[at+3]);};
    if(readBE(8)!=13||p[12]!='I'||p[13]!='H'||p[14]!='D'||p[15]!='R')return out;
    out.width=readBE(16);out.height=readBE(20);
    out.bitDepth=p[24];out.colorType=p[25];
    out.valid=out.width>0&&out.height>0&&out.width<=2048&&out.height<=2048
        &&(out.bitDepth==8||out.bitDepth==16)&&(out.colorType==2||out.colorType==6)
        &&p[26]==0&&p[27]==0;
    return out;
}
class LocalOverlayStageControl {
    mutable std::mutex mutex;
    std::optional<LocalOverlayStageRequest> pending;
    LocalOverlayStageView state;
    std::uint64_t next{};
    std::vector<std::uint8_t> loadedRgba; // decoded CPU pixels, NOT GPU memory
    std::string loadedBytes; // retained for future *verified* renderer transaction
public:
    bool request(std::shared_ptr<Catalog const> catalog,Selection const& chosen,bool inWorld){
        std::lock_guard lock(mutex);
        if(!inWorld||!catalog||!catalog->complete||chosen.generation!=catalog->generation
            ||chosen.order.size()!=1||pending||state.phase==LocalOverlayStagePhase::Staging)return false;
        auto it=std::find_if(catalog->packs.begin(),catalog->packs.end(),
            [&](Pack const& p){return p.key==chosen.order[0]&&p.selectable;});
        if(it==catalog->packs.end())return false;
        loadedRgba.clear();loadedBytes.clear();
        pending=LocalOverlayStageRequest{++next,catalog,it->key};
        state={next,LocalOverlayStagePhase::Queued,0,0,0,false,
            "未適用のローカル画像を検証待ちです。既存パック構成は変更しません。"};
        return true;
    }
    std::optional<LocalOverlayStageRequest> take(){
        std::lock_guard lock(mutex);
        if(!pending)return std::nullopt;
        auto r=std::move(pending);pending.reset();
        state.phase=LocalOverlayStagePhase::Staging;state.status="ローカル候補PNGを読み込み中です。";
        return r;
    }
    void complete(std::uint64_t token,std::string png,LocalOverlayPngMetadata m,
        std::vector<std::uint8_t> decodedRgba,bool accepted,std::string status){
        std::lock_guard lock(mutex);
        if(state.token!=token||state.phase!=LocalOverlayStagePhase::Staging)return;
        if(accepted&&m.valid&&decodedRgba.size()==std::size_t(m.width)*m.height*4){
            loadedBytes=std::move(png);loadedRgba=std::move(decodedRgba);
            state.bytes=loadedBytes.size();state.width=m.width;state.height=m.height;
            state.cpuImageValid=true;state.phase=LocalOverlayStagePhase::Decoded;
            state.status="画像の事前読込に成功。GPU/terrain atlasにはまだ反映していません。";
        }else{
            state.phase=LocalOverlayStagePhase::Rejected;state.status=std::move(status);
        }
    }
    LocalOverlayStageView view() const{
        std::lock_guard lock(mutex);return state;
    }
    std::optional<LocalOverlayRgbaCopy> snapshotForApply() const {
        std::lock_guard lock(mutex);
        if(state.phase!=LocalOverlayStagePhase::Decoded||!state.cpuImageValid)return std::nullopt;
        LocalOverlayRgbaCopy copy{state.token,state.width,state.height,loadedRgba,loadedBytes};
        return copy.valid()?std::optional<LocalOverlayRgbaCopy>{std::move(copy)}:std::nullopt;
    }
};
// Retain until all texture/GPU generations that may reference the staged
// resource are retired. This control never changes Minecraft's RP stack.
inline LocalOverlayStageControl& localOverlayStageControl(){
    static auto* ptr=new LocalOverlayStageControl;return *ptr;
}
} // namespace praxis::companion::packs
