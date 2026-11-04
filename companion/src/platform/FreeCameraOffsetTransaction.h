#pragma once
#include <cmath>
#include <optional>
#include <utility>
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraDirectLookComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOrbitComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOffsetComponent.h"
#include "mc/deps/vanilla_camera/components/UpdatePlayerFromCameraComponent.h"

namespace praxis::companion {
struct FreeCameraOffsetSnapshot {
    bool added{};
    glm::vec3 original{},lastWritten{};
};
inline bool finiteFreeCameraOffset(glm::vec3 const& value) noexcept {
    return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
}
template<class Registry,class Entity>
bool freeCameraDirectShape(Registry const& ecs,Entity id) {
    return ecs.valid(id)&&ecs.template all_of<MinecraftCamera::ActiveCameraComponent,
        MinecraftCamera::CameraDirectLookComponent>(id)
        &&!ecs.template all_of<MinecraftCamera::CameraOrbitComponent>(id);
}
// The caller establishes current client/player/world/dimension/thread identity.
// This snapshot stores values only. Every operation takes a fresh SDK borrow.
template<class Registry,class Entity>
std::optional<FreeCameraOffsetSnapshot> takeFreeCameraOffset(Registry& ecs,Entity id) {
    using Offset=MinecraftCamera::CameraOffsetComponent;
    if(!freeCameraDirectShape(ecs,id)
        ||!ecs.template all_of<VanillaCamera::UpdatePlayerFromCameraComponent>(id))return {};
    FreeCameraOffsetSnapshot snapshot;
    if(auto offset=ecs.template try_get<Offset>(id)) {
        auto const& value=offset->mEntityOffset.get();
        if(!finiteFreeCameraOffset(value))return {};
        snapshot.original={value.x,value.y,value.z};
    } else {
        // Initialize all SDK fields of the component we create. Existing native
        // derived offsets are never copied back or replaced by this candidate.
        Offset initial{};
        initial.mEntityOffset=glm::vec3{0.f};initial.mViewOffset=glm::vec2{0.f};
        initial.mCalculatedOffset=glm::vec3{0.f};initial.mPivot=glm::vec3{0.f};
        initial.mCaclulatedViewOffset=glm::vec3{0.f};
        initial.mOriginalEntityOffset=glm::vec3{0.f};initial.mOriginalViewOffset=glm::vec2{0.f};
        ecs.template emplace<Offset>(id,std::move(initial));snapshot.added=true;
    }
    snapshot.lastWritten=snapshot.original;return snapshot;
}
template<class Registry,class Entity>
bool freeCameraOffsetMatches(Registry const& ecs,Entity id,FreeCameraOffsetSnapshot const& snapshot) {
    if(!freeCameraDirectShape(ecs,id)
        ||ecs.template all_of<VanillaCamera::UpdatePlayerFromCameraComponent>(id))return false;
    auto offset=ecs.template try_get<MinecraftCamera::CameraOffsetComponent>(id);
    if(!offset)return false;
    auto const& value=offset->mEntityOffset.get();
    // Detect a visible competing write. This is not proof of component-instance
    // continuity: same-value remove/re-add remains a stated runtime uncertainty.
    return finiteFreeCameraOffset(value)&&value.x==snapshot.lastWritten.x
        &&value.y==snapshot.lastWritten.y&&value.z==snapshot.lastWritten.z;
}
template<class Registry,class Entity>
bool writeFreeCameraOffset(Registry& ecs,Entity id,FreeCameraOffsetSnapshot& snapshot,glm::vec3 const& value) {
    if(!finiteFreeCameraOffset(value)||!freeCameraOffsetMatches(ecs,id,snapshot))return false;
    auto offset=ecs.template try_get<MinecraftCamera::CameraOffsetComponent>(id);
    offset->mEntityOffset->x=value.x;offset->mEntityOffset->y=value.y;offset->mEntityOffset->z=value.z;
    snapshot.lastWritten=value;return true;
}
template<class Registry,class Entity>
bool restoreFreeCameraOffset(Registry& ecs,Entity id,FreeCameraOffsetSnapshot const& snapshot) {
    if(!freeCameraOffsetMatches(ecs,id,snapshot))return false;
    if(snapshot.added)ecs.template remove<MinecraftCamera::CameraOffsetComponent>(id);
    else {
        auto offset=ecs.template try_get<MinecraftCamera::CameraOffsetComponent>(id);
        offset->mEntityOffset->x=snapshot.original.x;offset->mEntityOffset->y=snapshot.original.y;
        offset->mEntityOffset->z=snapshot.original.z;
    }
    return true;
}
}
