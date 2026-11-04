#pragma once
#include "mc/entity/components/RawMoveInputComponent.h"
#include "mc/input/MoveInputState.h"

namespace praxis::companion {
inline void withholdFreeCameraMovement(RawMoveInputComponent& raw) noexcept {
    raw.mRawMove->x=0.f;raw.mRawMove->y=0.f;
    raw.mRawInput->mAnalogMoveVector->x=0.f;raw.mRawInput->mAnalogMoveVector->y=0.f;
    auto& flags=raw.mRawInput->mFlagValues.get();
    for(int flag=0;flag<static_cast<int>(MoveInputState::Flag::Count);++flag) {
        if(flag==static_cast<int>(MoveInputState::Flag::BlockSelectDown)
            ||flag==static_cast<int>(MoveInputState::Flag::LookCenter))continue;
        flags.set(static_cast<unsigned>(flag),false);
    }
    // Selection and look fields belong to their existing native/GUI owners.
}
}
