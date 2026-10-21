#include "render/LightOverlay.h"
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
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"
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
struct Key {
    int x{}, y{}, z{};
    bool operator==(Key const&) const = default;
};
struct KeyHash {
    std::size_t operator()(Key const& key) const noexcept {
        auto h = std::hash<int>{}(key.x);
        h ^= std::hash<int>{}(key.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(key.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};
using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;
std::mutex lightMutex;
std::unordered_map<Key, LightSample, KeyHash> samples;
std::unique_ptr<mce::Mesh> lightMesh;
std::unique_ptr<mce::MaterialPtr> lightMaterial;
std::optional<mce::TexturePtr> lightTexture;
LightOverlayOptions previousOptions{};
Vec3 anchorPosition{};
int anchorX{}, anchorY{}, anchorZ{};
std::size_t cursor{};
bool anchorValid{}, scanning{true}, meshDirty{};
unsigned long long previousGeneration{};
std::chrono::steady_clock::time_point nextRefresh{};
std::size_t scannedCells{};
std::size_t surfaceCells{};
std::size_t brightnessReads{};
std::size_t brightnessFailures{};
bool scanSummaryLogged{};
bool meshSummaryLogged{};

void resetScan() {
    samples.clear(); lightMesh.reset(); cursor = 0;
    anchorValid = false; scanning = true; meshDirty = true; nextRefresh = {};
    scannedCells = surfaceCells = brightnessReads = brightnessFailures = 0;
    scanSummaryLogged = false; meshSummaryLogged = false;
}

TextureVariant whiteTexture(LevelRenderer& renderer) {
    if (lightTexture && lightTexture->mClientTexture
        && lightTexture->mClientTexture->mIsMissingTexture != IsMissingTexture::Yes)
        return *lightTexture;
    lightTexture.reset();
    auto const& group = renderer.mTextureGroup.get();
    if (!group) return {};
    auto texture = group->getTexture(ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                                     false, std::nullopt, cg::TextureSetLayerType::Color);
    if (!texture.mClientTexture || texture.mClientTexture->mIsMissingTexture == IsMissingTexture::Yes)
        return {};
    lightTexture = std::move(texture);
    return *lightTexture;
}

OffscreenCaptureDescription const& emptyCapture() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

bool usableFloor(Block const& floor, Block const& above, LightOverlayOptions const& options) {
    if (!above.isAir()) return false;
    if (!options.onlyTopSurface) return true;
    if (floor.isAir()) return false;
    auto const& name = floor.getTypeName();
    static constexpr std::array excluded{"water", "lava", "torch", "redstone", "rail", "carpet", "button",
        "lever", "pressure_plate", "flower", "sapling", "short_grass", "tall_grass", "tallgrass",
        "fern", "vine", "sign", "banner", "ladder", "snow_layer", "tripwire"};
    return std::none_of(excluded.begin(), excluded.end(), [&](char const* token) {
        return name.find(token) != std::string::npos;
    });
}

void prune(int x, int y, int z, LightOverlayOptions const& options) {
    for (auto it = samples.begin(); it != samples.end();) {
        if (!lightInRange(it->second, x, y, z, options)) it = samples.erase(it);
        else ++it;
    }
    meshDirty = true;
}

void scan(BlockSource& source, LightOverlayOptions const& options) {
    auto const side = static_cast<std::size_t>(options.range * 2 + 1);
    auto const total = side * side * lightVerticalOffsets.size();
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(1500);
    for (int read = 0; read < options.scanBudget && cursor < total; ++read) {
        if (read && std::chrono::steady_clock::now() >= deadline) break;
        auto const index = cursor++;
        int dx{}, dz{};
        if (!lightColumnOffset(index / lightVerticalOffsets.size(), options.range, dx, dz)) continue;
        int const x = anchorX + dx;
        int const y = anchorY + lightVerticalOffsets[index % lightVerticalOffsets.size()] + 1;
        int const z = anchorZ + dz;
        Key const key{x, y, z};
        BlockPos const lightPos{x, y, z};
        ++scannedCells;
        if (!source.hasChunksAt(lightPos, 0, false)) {
            meshDirty |= samples.erase(key) != 0; continue;
        }
        BlockPos const floorPos{x, y - 1, z};
        if (!source.hasChunksAt(floorPos, 0, false)
            || !usableFloor(source.getBlock(floorPos), source.getBlock(lightPos), options)) {
            meshDirty |= samples.erase(key) != 0; continue;
        }
        ++surfaceCells;
        std::optional<LightSample> readSample;
        try {
            auto const brightness = source.getBrightnessPair(lightPos);
            int const sky = brightness.sky.get().mValue;
            int const block = brightness.block.get().mValue;
            if (sky >= 0 && sky <= 15 && block >= 0 && block <= 15)
                readSample = LightSample{x, y, z, static_cast<std::uint8_t>(sky), static_cast<std::uint8_t>(block)};
        } catch (...) {
            ++brightnessFailures;
        }
        if (!readSample) {
            meshDirty |= samples.erase(key) != 0; continue;
        }
        ++brightnessReads;
        LightSample const sample = *readSample;
        if (displayLight(sample, options)) {
            if (auto it = samples.find(key); it == samples.end() || it->second != sample) {
                samples.insert_or_assign(key, sample); meshDirty = true;
            }
        } else meshDirty |= samples.erase(key) != 0;
    }
    if (cursor >= total) {
        scanning = false;
        nextRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        if (!scanSummaryLogged) {
            scanSummaryLogged = true;
            char message[256]{};
            sprintf_s(message,
                "LightOverlay scan complete: cells=%zu surfaces=%zu brightness=%zu failures=%zu samples=%zu",
                scannedCells, surfaceCells, brightnessReads, brightnessFailures, samples.size());
            logGuiInfo(message);
        }
    }
}

struct LabelQuad {
    Vec3 a{}, b{}, c{}, d{};
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
    unsigned level,
    Vec3 const& center,
    Vec3 const& camera
) {
    char label[16]{};
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

bool buildBufferedMesh(BaseActorRenderContext& context, LightOverlayOptions const& options) {
    lightMesh.reset();
    meshDirty = false;
    if (samples.empty() || !options.numericDisplay) return false;

    int const limit = std::min<int>(options.drawBudget, static_cast<int>(samples.size()));
    if (limit <= 0) return false;

    auto const& impl = context.mImpl.get();
    if (!impl) return false;
    Vec3 const facingCamera = impl->mCameraPosition.get();

    std::vector<LabelQuad> quads;
    quads.reserve(static_cast<std::size_t>(limit) * 64u);

    int emitted{};
    for (auto const& [key, sample] : samples) {
        if (emitted++ >= limit) break;
        unsigned const level = static_cast<unsigned>(effectiveLight(sample, options));
        if (level > 15) continue;
        Vec3 const center{
            static_cast<float>(sample.x) + 0.5f,
            static_cast<float>(sample.y) + 0.035f,
            static_cast<float>(sample.z) + 0.5f
        };
        appendLegacyLabel(quads, level, center, facingCamera);
    }
    if (quads.empty()) return false;

    auto& t = context.mScreenContext.tessellator;
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

    auto candidate = std::make_unique<mce::Mesh>(t.end(
        Tessellator::UploadMode::Buffered,
        "PraxisLightOverlayLegacyGlyphCache",
        SupplementaryFieldAutoGenerationMode{0}
    ));
    auto const vertices = candidate
        ? candidate->mVertexCount.get().value_or(0u)
        : 0u;
    if (!candidate || !candidate->isValid() || vertices == 0u) {
        if (!meshSummaryLogged) {
            meshSummaryLogged = true;
            logGuiInfo("LightOverlay buffered mesh unavailable; using immediate fallback.");
        }
        return false;
    }

    lightMesh = std::move(candidate);
    if (!meshSummaryLogged) {
        meshSummaryLogged = true;
        char message[256]{};
        sprintf_s(
            message,
            "LightOverlay buffered mesh: samples=%zu quads=%zu vertices=%u",
            samples.size(),
            quads.size(),
            vertices
        );
        logGuiInfo(message);
    }
    return true;
}

void renderImmediate(BaseActorRenderContext& context, LightOverlayOptions const& options) {
    if (samples.empty() || !options.numericDisplay
        || !lightMaterial || !lightMaterial->mRenderMaterialInfoPtr.get()) return;

    auto const& impl = context.mImpl.get();
    if (!impl) return;

    int const limit = std::min<int>(options.drawBudget, static_cast<int>(samples.size()));
    if (limit <= 0) return;

    Vec3 const camera = impl->mCameraPosition.get();
    std::vector<LabelQuad> quads;
    quads.reserve(static_cast<std::size_t>(limit) * 64u);

    int emitted{};
    for (auto const& [key, sample] : samples) {
        if (emitted++ >= limit) break;
        unsigned const level = static_cast<unsigned>(effectiveLight(sample, options));
        if (level > 15) continue;
        Vec3 const center{
            static_cast<float>(sample.x) + 0.5f,
            static_cast<float>(sample.y) + 0.035f,
            static_cast<float>(sample.z) + 0.5f
        };
        appendLegacyLabel(quads, level, center, camera);
    }
    if (quads.empty()) return;

    auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(-camera.x, -camera.y, -camera.z);

    auto& t = context.mScreenContext.tessellator;
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
        *lightMaterial,
        emptyCapture()
    );

    if (!meshSummaryLogged) {
        meshSummaryLogged = true;
        char message[224]{};
        sprintf_s(message,
                  "LightOverlay legacy glyph draw: samples=%zu quads=%zu vertices=%zu camera=(%.2f,%.2f,%.2f)",
                  samples.size(), quads.size(), quads.size() * 4u,
                  camera.x, camera.y, camera.z);
        logGuiInfo(message);
    }
}
} // namespace

void renderLightOverlay(BaseActorRenderContext& context, bool alphaLayer) {
    std::lock_guard lock(lightMutex);
    auto* player = context.mClientInstance.getLocalPlayer();
    auto* renderer = context.mClientInstance.getLevelRenderer();
    auto const& impl = context.mImpl.get();
    bool const backendReady = player && renderer && impl && renderer->mLevelRendererPlayer;
    if (backendReady) availableFeatures.fetch_or(1u << static_cast<unsigned>(Feature::LightOverlay));
    else availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::LightOverlay)));
    auto current = settings();
    if (!backendReady || !current->on(Feature::LightOverlay)) { resetScan(); return; }
    if (!lightMaterial || !lightMaterial->mRenderMaterialInfoPtr.get())
        lightMaterial = std::make_unique<mce::MaterialPtr>(mce::RenderMaterialGroup::common(),
                                                         HashedString{"selection_box"});
    if (!lightMaterial || !lightMaterial->mRenderMaterialInfoPtr.get()) {
        availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::LightOverlay)));
        resetScan(); return;
    }
    auto const generation = worldGeneration.load(std::memory_order_acquire);
    if (generation != previousGeneration || previousOptions != current->lightOptions) {
        resetScan(); previousGeneration = generation; previousOptions = current->lightOptions;
    }
    if (!alphaLayer) {
        Vec3 const position = player->getPosition();
        int const x = blockFloor(position.x), y = blockFloor(position.y), z = blockFloor(position.z);
        if (!anchorValid || std::abs(x-anchorX)>1 || std::abs(y-anchorY)>2 || std::abs(z-anchorZ)>1) {
            if (anchorValid) prune(x,y,z,current->lightOptions);
            anchorX=x; anchorY=y; anchorZ=z; anchorPosition=position;
            anchorValid=true; cursor=0; scanning=true; meshDirty=true;
        }
        if (!scanning && std::chrono::steady_clock::now() >= nextRefresh) {
            cursor=0;
            scannedCells = surfaceCells = brightnessReads = brightnessFailures = 0;
            scanning=true;
        }
        if (scanning) {
            auto& source = player->getDimension().getBlockSourceFromMainChunkSource();
            scan(source, current->lightOptions);
        }
        // Rebuild only after a complete scan. While the next scan is in
        // progress, keep rendering the previous buffered mesh.
        if (meshDirty && !scanning) {
            buildBufferedMesh(context, current->lightOptions);
        }
        return;
    }

    if (lightMesh && lightMesh->isValid()) {
        auto texture = whiteTexture(*renderer);
        if (!std::holds_alternative<std::monostate>(texture)) {
            Vec3 const camera = impl->mCameraPosition.get();
            auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
            matrix.mat->translate(-camera.x, -camera.y, -camera.z);
            lightMesh->renderMesh(
                context.mScreenContext,
                *lightMaterial,
                texture,
                0,
                lightMesh->mVertexCount.get().value_or(0u),
                emptyCapture(),
                nullptr
            );
            return;
        }
    }

    // Correctness fallback: if a buffered mesh cannot be produced on this
    // runtime, preserve the verified immediate path rather than hiding labels.
    renderImmediate(context, current->lightOptions);
}

void clearLightOverlay() noexcept {
    std::lock_guard lock(lightMutex);
    resetScan(); lightMaterial.reset(); lightTexture.reset(); previousGeneration = 0;
    availableFeatures.fetch_and(~(1u << static_cast<unsigned>(Feature::LightOverlay)));
}
} // namespace praxis::companion
