#pragma once
class LevelRendererCamera;
class ScreenContext;
struct ViewRenderObject;
namespace praxis::companion {
bool installWeatherChanger();
bool removeWeatherChanger() noexcept;
void pollWeatherChangerDiagnostics(bool requested) noexcept;
void expireWeatherChangerDiagnostics() noexcept;
void observeWeatherSkyConsumer(LevelRendererCamera const&,ScreenContext const&,ViewRenderObject const&,bool originSelected) noexcept;
}
