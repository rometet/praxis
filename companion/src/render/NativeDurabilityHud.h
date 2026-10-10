#pragma once
namespace ll::event::inline render {class AfterUIRenderEvent;}
namespace praxis::companion {
// Invoked by the existing inspection UI-event owner. No retained item/controller/context pointer.
void drawNativeDurabilityHud(ll::event::AfterUIRenderEvent& event);
}
