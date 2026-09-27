#pragma once
namespace praxis::companion {
bool installShulkerPreview();
bool removeShulkerPreview() noexcept;
bool shulkerPreviewNeeded() noexcept;
void drawShulkerPreviewContent(void* context) noexcept;
}
