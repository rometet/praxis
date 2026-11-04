#pragma once
#include "core/CameraCandidatePolicy.h"
class IClientInstance;
class LocalPlayer;
class LevelRendererPlayer;
struct MoveInputComponent;
struct RawMoveInputComponent;
namespace praxis::companion {
void cameraCandidateStart() noexcept;
void cameraCandidateTick(LocalPlayer&) noexcept;
void cameraCandidateBeforeTick(LocalPlayer&) noexcept;
void cameraCandidateRender(LevelRendererPlayer&) noexcept;
void cameraCandidateBeforeRender(LevelRendererPlayer&) noexcept;
void cameraCandidateWillChangeDimension(LevelRendererPlayer&) noexcept;
void cameraCandidateConsumeMovement(MoveInputComponent const&,RawMoveInputComponent&) noexcept;
void setFreeCameraMovementBackendAvailable(bool) noexcept;
void cameraCandidateExit(IClientInstance&) noexcept;
void adjustFreelookMouse(short& dx,short& dy,bool cancelled=false) noexcept;
bool cameraCandidateMouse(int action,int data,short dx,short dy,bool cancelled=false) noexcept;
bool cameraCandidateKey(int key,bool down,bool cancelled=false) noexcept;
bool cameraCandidateCleanup() noexcept;
void requestCameraCandidateReset() noexcept;
char const* cameraCandidateStatus() noexcept;
}
