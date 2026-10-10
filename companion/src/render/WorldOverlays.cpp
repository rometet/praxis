#include "render/WorldOverlays.h"
#include "core/Settings.h"
#include "render/LightOverlay.h"
#include "render/RedstoneOverlay.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
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
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"
#include "mc/client/player/LocalPlayer.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace praxis::companion {
namespace {
using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;
struct ChunkMesh {
    std::unique_ptr<mce::Mesh> mesh;
    ChunkBorderOptions options{};
    int centerX{}, centerY{}, centerZ{};
    unsigned long long generation{};
    bool initialized{};
} chunkMesh;
std::mutex chunkMeshMutex;
std::unique_ptr<mce::MaterialPtr> chunkLineMaterial;
std::unique_ptr<mce::MaterialPtr> hitboxMaterial;
bool hitboxTransparent{};
std::optional<mce::TexturePtr> cachedWhiteTexture;

TextureVariant whiteTexture(LevelRenderer& renderer) {
    if (cachedWhiteTexture && cachedWhiteTexture->mClientTexture
        && cachedWhiteTexture->mClientTexture->mIsMissingTexture != IsMissingTexture::Yes)
        return *cachedWhiteTexture;
    cachedWhiteTexture.reset();
    auto const& group = renderer.mTextureGroup.get();
    if (!group) return {};
    auto texture = group->getTexture(
        ResourceLocation{Core::PathView{"textures/ui/white_background"}}, false,
        std::nullopt, cg::TextureSetLayerType::Color
    );
    if (!texture.mClientTexture || texture.mClientTexture->mIsMissingTexture == IsMissingTexture::Yes)
        return {};
    cachedWhiteTexture = std::move(texture);
    return *cachedWhiteTexture;
}

OffscreenCaptureDescription const& emptyCapture() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

void setColor(Tessellator& tessellator, std::uint32_t color) {
    tessellator.color(
        static_cast<float>(color & 0xff) / 255.0f,
        static_cast<float>((color >> 8) & 0xff) / 255.0f,
        static_cast<float>((color >> 16) & 0xff) / 255.0f,
        static_cast<float>((color >> 24) & 0xff) / 255.0f
    );
}

void buildChunkMesh(BaseActorRenderContext& context, ChunkBorderOptions const& options,
                    Vec3 const& position, unsigned long long generation) {
    chunkMesh.mesh.reset();
    chunkMesh.options = options;
    chunkMesh.centerX = chunkAt(position.x);
    chunkMesh.centerY = blockFloor(position.y);
    chunkMesh.centerZ = chunkAt(position.z);
    chunkMesh.generation = generation;
    chunkMesh.initialized = true;
    auto lines = makeChunkBorderLines(position.x, position.y, position.z, options);
    if (lines.empty()) return;
    auto& tessellator = context.mScreenContext.tessellator;
    tessellator.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::LineList,
        static_cast<int>(lines.size() * 2),
        false
    );
    for (auto const& line : lines) {
        setColor(tessellator, line.color);
        tessellator.vertex(line.x0, line.y0, line.z0);
        tessellator.vertex(line.x1, line.y1, line.z1);
    }
    chunkMesh.mesh = std::make_unique<mce::Mesh>(tessellator.end(
        Tessellator::UploadMode::Buffered,
        "PraxisChunkBordersLegacyLineList",
        SupplementaryFieldAutoGenerationMode{0}
    ));
}

void renderHitboxes(BaseActorRenderContext& context) {
    std::lock_guard lock(chunkMeshMutex);
    auto current=settings();auto const& o=current->hitboxOptions;
    auto* player=context.mClientInstance.getLocalPlayer();auto* renderer=context.mClientInstance.getLevelRenderer();
    auto const& impl=context.mImpl.get();
    if(!player||!renderer||!renderer->mLevelRendererPlayer||!impl)return;
    auto texture=whiteTexture(*renderer);
    if(!hitboxMaterial||hitboxTransparent!=o.transparent||!hitboxMaterial->mRenderMaterialInfoPtr.get()) {
        hitboxTransparent=o.transparent;
        hitboxMaterial=std::make_unique<mce::MaterialPtr>(mce::RenderMaterialGroup::common(),
            HashedString{o.transparent?"selection_overlay":"selection_box"});
    }
    bool const backendReady=hitboxMaterial&&hitboxMaterial->mRenderMaterialInfoPtr.get()&&!std::holds_alternative<std::monostate>(texture);
    auto const bit=1u<<static_cast<unsigned>(Feature::Hitbox);
    if(backendReady)availableFeatures.fetch_or(bit,std::memory_order_acq_rel);
    else availableFeatures.fetch_and(~bit,std::memory_order_acq_rel);
    if(!backendReady||!current->on(Feature::Hitbox)||!o.showUnclassified)return;
    // Typed SDK enumerates live actors exactly as the old feature did. The SDK
    // materializes its actor vector; rendering is bounded by maxActors and range.
    auto actors=player->getLevel().getRuntimeActorList();
    auto const position=player->getPosition();auto const generation=worldGeneration.load();
    std::vector<HitboxBounds> boxes;boxes.reserve(o.maxActors);
    for(auto* actor:actors) {
        if(boxes.size()>=static_cast<std::size_t>(o.maxActors))break;
        if(!actor||actor==player||actor->getDimensionId()!=player->getDimensionId())continue;
        auto const p=actor->getPosition();
        auto dx=p.x-position.x,dy=p.y-position.y,dz=p.z-position.z;
        auto distance=dx*dx+dy*dy+dz*dz;
        if(!std::isfinite(distance)||distance>o.range*o.range)continue;
        float alpha=context.getFrameAlpha(*actor);
        if(!std::isfinite(alpha))continue;
        auto const interpolated=actor->getInterpolatedPosition(std::clamp(alpha,0.f,1.f));
        auto const& bounds=actor->getAABB();
        auto delta=interpolated-p;
        HitboxBounds b{{bounds.min.x+delta.x,bounds.min.y+delta.y,bounds.min.z+delta.z},
                       {bounds.max.x+delta.x,bounds.max.y+delta.y,bounds.max.z+delta.z}};
        if(validHitboxBounds(b))boxes.push_back(b);
    }
    if(boxes.empty()||generation!=worldGeneration.load()||!runtimeReady.load())return;
    auto& tessellator=context.mScreenContext.tessellator;
    tessellator.begin(Tessellator::DebugContextCallback{},mce::PrimitiveMode::LineList,static_cast<int>(boxes.size()*24),false);
    setColor(tessellator,o.color);
    for(auto const& b:boxes)for(auto const& edge:hitboxEdges(b)) {
        tessellator.vertex(edge.first.x,edge.first.y,edge.first.z);
        tessellator.vertex(edge.second.x,edge.second.y,edge.second.z);
    }
    auto mesh=tessellator.end(Tessellator::UploadMode::Buffered,"PraxisTypedHitboxes",SupplementaryFieldAutoGenerationMode{0});
    if(!mesh.isValid())return;
    auto camera=impl->mCameraPosition.get();
    if(!std::isfinite(camera.x)||!std::isfinite(camera.y)||!std::isfinite(camera.z))return;
    auto matrix=context.mScreenContext.camera.worldMatrixStack.get().push(false);matrix.mat->translate(-camera.x,-camera.y,-camera.z);
    mesh.renderMesh(context.mScreenContext,*hitboxMaterial,texture,0,mesh.mVertexCount.get().value_or(0u),emptyCapture(),nullptr);
}

} // namespace

void renderWorldOverlays(BaseActorRenderContext& context, bool alphaLayer) {
    // World overlay scanning and glyph generation must never run during the
    // world-loading transition. captureClient() sets inputAllowed only after
    // the top screen is the in-game HUD/play screen.
    if (!inputAllowed.load(std::memory_order_acquire)) return;

    if(alphaLayer)renderHitboxes(context);
    renderLightOverlay(context, alphaLayer);
    renderRedstoneOverlay(context, alphaLayer);
    std::lock_guard lock(chunkMeshMutex);
    auto current = settings();
    if (!current->chunkBorder.enabled) {
        chunkMesh.mesh.reset();
        chunkMesh.initialized = false;
        return;
    }
    if (!chunkLineMaterial || !chunkLineMaterial->mRenderMaterialInfoPtr.get())
        chunkLineMaterial = std::make_unique<mce::MaterialPtr>(
            mce::RenderMaterialGroup::common(),
            HashedString{"selection_box"}
        );
    auto* levelRenderer = context.mClientInstance.getLevelRenderer();
    if (!levelRenderer) return;
    auto texture = whiteTexture(*levelRenderer);
    bool const backendReady = chunkLineMaterial && chunkLineMaterial->mRenderMaterialInfoPtr.get()
        && !std::holds_alternative<std::monostate>(texture);
    if (backendReady)
        availableFeatures.fetch_or(1u << static_cast<unsigned>(Feature::ChunkBorder), std::memory_order_acq_rel);
    else
        availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::ChunkBorder)), std::memory_order_acq_rel);
    if (!current->on(Feature::ChunkBorder) || !featureAvailable(Feature::ChunkBorder)) {
        chunkMesh.mesh.reset();
        chunkMesh.initialized = false;
        return;
    }
    auto* player = context.mClientInstance.getLocalPlayer();
    auto const& impl = context.mImpl.get();
    if (!player || !levelRenderer || !impl || !levelRenderer->mLevelRendererPlayer) return;
    Vec3 const position = player->getPosition();
    auto const generation = worldGeneration.load(std::memory_order_acquire);
    if (!alphaLayer) {
        if (!chunkMesh.initialized || chunkMesh.options != current->chunkBorder
            || chunkMesh.centerX != chunkAt(position.x)
            || chunkMesh.centerY != blockFloor(position.y)
            || chunkMesh.centerZ != chunkAt(position.z)
            || chunkMesh.generation != generation)
            buildChunkMesh(context, current->chunkBorder, position, generation);
        return;
    }
    if (!chunkMesh.mesh || !chunkMesh.mesh->isValid()) return;
    Vec3 const camera = impl->mCameraPosition.get();
    auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(-camera.x, -camera.y, -camera.z);
    chunkMesh.mesh->renderMesh(
        context.mScreenContext, *chunkLineMaterial, texture, 0,
        chunkMesh.mesh->mVertexCount.get().value_or(0u), emptyCapture(), nullptr
    );
}

void clearWorldOverlays() noexcept {
    clearLightOverlay();
    clearRedstoneOverlay();
    std::lock_guard lock(chunkMeshMutex);
    chunkMesh.mesh.reset();
    chunkMesh.initialized = false;
    cachedWhiteTexture.reset();
    chunkLineMaterial.reset();
    hitboxMaterial.reset();
}
} // namespace praxis::companion
