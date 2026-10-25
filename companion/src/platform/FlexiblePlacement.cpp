#include "platform/FlexiblePlacement.h"
#include "platform/Native.h"
#include "core/FlexiblePlacementPolicy.h"
#include "core/FlexibleStairPlacementPolicy.h"
#include "core/FlexiblePlacementClickFence.h"
#include "core/HotkeyPolicy.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "platform/LholoFlexiblePlacementAbi.h"
#include "platform/HandRestock.h"
#include "platform/InventoryRequest.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
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
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/phys/HitResult.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace praxis::companion {
namespace {

struct LiveOverlay {
    FlexiblePlacementPlan plan{};
    FlexibleUV uv{};
    FlexibleHorizontal playerFacing{FlexibleHorizontal::South};
    unsigned long long generation{};
    FlexibleAdmissionReason admissionReason{FlexibleAdmissionReason::Accepted};
    bool valid{};
};

std::mutex placementMutex;
FlexibleInputLatch placementLatch;
FlexiblePlacementClickFence flexibleClickFence;
LiveOverlay liveOverlay;
std::atomic_bool hookInstalled{};
std::atomic_bool lholoNoticeShown{};
std::atomic_bool lholoAbiNoticeShown{};
std::atomic<ULONGLONG> lastAdmissionNotice{};
std::atomic<std::uint64_t> flexibleExternalEpoch{1};
std::atomic_bool flexibleExternalPending{};
constexpr std::uint64_t flexibleExternalOwnerToken = 0x505241584953464Cull;

bool gameHasFocus() noexcept {
    DWORD process{};
    auto window = GetForegroundWindow();
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}

bool keyDown(int key) noexcept {
    return key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
}

unsigned currentModifiers() noexcept {
    return modifierBits(keyDown(VK_CONTROL), keyDown(VK_SHIFT), keyDown(VK_MENU));
}

bool chordDown(int key, unsigned modifiers) noexcept {
    return keyDown(key) && modifiersMatch(modifiers, currentModifiers());
}

unsigned heldModes(Settings const& settings) noexcept {
    auto const& options = settings.flexiblePlacement;
    unsigned modes{};
    if (chordDown(options.rotationKey, options.rotationModifiers)) modes |= FlexibleRotation;
    if (chordDown(options.offsetKey, options.offsetModifiers)) modes |= FlexibleOffset;
    if (chordDown(options.adjacentKey, options.adjacentModifiers)) modes |= FlexibleAdjacent;
    return modes;
}

struct LholoModuleLease {
    HMODULE module{};
    LholoModuleLease() noexcept {
        if (!GetModuleHandleExW(0, L"LHolo.dll", &module))
            GetModuleHandleExW(0, L"lholo.dll", &module);
    }
    ~LholoModuleLease() { if (module) FreeLibrary(module); }
    LholoModuleLease(LholoModuleLease const&) = delete;
    LholoModuleLease& operator=(LholoModuleLease const&) = delete;
};

using LholoStatusV2Fn = lholo_abi::FlexiblePlacementStatusV2(__cdecl*)();
using LholoQueueV2Fn = std::uint32_t(__cdecl*)(lholo_abi::FlexiblePlacementRequestV2);
using LholoCancelV2Fn = std::uint32_t(__cdecl*)(std::uint64_t, std::uint64_t);

enum class LholoPlacementState : std::uint8_t { Absent, Idle, Owned, ExternalPending, AbiUnavailable };

LholoStatusV2Fn lholoStatusFn(HMODULE module) noexcept {
    return module ? reinterpret_cast<LholoStatusV2Fn>(GetProcAddress(
        module, "lholo_flexible_placement_v2_get_status")) : nullptr;
}

LholoQueueV2Fn lholoQueueFn(HMODULE module) noexcept {
    return module ? reinterpret_cast<LholoQueueV2Fn>(GetProcAddress(
        module, "lholo_flexible_placement_v2_queue")) : nullptr;
}

LholoPlacementState lholoPlacementState() noexcept {
    LholoModuleLease lease;
    if (!lease.module) return LholoPlacementState::Absent;
    auto const status = lholoStatusFn(lease.module);
    if (!status) return LholoPlacementState::AbiUnavailable;
    auto const state = status();
    if (state.statusBits != 0) return LholoPlacementState::Owned;
    if (state.ownerToken != 0) {
        return state.ownerToken == flexibleExternalOwnerToken
            && state.ownerEpoch == flexibleExternalEpoch.load(std::memory_order_acquire)
            ? LholoPlacementState::ExternalPending : LholoPlacementState::Owned;
    }
    return LholoPlacementState::Idle;
}

void lholoAbiNotice() noexcept {
    if (!lholoAbiNoticeShown.exchange(true))
        logGuiInfo("FlexiblePlacement: LHolo placement ABI is unavailable; update LHolo for safe coexistence");
}

bool useAllowed() noexcept {
    return runtimeReady.load(std::memory_order_acquire)
        && inputAllowed.load(std::memory_order_acquire) && gameHasFocus() && !guiInputCaptured();
}

std::optional<FlexibleUV> absoluteHitUV(BlockPos const& pos, Vec3 const& hit,
    FlexibleFace face, FlexibleHorizontal playerFacing) noexcept {
    return flexibleUVFromAbsoluteHitForFacing(
        {pos.x, pos.y, pos.z}, {hit.x, hit.y, hit.z}, face, playerFacing);
}

std::optional<FlexibleUV> physicalAbsoluteHitUV(BlockPos const& pos, Vec3 const& hit,
    FlexibleFace face) noexcept {
    return flexibleUVFromAbsoluteHit(
        {pos.x, pos.y, pos.z}, {hit.x, hit.y, hit.z}, face);
}

std::optional<FlexibleUV> relativeHitUV(Vec3 const& hit, FlexibleFace face) noexcept {
    return flexibleUVFromRelativeHit({hit.x, hit.y, hit.z}, face);
}

std::optional<FlexibleUV> selectionHitUV(Vec3 const& hit, FlexibleFace face,
    FlexibleHorizontal playerFacing) noexcept {
    return flexibleUVFromRelativeHitForFacing({hit.x, hit.y, hit.z}, face, playerFacing);
}

FlexibleCell cellOf(BlockPos const& pos) noexcept { return {pos.x, pos.y, pos.z}; }
BlockPos blockPosOf(FlexibleCell cell) noexcept { return BlockPos{cell.x, cell.y, cell.z}; }

bool blockItem(ItemStack const& item) noexcept {
    return !item.isNull() && item.mCount != 0 && item.mBlock != nullptr;
}

void admissionNotice(FlexibleAdmissionReason reason) noexcept {
    char const* message = nullptr;
    switch (reason) {
    case FlexibleAdmissionReason::DestinationOccupied: message = "FlexiblePlacement: destination occupied"; break;
    case FlexibleAdmissionReason::MissingSupport: message = "FlexiblePlacement: no supporting block"; break;
    case FlexibleAdmissionReason::OutOfReach: message = "FlexiblePlacement: destination out of reach"; break;
    case FlexibleAdmissionReason::NotBlockItem: message = "FlexiblePlacement: held item is not a block"; break;
    case FlexibleAdmissionReason::InteractiveTarget: message = "FlexiblePlacement: interactive target left unchanged"; break;
    case FlexibleAdmissionReason::CompetingPlacement: message = "FlexiblePlacement: another placement owner is active"; break;
    case FlexibleAdmissionReason::PlacementAbiUnavailable: message = "FlexiblePlacement: compatible LHolo placement ABI is unavailable"; break;
    case FlexibleAdmissionReason::NativeUnsupported: message = "FlexiblePlacement: rotation delivery is unsupported for this action"; break;
    case FlexibleAdmissionReason::RepeatSuppressed: message = "FlexiblePlacement: repeated transformed input suppressed"; break;
    default: break;
    }
    if (!message) return;
    auto const now = GetTickCount64();
    auto previous = lastAdmissionNotice.load(std::memory_order_relaxed);
    while (now >= previous && now - previous >= 750
        && !lastAdmissionNotice.compare_exchange_weak(previous, now, std::memory_order_relaxed)) {}
    if (now >= previous && now - previous >= 750) logGuiInfo(message);
}

bool withinReach(LocalPlayer const& player, Vec3 const& hit) noexcept {
    auto eye = player.getEyePos();
    auto const dx = hit.x - eye.x, dy = hit.y - eye.y, dz = hit.z - eye.z;
    auto const pickRange = player.getPickRange();
    return std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz)
        && std::isfinite(pickRange) && pickRange > 0.0f
        && dx * dx + dy * dy + dz * dz <= pickRange * pickRange;
}

bool stairBlockItem(ItemStack const& item) noexcept {
    return blockItem(item)
        && (static_cast<std::uint64_t>(item.mBlock->getBlockType().mProperties)
            & static_cast<std::uint64_t>(BlockProperty::Stair)) != 0;
}

bool supportlessBlockAllowed(ItemStack const& item) noexcept {
    return item.mBlock && item.mBlock->getBlockType().mIsOpaqueFullBlock
        && !item.mBlock->getBlockType().isSlabBlock();
}

struct FlexibleDispatchGeometry {
    FlexibleCell requestSupport{};
    bool airTarget{};
    FlexibleHitPoint worldHit{};
};

FlexibleDispatchGeometry dispatchGeometry(
    FlexiblePlacementPlan const& plan, FlexibleUV rawUv, Block const& supportBlock) noexcept {
    auto const airTarget = supportBlock.isAir();
    auto const requestSupport = airTarget ? plan.destination : plan.useSupport;
    return {requestSupport, airTarget,
        flexibleWorldHit(requestSupport, plan.placementFace, rawUv)};
}

std::optional<FlexiblePlacementPlan> livePlan(LocalPlayer& player, unsigned modes) {
    if (!modes || !useAllowed()) return std::nullopt;
    auto client = ll::service::getClientInstance();
    if (!client || client->getLocalPlayer() != &player) return std::nullopt;
    auto const& hit = client->getLatestHitResult();
    if (hit.mType != HitResultType::Tile || hit.mFacing >= 6) return std::nullopt;
    auto const face = static_cast<FlexibleFace>(hit.mFacing);
    auto const facing = horizontalFacingFromYaw(player.getRotation().y);
    auto uv = absoluteHitUV(hit.mBlock, hit.mPos, face, facing);
    if (!uv) return std::nullopt;
    auto const slot = player.getSelectedItemSlot();
    if (slot < 0 || slot >= 9) return std::nullopt;
    auto const& held = player.getInventory().getItem(slot);
    auto plan = planFlexiblePlacementForItem(
        cellOf(hit.mBlock), face, *uv, modes, facing, stairBlockItem(held));
    return plan.valid ? std::optional<FlexiblePlacementPlan>{plan} : std::nullopt;
}

void clearOverlayLocked() noexcept { liveOverlay = {}; }

InteractionResult safeFlexibleFailure() noexcept { return InteractionResult{false, false}; }

bool queueLholoPlacement(
    ItemStack const& item, FlexiblePlacementPlan const& plan, FlexibleUV uv,
    Block const& support, bool airTarget, LocalPlayer const& player, unsigned modes) {
    LholoModuleLease lease;
    auto const queue = lholoQueueFn(lease.module);
    if (!queue) return false;
    auto const relative = flexibleRelativeHit(plan.placementFace, uv);
    lholo_abi::FlexiblePlacementRequestV2 request{};
    request.version = lholo_abi::kVersion2;
    request.flags = lholo_abi::RequestGameplay | lholo_abi::RequestFocused
        | lholo_abi::RequestGuiClear | lholo_abi::RequestModifiers;
    if (airTarget) request.flags |= lholo_abi::RequestAirTarget;
    if (hasFlexibleMode(modes, FlexibleRotation))
        request.flags |= lholo_abi::RequestRotationSync | lholo_abi::RequestRotationMode;
    if (hasFlexibleMode(modes, FlexibleOffset)) request.flags |= lholo_abi::RequestOffsetMode;
    if (hasFlexibleMode(modes, FlexibleAdjacent)) request.flags |= lholo_abi::RequestAdjacentMode;
    request.ownerToken = flexibleExternalOwnerToken;
    request.ownerEpoch = flexibleExternalEpoch.load(std::memory_order_acquire);
    request.worldGeneration = worldGeneration.load(std::memory_order_acquire);
    request.dimension = static_cast<int>(player.getDimensionId());
    auto const requestSupport = airTarget ? plan.destination : plan.useSupport;
    request.supportX = requestSupport.x;
    request.supportY = requestSupport.y;
    request.supportZ = requestSupport.z;
    auto const& region = player.getDimensionBlockSource();
    request.targetBlockId = airTarget
        ? region.getBlock(blockPosOf(plan.destination)).mNetworkId : support.mNetworkId;
    request.face = static_cast<std::uint32_t>(plan.placementFace);
    request.selectedSlot = player.getSelectedItemSlot();
    request.itemId = item.getId();
    request.itemAux = item.getIdAux();
    request.itemCount = item.mCount;
    request.relativeHitX = relative.x;
    request.relativeHitY = relative.y;
    request.relativeHitZ = relative.z;
    // Tweakeroo changes the network yaw for the flexible action but preserves
    // the player's actual pitch. LHolo mirrors that exact pair into one
    // genuine PlayerAuthInput packet; plan.desiredPitch is not a fake camera.
    request.pitch = player.getRotation().x;
    request.yaw = hasFlexibleMode(modes, FlexibleRotation) ? plan.desiredYaw : player.getRotation().y;
    request.maxPickRange = player.getPickRange();
    auto const queued = queue(request);
    auto const accepted = queued == lholo_abi::QueueAccepted;
    auto const message = std::format(
        "FLEX_LOCAL queue={} sector={} modes={} face={} target=({},{},{}) originalYaw={:.2f} requestedYaw={:.2f} pitch={:.2f}",
        queued, static_cast<unsigned>(plan.region), modes, request.face,
        plan.destination.x,plan.destination.y,plan.destination.z,
        player.getRotation().y,request.yaw,request.pitch);
    logGuiInfo(message.c_str());
    if (accepted) {
        flexibleExternalPending.store(true, std::memory_order_release);
        std::lock_guard lock(placementMutex);
        flexibleClickFence.accepted(plan.destination, reinterpret_cast<std::uintptr_t>(&player),
            worldGeneration.load(std::memory_order_acquire), GetTickCount64());
    }
    return accepted;
}

void cancelLholoExternal() noexcept {
    if (!flexibleExternalPending.exchange(false, std::memory_order_acq_rel)) return;
    auto const epoch = flexibleExternalEpoch.load(std::memory_order_acquire);
    LholoModuleLease lease;
    if (lease.module) {
        auto const cancel = reinterpret_cast<LholoCancelV2Fn>(GetProcAddress(
            lease.module, "lholo_flexible_placement_v2_cancel"));
        if (cancel) (void)cancel(flexibleExternalOwnerToken, epoch);
    }
    flexibleExternalEpoch.fetch_add(1, std::memory_order_acq_rel);
}

enum class FlexibleTransformDecision : std::uint8_t { NotApplicable, Handled, Blocked };

FlexibleTransformDecision tryFlexibleTransform(
    GameMode& gameMode, ItemStack const& item, BlockPos const& pos, uchar face, Vec3 const& hit,
    HandSlot hand, bool first, bool interactiveTarget, bool hitValid, bool& transforming) {
    auto client = ll::service::getClientInstance();
    auto* local = client ? client->getLocalPlayer() : nullptr;
    if (!local || &gameMode.mPlayer != static_cast<Player*>(local)) return {};
    auto& player = *local;
    auto current = settings();
    if (!current || !current->on(Feature::FlexibleBlockPlacement) || !useAllowed()) return {};
    if (hand != HandSlot::Mainhand) return {};
    // Check before the ordinary interactive-block passthrough. A held use
    // callback can now hit the freshly placed trapdoor instead of its support.
    if (interactiveTarget) {
        bool fenced{};
        {
            std::lock_guard lock(placementMutex);
            fenced = flexibleClickFence.blocks(cellOf(pos),
                reinterpret_cast<std::uintptr_t>(&player),
                worldGeneration.load(std::memory_order_acquire), keyDown(VK_RBUTTON), GetTickCount64());
        }
        if (fenced) {
            transforming = true;
            static std::atomic_uint reported{};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 128)
                logGuiInfo("FLEX_LOCAL blocked=post-placement-interaction same-held-click");
            return FlexibleTransformDecision::Blocked;
        }
    }
    auto const selectedSlot = player.getSelectedItemSlot();
    if (selectedSlot < 0 || selectedSlot >= 9) return {};
    auto const& selected = player.getInventory().getItem(selectedSlot);
    if (!blockItem(item) || !blockItem(selected) || item.getId() != selected.getId()
        || item.getIdAux() != selected.getIdAux() || item.mCount != selected.mCount
        || !item.matchesNetIdVariant(selected) || interactiveTarget) return {};
    auto const modes = heldModes(*current);
    if (!modes) return {};
    transforming = true;
    auto const lholoState = lholoPlacementState();
    if (lholoState == LholoPlacementState::Owned
        || lholoState == LholoPlacementState::ExternalPending) {
        admissionNotice(FlexibleAdmissionReason::CompetingPlacement);
        static std::atomic<ULONGLONG> lastOwnerTrace{};
        auto const now = GetTickCount64();
        if (now - lastOwnerTrace.load(std::memory_order_relaxed) > 1000) {
            lastOwnerTrace.store(now,std::memory_order_relaxed);
            logGuiInfo(lholoState == LholoPlacementState::ExternalPending
                ? "FLEX_LOCAL blocked=own-pending-request"
                : "FLEX_LOCAL blocked=lholo-mode-or-other-owner");
        }
        return FlexibleTransformDecision::Blocked;
    }
    if (lholoState == LholoPlacementState::AbiUnavailable
        || lholoState == LholoPlacementState::Absent) {
        lholoAbiNotice();
        admissionNotice(FlexibleAdmissionReason::PlacementAbiUnavailable);
        return FlexibleTransformDecision::Blocked;
    }
    if (!first) {
        admissionNotice(FlexibleAdmissionReason::RepeatSuppressed);
        return FlexibleTransformDecision::Blocked;
    }
    if (!hitValid || handRestockPlacementBusy() || inventory::requestPathBusy()
        || face >= 6) {
        admissionNotice(FlexibleAdmissionReason::InvalidPlan);
        return FlexibleTransformDecision::Blocked;
    }
    auto const typedFace = static_cast<FlexibleFace>(face);
    auto const rawUv = relativeHitUV(hit, typedFace);
    auto const selectionUv = selectionHitUV(hit, typedFace,
        horizontalFacingFromYaw(player.getRotation().y));
    if (!rawUv || !selectionUv) {
        admissionNotice(FlexibleAdmissionReason::InvalidPlan);
        return FlexibleTransformDecision::Blocked;
    }
    auto plan = planFlexiblePlacementForItem(cellOf(pos), typedFace, *selectionUv, modes,
        horizontalFacingFromYaw(player.getRotation().y), stairBlockItem(item));
    if (!plan.valid) {
        admissionNotice(FlexibleAdmissionReason::InvalidPlan);
        return FlexibleTransformDecision::Blocked;
    }
    auto& region = player.getDimensionBlockSource();
    plan = resolveFlexiblePositionSupport(plan, [&](FlexibleCell cell) {
        return !region.getBlock(blockPosOf(cell)).isAir();
    });
    auto const& supportBlock = region.getBlock(blockPosOf(plan.useSupport));
    auto const& destinationBlock = region.getBlock(blockPosOf(plan.destination));
    bool const supportPresent = !supportBlock.isAir() || supportlessBlockAllowed(item);
    auto const geometry = dispatchGeometry(plan, *rawUv, supportBlock);
    FlexibleAdmission admission{
        destinationBlock.isAir(), supportPresent,
        withinReach(player, Vec3{geometry.worldHit.x, geometry.worldHit.y, geometry.worldHit.z}),
        blockItem(item), false, false
    };
    auto const reason = admitFlexiblePlacement(plan, admission);
    if (reason != FlexibleAdmissionReason::Accepted) {
        admissionNotice(reason);
        return FlexibleTransformDecision::Blocked;
    }
    if (!queueLholoPlacement(item, plan, *rawUv, supportBlock, supportBlock.isAir(), player, modes)) {
        admissionNotice(FlexibleAdmissionReason::NativeUnsupported);
        return FlexibleTransformDecision::Blocked;
    }
    lholoNoticeShown.store(false, std::memory_order_relaxed);
    return FlexibleTransformDecision::Handled;
}

Vec3 latestHitFor(BlockPos const& pos, uchar face, bool& valid) {
    valid = false;
    auto client = ll::service::getClientInstance();
    if (!client) return {};
    auto const& latest = client->getLatestHitResult();
    if (latest.mType != HitResultType::Tile || latest.mFacing >= 6
        || latest.mBlock != pos || latest.mFacing != face) return {};
    valid = true;
    // HitResult is world-space; the shared use-on adapter consumes a point
    // relative to the clicked support. Keep this conversion at the boundary.
    return Vec3{latest.mPos.x - static_cast<float>(pos.x),
                latest.mPos.y - static_cast<float>(pos.y),
                latest.mPos.z - static_cast<float>(pos.z)};
}

bool localFlexibleBuildCandidate(GameMode const& gameMode, HandSlot hand) {
    if (hand != HandSlot::Mainhand) return false;
    auto const current = settings();
    if (!current || !current->on(Feature::FlexibleBlockPlacement) || !useAllowed()
        || heldModes(*current) == 0) return false;
    auto const client = ll::service::getClientInstance();
    auto const* local = client ? client->getLocalPlayer() : nullptr;
    return local && &gameMode.mPlayer == static_cast<Player const*>(local)
        && local->getSelectedItemSlot() >= 0 && local->getSelectedItemSlot() < 9;
}

LL_TYPE_INSTANCE_HOOK(FlexibleStartBuild, ll::memory::HookPriority::High, GameMode,
    &GameMode::$startBuildBlock, void, BlockPos const& pos, uchar face, HandSlot hand) {
    NativeHookLifetime::Lease lifetimeLease(nativeHookLifetime());
    if (!lifetimeLease.admitted()) { origin(pos, face, hand); return; }
    bool transforming{};
    try {
        if (localFlexibleBuildCandidate(*this, hand)) {
            bool hitValid{};
            auto const hit = latestHitFor(pos, face, hitValid);
            auto const interactive = mPlayer.getDimensionBlockSource().getBlock(pos)
                .getBlockType().isInteractiveBlock();
            ItemStack const& item = mPlayer.getInventory().getItem(mPlayer.getSelectedItemSlot());
            auto const decision = tryFlexibleTransform(
                *this, item, pos, face, hit, hand, true, interactive, hitValid, transforming);
            if (decision != FlexibleTransformDecision::NotApplicable) return;
        }
    } catch (...) {
        if (transforming) return;
    }
    origin(pos, face, hand);
}

LL_TYPE_INSTANCE_HOOK(FlexibleBuildBlock, ll::memory::HookPriority::High, GameMode,
    &GameMode::$buildBlock, bool, BlockPos const& pos, uchar face, HandSlot hand, bool isSimTick) {
    NativeHookLifetime::Lease lifetimeLease(nativeHookLifetime());
    if (!lifetimeLease.admitted()) return origin(pos, face, hand, isSimTick);
    bool transforming{};
    try {
        if (localFlexibleBuildCandidate(*this, hand)) {
            bool hitValid{};
            auto const hit = latestHitFor(pos, face, hitValid);
            auto const interactive = mPlayer.getDimensionBlockSource().getBlock(pos)
                .getBlockType().isInteractiveBlock();
            ItemStack const& item = mPlayer.getInventory().getItem(mPlayer.getSelectedItemSlot());
            auto const decision = tryFlexibleTransform(
                *this, item, pos, face, hit, hand, !isSimTick, interactive, hitValid, transforming);
            if (decision != FlexibleTransformDecision::NotApplicable) return false;
        }
    } catch (...) {
        if (transforming) return false;
    }
    return origin(pos, face, hand, isSimTick);
}

LL_TYPE_INSTANCE_HOOK(FlexibleUseOn, ll::memory::HookPriority::High, GameMode,
    &GameMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* target, bool first) {
    NativeHookLifetime::Lease lifetimeLease(nativeHookLifetime());
    auto const original = [&] { return origin(item, pos, face, hit, hand, target, first); };
    if (!lifetimeLease.admitted()) return original();
    bool transforming{};
    try {
        auto const decision = tryFlexibleTransform(
            *this, item, pos, face, hit, hand, first,
            target && target->getBlockType().isInteractiveBlock(), true, transforming);
        if (decision == FlexibleTransformDecision::Handled) return InteractionResult{true, true};
        if (decision == FlexibleTransformDecision::Blocked) return safeFlexibleFailure();
    } catch (...) {
        if (transforming) return safeFlexibleFailure();
    }
    return original();
}

struct TextureVariant {
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> value;
};
std::unique_ptr<mce::MaterialPtr> placementMaterial;

TextureVariant whiteTexture(LevelRenderer& renderer) {
    auto const& group = renderer.mTextureGroup.get();
    if (!group) return {};
    auto texture = group->getTexture(ResourceLocation{Core::PathView{"textures/ui/white_background"}}, false,
        std::nullopt, cg::TextureSetLayerType::Color);
    if (!texture.mClientTexture || texture.mClientTexture->mIsMissingTexture == IsMissingTexture::Yes) return {};
    TextureVariant result;
    result.value = std::move(texture);
    return result;
}

OffscreenCaptureDescription const& emptyCapture() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

void setColor(Tessellator& tessellator, std::uint32_t color) {
    tessellator.color(static_cast<float>(color & 0xff) / 255.0f,
        static_cast<float>((color >> 8) & 0xff) / 255.0f,
        static_cast<float>((color >> 16) & 0xff) / 255.0f,
        static_cast<float>((color >> 24) & 0xff) / 255.0f);
}

Vec3 facePoint(FlexibleCell cell, FlexibleFace face, float u, float v) {
    constexpr float epsilon = 0.004f;
    switch (face) {
    case FlexibleFace::Down: return {cell.x + u, cell.y - epsilon, cell.z + v};
    case FlexibleFace::Up: return {cell.x + u, cell.y + 1.0f + epsilon, cell.z + v};
    case FlexibleFace::North: return {cell.x + u, cell.y + v, cell.z - epsilon};
    case FlexibleFace::South: return {cell.x + u, cell.y + v, cell.z + 1.0f + epsilon};
    case FlexibleFace::West: return {cell.x - epsilon, cell.y + v, cell.z + u};
    case FlexibleFace::East: return {cell.x + 1.0f + epsilon, cell.y + v, cell.z + u};
    }
    return {};
}

using Line = std::pair<Vec3, Vec3>;
void addFaceGrid(std::vector<Line>& lines, FlexibleCell cell, FlexibleFace face,
    FlexibleHorizontal playerFacing) {
    auto add = [&](float u0, float v0, float u1, float v1) {
        auto const first = flexibleRawUVFromOriented({u0, v0}, face, playerFacing);
        auto const second = flexibleRawUVFromOriented({u1, v1}, face, playerFacing);
        lines.emplace_back(facePoint(cell, face, first.u, first.v),
            facePoint(cell, face, second.u, second.v));
    };
    add(0, 0, 1, 0); add(1, 0, 1, 1); add(1, 1, 0, 1); add(0, 1, 0, 0);
    for (float u : {0.25f, 0.75f}) add(u, 0.25f, u, 0.75f);
    for (float v : {0.25f, 0.75f}) add(0.25f, v, 0.75f, v);
    add(0.0f, 0.0f, 0.25f, 0.25f);
    add(0.0f, 1.0f, 0.25f, 0.75f);
    add(0.75f, 0.25f, 1.0f, 0.0f);
    add(0.75f, 0.75f, 1.0f, 1.0f);
}

void addSelectedRegion(std::vector<Line>& lines, FlexibleCell cell, FlexibleFace face,
    FlexibleRegion region, FlexibleHorizontal playerFacing) {
    auto const polygon = flexibleRegionPolygon(region);
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        auto const next = (index + 1) % polygon.size();
        auto const first = flexibleRawUVFromOriented(polygon[index], face, playerFacing);
        auto const second = flexibleRawUVFromOriented(polygon[next], face, playerFacing);
        lines.emplace_back(facePoint(cell, face, first.u, first.v),
            facePoint(cell, face, second.u, second.v));
    }
}

void addCube(std::vector<Line>& lines, FlexibleCell cell) {
    Vec3 p[8]{{float(cell.x),float(cell.y),float(cell.z)}, {float(cell.x+1),float(cell.y),float(cell.z)},
        {float(cell.x+1),float(cell.y+1),float(cell.z)}, {float(cell.x),float(cell.y+1),float(cell.z)},
        {float(cell.x),float(cell.y),float(cell.z+1)}, {float(cell.x+1),float(cell.y),float(cell.z+1)},
        {float(cell.x+1),float(cell.y+1),float(cell.z+1)}, {float(cell.x),float(cell.y+1),float(cell.z+1)}};
    constexpr int edges[][2]{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto const& edge : edges) lines.emplace_back(p[edge[0]], p[edge[1]]);
}

void renderLines(BaseActorRenderContext& context, std::vector<Line> const& lines, std::uint32_t color,
    char const* meshName, std::unique_ptr<mce::MaterialPtr>& material) {
    if (lines.empty()) return;
    auto* renderer = context.mClientInstance.getLevelRenderer();
    auto const& impl = context.mImpl.get();
    if (!renderer || !impl) return;
    if (!material || !material->mRenderMaterialInfoPtr.get())
        material = std::make_unique<mce::MaterialPtr>(mce::RenderMaterialGroup::common(), HashedString{"selection_box"});
    auto texture = whiteTexture(*renderer);
    if (std::holds_alternative<std::monostate>(texture.value)) return;
    auto& tessellator = context.mScreenContext.tessellator;
    tessellator.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::LineList,
        static_cast<int>(lines.size() * 2), false);
    setColor(tessellator, color);
    for (auto const& line : lines) { tessellator.vertex(line.first.x,line.first.y,line.first.z); tessellator.vertex(line.second.x,line.second.y,line.second.z); }
    auto mesh = tessellator.end(Tessellator::UploadMode::Buffered, meshName, SupplementaryFieldAutoGenerationMode{0});
    if (!mesh.isValid()) return;
    auto camera = impl->mCameraPosition.get();
    auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(-camera.x, -camera.y, -camera.z);
    mesh.renderMesh(context.mScreenContext, *material, texture.value, 0,
        mesh.mVertexCount.get().value_or(0u), emptyCapture(), nullptr);
}

} // namespace

extern "C" __declspec(dllexport)
lholo_abi::FlexiblePlacementOwnerStatusV2 praxis_flexible_placement_v2_get_owner_status(
    lholo_abi::FlexiblePlacementOwnerQueryV2 query) noexcept {
    using namespace lholo_abi;
    FlexiblePlacementOwnerStatusV2 status{};
    status.version = kVersion2;
    status.ownerToken = flexibleExternalOwnerToken;
    status.ownerEpoch = flexibleExternalEpoch.load(std::memory_order_acquire);
    status.worldGeneration = worldGeneration.load(std::memory_order_acquire);
    try {
        if (query.version != kVersion2 || query.reserved != 0) return status;
        auto const current = settings();
        auto const client = ll::service::getClientInstance();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        bool const tokenMatch = query.ownerToken == flexibleExternalOwnerToken;
        bool const epochMatch = query.ownerEpoch == status.ownerEpoch;
        bool const worldMatch = query.worldGeneration == status.worldGeneration;
        bool const gameplay = runtimeReady.load(std::memory_order_acquire)
            && inputAllowed.load(std::memory_order_acquire)
            && client && player && client->isInGameInputEnabled();
        bool const focused = gameHasFocus();
        bool const guiClear = !guiInputCaptured();
        bool const featureEnabled = current
            && current->on(Feature::FlexibleBlockPlacement);
        unsigned modes{};
        if (current && player) modes = heldModes(*current);
        status.modeFlags = modes;
        status.dimension = player ? static_cast<int>(player->getDimensionId()) : 0;
        status.selectedSlot = player ? player->getSelectedItemSlot() : -1;
        if (player && status.selectedSlot >= 0 && status.selectedSlot < 9) {
            auto const& item = player->getInventory().getItem(status.selectedSlot);
            status.itemId = item.getId();
            status.itemAux = item.getIdAux();
            status.itemCount = item.mCount;
        }
        bool const dimensionMatch = player
            && status.dimension == query.dimension;
        bool stackMatch = player && status.selectedSlot == query.selectedSlot
            && status.itemId == query.itemId && status.itemAux == query.itemAux
            && status.itemCount == query.itemCount;
        auto const modeMatches = [&] {
            unsigned requested{};
            if (query.flags & RequestRotationMode) requested |= FlexibleRotation;
            if (query.flags & RequestOffsetMode) requested |= FlexibleOffset;
            if (query.flags & RequestAdjacentMode) requested |= FlexibleAdjacent;
            return requested != 0 && modes == requested;
        };
        bool const live = tokenMatch && epochMatch && worldMatch && featureEnabled && gameplay
            && focused && guiClear && (modes != 0)
            && dimensionMatch && stackMatch && modeMatches();
        if (featureEnabled) status.flags |= OwnerFeatureEnabled;
        if (gameplay) status.flags |= OwnerGameplay;
        if (focused) status.flags |= OwnerFocused;
        if (guiClear) status.flags |= OwnerGuiClear;
        if (modes != 0) status.flags |= OwnerModifiers;
        if (worldMatch) status.flags |= OwnerWorldMatch;
        if (dimensionMatch) status.flags |= OwnerDimensionMatch;
        if (stackMatch) status.flags |= OwnerStackMatch;
        if (modeMatches()) status.flags |= OwnerModeMatch;
        if (tokenMatch) status.flags |= OwnerTokenMatch;
        if (epochMatch) status.flags |= OwnerEpochMatch;
        if (live) status.flags |= OwnerLive;
    } catch (...) {
        status.flags = 0;
    }
    return status;
}

void tickFlexiblePlacement(LocalPlayer& player) noexcept {
    try {
        auto current = settings();
        if (!current) { resetFlexiblePlacement(); return; }
        bool const enabled = current->on(Feature::FlexibleBlockPlacement);
        auto const lholoState = lholoPlacementState();
        bool const allowed = enabled && useAllowed();
        auto modes = allowed ? heldModes(*current) : 0u;
        if (!allowed || !modes || lholoState == LholoPlacementState::Owned
            || lholoState == LholoPlacementState::AbiUnavailable
            || lholoState == LholoPlacementState::Absent) cancelLholoExternal();
        std::lock_guard lock(placementMutex);
        flexibleClickFence.observe(keyDown(VK_RBUTTON),
            worldGeneration.load(std::memory_order_acquire), GetTickCount64());
        placementLatch.update(enabled, useAllowed(), gameHasFocus(), guiInputCaptured(),
            (modes & FlexibleRotation) != 0, (modes & FlexibleOffset) != 0, (modes & FlexibleAdjacent) != 0);
        if (!allowed || !placementLatch.active) { clearOverlayLocked(); return; }
        auto plan = livePlan(player, modes);
        if (!plan) { cancelLholoExternal(); clearOverlayLocked(); return; }
        auto client = ll::service::getClientInstance();
        auto const& hit = client->getLatestHitResult();
        auto const facing = horizontalFacingFromYaw(player.getRotation().y);
        auto uv = physicalAbsoluteHitUV(hit.mBlock, hit.mPos,
            static_cast<FlexibleFace>(hit.mFacing));
        if (!uv) { cancelLholoExternal(); clearOverlayLocked(); return; }
        auto const& selected = player.getInventory().getItem(player.getSelectedItemSlot());
        auto& region = player.getDimensionBlockSource();
        *plan = resolveFlexiblePositionSupport(*plan, [&](FlexibleCell cell) {
            return !region.getBlock(blockPosOf(cell)).isAir();
        });
        auto const& supportBlock = region.getBlock(blockPosOf(plan->useSupport));
        auto const& destinationBlock = region.getBlock(blockPosOf(plan->destination));
        bool const supportPresent = !supportBlock.isAir() || supportlessBlockAllowed(selected);
        auto const geometry = dispatchGeometry(*plan, *uv, supportBlock);
        FlexibleAdmission admission{
            destinationBlock.isAir(), supportPresent,
            withinReach(player, Vec3{geometry.worldHit.x, geometry.worldHit.y, geometry.worldHit.z}),
            blockItem(selected), false,
            lholoState != LholoPlacementState::Idle
        };
        auto reason = admitFlexiblePlacement(*plan, admission);
        if (reason == FlexibleAdmissionReason::Accepted
            && lholoState != LholoPlacementState::Idle
            && lholoState != LholoPlacementState::ExternalPending)
            reason = lholoState == LholoPlacementState::Owned
                ? FlexibleAdmissionReason::CompetingPlacement
                : FlexibleAdmissionReason::PlacementAbiUnavailable;
        liveOverlay.plan = *plan;
        liveOverlay.uv = *uv;
        liveOverlay.playerFacing = facing;
        liveOverlay.generation = worldGeneration.load(std::memory_order_acquire);
        liveOverlay.admissionReason = reason;
        liveOverlay.valid = true;
    } catch (...) {
        resetFlexiblePlacement();
    }
}

void resetFlexiblePlacement() noexcept {
    cancelLholoExternal();
    std::lock_guard lock(placementMutex);
    placementLatch.clear();
    flexibleClickFence.clear();
    clearOverlayLocked();
}

bool flexiblePlacementModifiersHeld() noexcept {
    std::lock_guard lock(placementMutex);
    return placementLatch.active;
}

void renderFlexiblePlacementOverlay(BaseActorRenderContext& context, bool alphaLayer) noexcept {
    if (alphaLayer) return;
    try {
        LiveOverlay snapshot;
        bool modifiersHeld{};
        { std::lock_guard lock(placementMutex); snapshot = liveOverlay; modifiersHeld = placementLatch.active; }
        auto current = settings();
        if (!snapshot.valid || !modifiersHeld || !current || !current->on(Feature::FlexibleBlockPlacement)
            || snapshot.generation != worldGeneration.load(std::memory_order_acquire)) return;
        std::vector<Line> grid, selected, destination;
        addFaceGrid(grid, snapshot.plan.originalSupport, snapshot.plan.originalFace, snapshot.playerFacing);
        addSelectedRegion(selected, snapshot.plan.originalSupport, snapshot.plan.originalFace,
            snapshot.plan.region, snapshot.playerFacing);
        addCube(destination, snapshot.plan.destination);
        renderLines(context, grid, 0xB04DD7FFu, "PraxisFlexiblePlacementGrid", placementMaterial);
        renderLines(context, selected, 0xE0FFD34Du, "PraxisFlexiblePlacementSelected", placementMaterial);
        auto const color = snapshot.admissionReason == FlexibleAdmissionReason::Accepted
            ? 0xE04DFF7Au : 0xE02D2DFFu;
        renderLines(context, destination, color, "PraxisFlexiblePlacementDestination", placementMaterial);
    } catch (...) {}
}

bool installFlexiblePlacement() {
    if (hookInstalled.load(std::memory_order_acquire)) return true;
    if (FlexibleStartBuild::hook() != 0) return false;
    if (FlexibleBuildBlock::hook() != 0) {
        FlexibleStartBuild::unhook();
        return false;
    }
    if (FlexibleUseOn::hook() != 0) {
        FlexibleBuildBlock::unhook();
        FlexibleStartBuild::unhook();
        return false;
    }
    hookInstalled.store(true, std::memory_order_release);
    availableFeatures.fetch_or(featureRegistryBit(static_cast<unsigned>(Feature::FlexibleBlockPlacement)));
    return true;
}

bool removeFlexiblePlacement() noexcept {
    resetFlexiblePlacement();
    availableFeatures.fetch_and(~featureRegistryBit(static_cast<unsigned>(Feature::FlexibleBlockPlacement)));
    if (!hookInstalled.exchange(false, std::memory_order_acq_rel)) return true;
    bool const use = FlexibleUseOn::unhook() == 0;
    bool const build = FlexibleBuildBlock::unhook() == 0;
    bool const start = FlexibleStartBuild::unhook() == 0;
    return use && build && start;
}

} // namespace praxis::companion
