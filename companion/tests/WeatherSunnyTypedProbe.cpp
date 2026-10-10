#include "core/WeatherSunnyPolicy.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/deps/core/math/Color.h"
#include <cstddef>
#include <memory>
#include <type_traits>

using Parameters=praxis::companion::WeatherSunnyParameters<mce::Color>;
static_assert(sizeof(Parameters)==108 && alignof(Parameters)==4);
static_assert(std::is_trivially_copyable_v<Parameters>);
static_assert(offsetof(Parameters,rainSkyWeight)==0x00);
static_assert(offsetof(Parameters,untouched4)==0x04);
static_assert(offsetof(Parameters,untouched8)==0x08);
static_assert(offsetof(Parameters,rainSkyColor)==0x0c);
static_assert(offsetof(Parameters,rainCloudColor)==0x1c);
static_assert(offsetof(Parameters,rainCloudWeight)==0x2c);
static_assert(offsetof(Parameters,thunderSkyColor)==0x30);
static_assert(offsetof(Parameters,thunderSkyWeight)==0x40);
static_assert(offsetof(Parameters,thunderCloudColor)==0x44);
static_assert(offsetof(Parameters,thunderCloudWeight)==0x54);
static_assert(offsetof(Parameters,flashSkyColor)==0x58);
static_assert(offsetof(Parameters,flashSkyWeight)==0x68);
using SunnyBrightnessMethod=float (Dimension::*)(float) const;
using SunnyClockMethod=float (Dimension::*)(int,float) const;
using SunnyRuntimeBrightness=float (*)(Dimension const*,float);
using SunnyPreRenderMethod=void (LevelRendererPlayer::*)(ScreenContext&,LevelRenderPreRenderUpdateParameters&);
using SunnyLightMethod=std::unique_ptr<BaseLightData> (BaseLightTextureImageBuilder::*)(IClientInstance*,BaseLightData const&);
static_assert(std::is_same_v<decltype(&Dimension::getSkyDarken),SunnyBrightnessMethod>);
static_assert(std::is_same_v<decltype(&Dimension::$getTimeOfDay),SunnyClockMethod>);
static_assert(std::is_same_v<decltype(&LevelRendererPlayer::$preRenderUpdate),SunnyPreRenderMethod>);
static_assert(std::is_same_v<decltype(&BaseLightTextureImageBuilder::$createBaseLightTextureData),SunnyLightMethod>);
static_assert(std::is_same_v<decltype(std::declval<BaseLightData&>().mSkyDarken),float>);
// Compile-only: no fabricated engine instance, guessed offset write, or game call.
