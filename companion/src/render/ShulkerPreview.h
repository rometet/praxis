#pragma once
class ItemStack;
class ItemRenderer;
class BaseActorRenderContext;
namespace praxis::companion {
// Synchronous owned-HUD call. Reuses the existing observer recursion guard;
// a Companion HUD must never replace the captured vanilla preview context.
void renderOwnedHudItem(ItemRenderer&,BaseActorRenderContext&,ItemStack const&,float x,float y,float scale);
bool previewItemWithinBudget(ItemStack const& item) noexcept;
bool installShulkerPreview();
bool removeShulkerPreview() noexcept;
bool shulkerPreviewNeeded() noexcept;
void drawShulkerPreviewContent(void* context) noexcept;
}
