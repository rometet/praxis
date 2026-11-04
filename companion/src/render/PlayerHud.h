#pragma once
namespace ll::event::inline render { class AfterUIRenderEvent; }
namespace praxis::companion {
bool foodHudNeeded() noexcept;
void drawFoodHud(void*) noexcept;
void drawNativeOffhandHud(ll::event::AfterUIRenderEvent&);
}
