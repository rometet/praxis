#pragma once
// Deterministic read-only proof policy. No GPU uploads, pack mutations, or
// renderer reset. A valid UV does NOT prove an atlas update is safe.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
namespace praxis::companion::packs {
struct LocalTerrainAtlasProof {
    std::uint64_t stagedToken{},world{};
    bool gameAtlas{},terrainManager{},sameAtlasManager{},textureGroup{};
    bool sourcePathPowder{}, uvFinite{},uvInsideAtlas{},sourceDimensionsMatch{}, uvAreaNonzero{};
    unsigned candidateW{},candidateH{},uvSourceW{},uvSourceH{},uvTexW{},uvTexH{};
    float u0{},v0{},u1{},v1{};
    bool mappingObserved() const noexcept {
        return gameAtlas&&terrainManager&&sameAtlasManager&&textureGroup
          &&sourcePathPowder&&uvFinite&&uvInsideAtlas&&sourceDimensionsMatch&&uvAreaNonzero;
    }
};
inline bool terrainUvGeometryValid(LocalTerrainAtlasProof const& x) noexcept {
    if(!x.candidateW||!x.candidateH||!x.uvSourceW||!x.uvSourceH)return false;
    if(x.candidateW!=x.uvSourceW||x.candidateH!=x.uvSourceH)return false;
    if(!(std::isfinite(x.u0)&&std::isfinite(x.v0)&&std::isfinite(x.u1)&&std::isfinite(x.v1)))return false;
    return x.u0>=0.f&&x.v0>=0.f&&x.u1<=1.f&&x.v1<=1.f
      &&x.u0<x.u1&&x.v0<x.v1;
}
class LocalTerrainAtlasProofControl {
    std::mutex mutex;
    LocalTerrainAtlasProof latest;
    bool observed{};
public:
    void publish(LocalTerrainAtlasProof proof) {
        std::lock_guard lock(mutex);latest=proof;observed=true;
    }
    std::pair<bool,LocalTerrainAtlasProof> view(){
        std::lock_guard lock(mutex);return {observed,latest};
    }
};
inline LocalTerrainAtlasProofControl& localTerrainAtlasProofControl() {
    static auto* state=new LocalTerrainAtlasProofControl;return *state;
}
} // namespace praxis::companion::packs
