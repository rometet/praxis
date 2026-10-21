#pragma once
namespace ll::event::inline render {class AfterUIRenderEvent;}
namespace praxis::companion {
// A rejected item or native draw must not interrupt the shared preview callback.
void drawNativeInventoryHud(ll::event::AfterUIRenderEvent& event) noexcept;
}
