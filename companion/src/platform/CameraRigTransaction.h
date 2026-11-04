#pragma once
#include "core/CameraCandidatePolicy.h"
#include "mc/deps/minecraft_camera/components/CameraDirectLookComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOffsetComponent.h"
#include "mc/deps/vanilla_camera/components/UpdatePlayerFromCameraComponent.h"

namespace praxis::companion {
// SDK 26.51.5: Offset has no instance generation and ENTT_NO_MIXIN storage
// provides no construct/destroy observer. Entity version/address/field equality
// cannot prove component continuity after remove/re-add. No backend is present.
inline constexpr bool candidateCameraOffsetContinuitySupported=false;
// Caller proves ownership, registry lifetime and callback thread first.
template<class Registry,class Entity>
bool writeCandidateCamera(Registry& registry,Entity id,CameraPose const& pose,
                          std::optional<glm::vec3> offset={}) {
    // Reject before any orientation or component mutation. Never treat equal
    // addresses/values as an Offset instance-lifetime proof (including ABA).
    if(offset&&!candidateCameraOffsetContinuitySupported)return false;
    using Direct=MinecraftCamera::CameraDirectLookComponent;
    using Offset=MinecraftCamera::CameraOffsetComponent;
    using Link=VanillaCamera::UpdatePlayerFromCameraComponent;
    if(!registry.valid(id)||registry.template all_of<Link>(id)) return false;
    auto* direct=registry.template try_get<Direct>(id);
    auto* position=registry.template try_get<Offset>(id);
    if(!direct || (offset&&!position) || !std::isfinite(pose.yaw)||!std::isfinite(pose.pitch)
        || !validCameraPitchBounds(direct->mPitchMin,direct->mPitchMax)
        || pose.pitch<direct->mPitchMin||pose.pitch>direct->mPitchMax) return false;
    if(offset && (!std::isfinite(offset->x)||!std::isfinite(offset->y)||!std::isfinite(offset->z))) return false;
    direct->mYaw=pose.yaw; direct->mPitch=pose.pitch; direct->mYawDelta=0.0f;
    if(offset) position->mEntityOffset=*offset;
    return true;
}
template<class Registry,class Entity>
bool restoreCandidateCamera(Registry& registry,Entity id,CameraPose const& original,
    VanillaCamera::UpdatePlayerFromCameraComponent::LookMode mode,std::optional<glm::vec3> offset={}) {
    // Write orientation/position before enabling the engine's camera-to-player copy.
    if(!writeCandidateCamera(registry,id,original,offset)) return false;
    registry.template emplace<VanillaCamera::UpdatePlayerFromCameraComponent>(id).mLookMode=mode;
    return true;
}
}
