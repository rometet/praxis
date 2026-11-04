#pragma once
#include "map/MapRenderer.h"
#include <chrono>
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace praxis::companion::map {
// Called only by the selected existing host at frame admission/graphics reset.
// This owns resource references, never another ImGui/Present/input lifecycle.
void setMapGraphics(ID3D11Device*,ID3D11DeviceContext*) noexcept;
void clearMapGraphics() noexcept;
bool drawTerrainTexture(ImDrawList&,Snapshot const&,Canvas const&);
std::optional<std::chrono::steady_clock::time_point> firstMapSubmission(Session const&) noexcept;
}
