#include "render/RedstoneOverlay.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "font/CorporateLogoFont.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/ComparatorBlockActor.h"
#include "mc/world/level/block/RepeaterBlock.h"
#include "mc/world/level/block/RedstoneTorchBlock.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <variant>
#include <vector>

namespace praxis::companion {
namespace {
struct Key { int x{},y{},z{}; bool operator==(Key const&) const = default; };
struct KeyHash {
    std::size_t operator()(Key const& k) const noexcept {
        auto h=std::hash<int>{}(k.x);
        h^=std::hash<int>{}(k.y)+0x9e3779b9u+(h<<6)+(h>>2);
        h^=std::hash<int>{}(k.z)+0x9e3779b9u+(h<<6)+(h>>2);
        return h;
    }
};
using TextureVariant=std::variant<std::monostate,mce::TexturePtr,mce::ClientTexture,mce::ServerTexture>;
std::mutex redstoneMutex;
std::unordered_map<Key,RedstoneSample,KeyHash> samples;
std::unique_ptr<mce::Mesh> mesh;
std::unique_ptr<mce::MaterialPtr> material;
std::optional<mce::TexturePtr> cachedTexture;
RedstoneOverlayOptions previousOptions{};
Vec3 anchorPosition{};
int anchorX{},anchorY{},anchorZ{};
std::size_t cursor{};
bool anchorValid{},scanning{true},meshDirty{};
unsigned long long previousGeneration{};
std::chrono::steady_clock::time_point nextRefresh{};
std::size_t scannedBlocks{};
std::size_t matchedDust{};
std::size_t stateReadFailures{};
bool scanSummaryLogged{};
bool meshSummaryLogged{};

void resetScan() {
    samples.clear();mesh.reset();cursor=0;anchorValid=false;scanning=true;
    meshDirty=true;nextRefresh={};
    scannedBlocks=matchedDust=stateReadFailures=0;
    scanSummaryLogged=false;meshSummaryLogged=false;
}
TextureVariant whiteTexture(LevelRenderer& renderer) {
    if(cachedTexture&&cachedTexture->mClientTexture
        &&cachedTexture->mClientTexture->mIsMissingTexture!=IsMissingTexture::Yes)return *cachedTexture;
    cachedTexture.reset();
    auto const& group=renderer.mTextureGroup.get();if(!group)return {};
    auto texture=group->getTexture(ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                                   false,std::nullopt,cg::TextureSetLayerType::Color);
    if(!texture.mClientTexture||texture.mClientTexture->mIsMissingTexture==IsMissingTexture::Yes)return {};
    cachedTexture=std::move(texture);return *cachedTexture;
}
OffscreenCaptureDescription const& emptyCapture() {
    using Storage=decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};return empty.get();
}
void prune(int x,int y,int z,RedstoneOverlayOptions const& options) {
    for(auto it=samples.begin();it!=samples.end();) {
        if(!redstoneInRange(it->second,x,y,z,options))it=samples.erase(it);
        else ++it;
    }
    meshDirty=true;
}

bool exactNativeBlockType(BlockType const& type, void** expectedVtable) noexcept {
    // The pinned Windows SDK exposes the vanilla class vftables. Read only
    // the object's vptr and require an exact match before a typed downcast;
    // Minecraft RTTI is not required and custom block types fail closed.
    void** actualVtable{};
    std::memcpy(&actualVtable, static_cast<void const*>(&type), sizeof(actualVtable));
    return expectedVtable && actualVtable == expectedVtable;
}

void scan(BlockSource& source,RedstoneOverlayOptions const& options) {
    std::size_t const side=static_cast<std::size_t>(options.range*2+1);
    std::size_t const total=side*side*lightVerticalOffsets.size();
    auto const deadline=std::chrono::steady_clock::now()+std::chrono::microseconds(1500);
    static HashedString const signalName{"redstone_signal"};
    for(int read=0;read<options.scanBudget&&cursor<total;++read) {
        if(read&&std::chrono::steady_clock::now()>=deadline)break;
        std::size_t const index=cursor++;
        int dx{},dz{};
        if(!lightColumnOffset(index/lightVerticalOffsets.size(),options.range,dx,dz))continue;
        int const x=anchorX+dx,y=anchorY+lightVerticalOffsets[index%lightVerticalOffsets.size()],z=anchorZ+dz;
        Key const key{x,y,z};BlockPos const pos{x,y,z};
        if(!source.hasChunksAt(pos,0,false)){meshDirty|=samples.erase(key)!=0;continue;}
        Block const& block=source.getBlock(pos);
        ++scannedBlocks;
        auto const& name=block.getTypeName();
        std::optional<int> power;
        try {
            auto const& type=block.getBlockType();
            if(isRedstoneDust(name)) {
                ++matchedDust;
                BlockState const* state=type.getBlockState(signalName);
                if(state) power=block.getState<int>(*state);
            } else if(isRedstoneComparator(name)) {
                // BlockActor::getType reads the SDK's immutable native tag.
                // Borrow the actor only for this callback and copy the output.
                auto const* actor=source.getBlockEntity(pos);
                if(actor && actor->getType()==BlockActorType::Comparator)
                    power=static_cast<ComparatorBlockActor const*>(actor)->mOutput;
            } else if(isRedstoneRepeater(name)) {
                if(exactNativeBlockType(type,RepeaterBlock::$vftable()))
                    power=static_cast<RepeaterBlock const&>(type).mOn?15:0;
            } else if(isRedstoneTorch(name)) {
                if(exactNativeBlockType(type,RedstoneTorchBlock::$vftable()))
                    power=static_cast<RedstoneTorchBlock const&>(type).mOn?15:0;
            }
        } catch (...) {
            ++stateReadFailures;
            power.reset();
        }
        if(!power||!validRedstonePower(*power)){meshDirty|=samples.erase(key)!=0;continue;}
        RedstoneSample const sample{x,y,z,*power};
        if(auto it=samples.find(key);it==samples.end()||it->second!=sample){
            samples.insert_or_assign(key,sample);meshDirty=true;
        }
    }
    if(cursor>=total){
        scanning=false;nextRefresh=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        if(!scanSummaryLogged){
            scanSummaryLogged=true;
            char message[256]{};
            sprintf_s(message,
                "RedstoneOverlay scan complete: blocks=%zu dust=%zu samples=%zu stateFailures=%zu",
                scannedBlocks,matchedDust,samples.size(),stateReadFailures);
            logGuiInfo(message);
        }
    }
}

struct LabelQuad {
    Vec3 a{}, b{}, c{}, d{};
};
struct LabelLine {
    Vec3 a{}, b{};
};

Vec3 labelPoint(Vec3 const& origin, Vec3 const& right, Vec3 const& down, float x, float y) {
    return {
        origin.x + right.x * x + down.x * y,
        origin.y + right.y * x + down.y * y,
        origin.z + right.z * x + down.z * y
    };
}

bool appendLegacyLabel(
    std::vector<LabelQuad>& quads,
    std::vector<LabelLine>& lines,
    unsigned level,
    Vec3 const& center,
    Vec3 const& camera
) {
    char label[8]{};
    std::snprintf(label, sizeof(label), "%u", level);
    constexpr float glyphHeight = 0.46f;
    constexpr float glyphGap = 0.012f;

    Vec3 down{camera.x - center.x, 0.0f, camera.z - center.z};
    float const length = std::sqrt(down.x * down.x + down.z * down.z);
    if (!std::isfinite(length) || length < 0.001f) {
        down = Vec3{0.0f, 0.0f, 1.0f};
    } else {
        down.x /= length;
        down.z /= length;
    }
    Vec3 const right{down.z, 0.0f, -down.x};

    std::size_t const count = std::strlen(label);
    if (!count) return false;
    float totalWidth = glyphGap * static_cast<float>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        auto const* glyph = CorporateLogoFont::digitGlyph(label[i]);
        if (!glyph) return false;
        totalWidth += glyph->advance * glyphHeight;
    }

    Vec3 glyphOrigin{
        center.x - right.x * (totalWidth * 0.5f) - down.x * (glyphHeight * 0.5f),
        center.y,
        center.z - right.z * (totalWidth * 0.5f) - down.z * (glyphHeight * 0.5f)
    };
    for (std::size_t i = 0; i < count; ++i) {
        auto const* glyph = CorporateLogoFont::digitGlyph(label[i]);
        if (!glyph) return false;
        // Legacy Praxis lifts the black contour slightly above the white
        // glyph surface. Without this 26.51 depth-tests the two coplanar
        // primitives against each other and digits look broken/flickery.
        Vec3 const outlineLift{0.0f, 0.0015f, 0.0f};
        for (auto const& edge : glyph->outline) {
            auto a = labelPoint(glyphOrigin, right, down, edge.x0 * glyphHeight, edge.y0 * glyphHeight);
            auto b = labelPoint(glyphOrigin, right, down, edge.x1 * glyphHeight, edge.y1 * glyphHeight);
            a.y += outlineLift.y;
            b.y += outlineLift.y;
            lines.push_back({a, b});
        }
        for (auto const& rect : glyph->rects) {
            quads.push_back({
                labelPoint(glyphOrigin, right, down, rect.x0 * glyphHeight, rect.y1 * glyphHeight),
                labelPoint(glyphOrigin, right, down, rect.x1 * glyphHeight, rect.y1 * glyphHeight),
                labelPoint(glyphOrigin, right, down, rect.x1 * glyphHeight, rect.y0 * glyphHeight),
                labelPoint(glyphOrigin, right, down, rect.x0 * glyphHeight, rect.y0 * glyphHeight)
            });
        }
        float const advance = glyph->advance * glyphHeight + glyphGap;
        glyphOrigin.x += right.x * advance;
        glyphOrigin.y += right.y * advance;
        glyphOrigin.z += right.z * advance;
    }
    return true;
}

void renderImmediate(BaseActorRenderContext& context, RedstoneOverlayOptions const& options) {
    if (samples.empty() || !options.showValues
        || !material || !material->mRenderMaterialInfoPtr.get()) return;

    auto const& impl = context.mImpl.get();
    if (!impl) return;

    int const limit = std::min<int>(options.drawBudget, static_cast<int>(samples.size()));
    if (limit <= 0) return;

    Vec3 const camera = impl->mCameraPosition.get();
    std::vector<LabelQuad> quads;
    std::vector<LabelLine> lines;
    quads.reserve(static_cast<std::size_t>(limit) * 64u);
    lines.reserve(static_cast<std::size_t>(limit) * 64u);

    int emitted{};
    for (auto const& [key, sample] : samples) {
        if (emitted++ >= limit) break;
        unsigned const level = static_cast<unsigned>(sample.power);
        if (level > 15) continue;
        Vec3 const center{
            static_cast<float>(sample.x) + 0.5f,
            static_cast<float>(sample.y) + 0.035f,
            static_cast<float>(sample.z) + 0.5f
        };
        appendLegacyLabel(quads, lines, level, center, camera);
    }
    if (quads.empty() && lines.empty()) return;

    auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(-camera.x, -camera.y, -camera.z);

    auto& t = context.mScreenContext.tessellator;
    if (!lines.empty()) {
        t.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(lines.size() * 2u),
            false
        );
        t.color(0.015f, 0.015f, 0.015f, 1.0f);
        for (auto const& line : lines) {
            t.vertex(line.a.x, line.a.y, line.a.z);
            t.vertex(line.b.x, line.b.y, line.b.z);
        }
        MeshHelpers::renderMeshImmediately(
            context.mScreenContext,
            t,
            *material,
            emptyCapture()
        );
    }

    if (!quads.empty()) {
        t.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(quads.size() * 4u),
            false
        );
        t.color(0.96f, 0.96f, 0.96f, 1.0f);
        for (auto const& quad : quads) {
            t.vertex(quad.a.x, quad.a.y, quad.a.z);
            t.vertex(quad.b.x, quad.b.y, quad.b.z);
            t.vertex(quad.c.x, quad.c.y, quad.c.z);
            t.vertex(quad.d.x, quad.d.y, quad.d.z);
        }
        MeshHelpers::renderMeshImmediately(
            context.mScreenContext,
            t,
            *material,
            emptyCapture()
        );
    }

    if (!meshSummaryLogged) {
        meshSummaryLogged = true;
        char message[240]{};
        sprintf_s(message,
                  "RedstoneOverlay legacy glyph draw: samples=%zu quads=%zu lines=%zu camera=(%.2f,%.2f,%.2f)",
                  samples.size(), quads.size(), lines.size(),
                  camera.x, camera.y, camera.z);
        logGuiInfo(message);
    }
}
} // namespace

void renderRedstoneOverlay(BaseActorRenderContext& context,bool alphaLayer) {
    std::lock_guard lock(redstoneMutex);
    auto* player=context.mClientInstance.getLocalPlayer();
    auto* renderer=context.mClientInstance.getLevelRenderer();
    auto const& impl=context.mImpl.get();
    bool const backendReady=player&&renderer&&impl&&renderer->mLevelRendererPlayer;
    if(backendReady)availableFeatures.fetch_or(1u<<static_cast<unsigned>(Feature::RedstoneOverlay));
    else availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::RedstoneOverlay)));
    auto current=settings();
    if(!backendReady||!current->on(Feature::RedstoneOverlay)){resetScan();return;}
    if(!material||!material->mRenderMaterialInfoPtr.get())
        material=std::make_unique<mce::MaterialPtr>(mce::RenderMaterialGroup::common(),
                                                    HashedString{"selection_box"});
    if(!material||!material->mRenderMaterialInfoPtr.get()){
        availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::RedstoneOverlay)));
        resetScan();return;
    }
    auto const generation=worldGeneration.load(std::memory_order_acquire);
    if(generation!=previousGeneration||previousOptions!=current->redstoneOptions){
        resetScan();previousGeneration=generation;previousOptions=current->redstoneOptions;
    }
    if(!alphaLayer){
        Vec3 const position=player->getPosition();
        int const x=blockFloor(position.x),y=blockFloor(position.y),z=blockFloor(position.z);
        if(!anchorValid||std::abs(x-anchorX)>1||std::abs(y-anchorY)>2||std::abs(z-anchorZ)>1){
            if(anchorValid)prune(x,y,z,current->redstoneOptions);
            anchorX=x;anchorY=y;anchorZ=z;anchorPosition=position;
            anchorValid=true;cursor=0;scanning=true;meshDirty=true;
        }
        if(!scanning&&std::chrono::steady_clock::now()>=nextRefresh){cursor=0;scanning=true;}
        if(scanning){
            auto& source=player->getDimension().getBlockSourceFromMainChunkSource();
            scan(source,current->redstoneOptions);
        }
        meshDirty=false;
        return;
    }
    renderImmediate(context,current->redstoneOptions);
}
void clearRedstoneOverlay() noexcept {
    std::lock_guard lock(redstoneMutex);
    resetScan();material.reset();cachedTexture.reset();previousGeneration=0;
    availableFeatures.fetch_and(~(1u<<static_cast<unsigned>(Feature::RedstoneOverlay)));
}
} // namespace praxis::companion
