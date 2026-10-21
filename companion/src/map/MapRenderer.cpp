#include "map/MapRenderer.h"
#include "map/MapCore.h"
#include "map/MapGraphics.h"
#include <cfloat>
#include <array>
#include <cstdio>
#include <imgui.h>

namespace praxis::companion::map {
namespace {
ImVec2 v(Point p) { return {float(p.x), float(p.z)}; }
bool inside(Point p, Canvas const &c, double inset = 0) {
    auto dx = p.x - c.transform.screenCenter.x, dz = p.z - c.transform.screenCenter.z;
    auto r = c.halfSize - inset;
    return c.circular ? dx * dx + dz * dz <= r * r : std::abs(dx) <= r && std::abs(dz) <= r;
}
// Sutherland-Hodgman clips only boundary cells to a fixed 32-sided circle.
// All scratch is stack-owned and draw work stays bounded by the supplied cells.
void clippedQuad(ImDrawList &draw, std::array<Point, 4> const &quad, Canvas const &c, std::uint32_t color) {
    if (!c.circular) {
        draw.AddQuadFilled(v(quad[0]), v(quad[1]), v(quad[2]), v(quad[3]), color);
        return;
    }
    bool all = true;
    for (auto p : quad)
        all &= inside(p, c, c.halfSize * 0.005);
    if (all) {
        draw.AddQuadFilled(v(quad[0]), v(quad[1]), v(quad[2]), v(quad[3]), color);
        return;
    }
    std::array<Point, 40> a{}, b{};
    std::copy(quad.begin(), quad.end(), a.begin());
    int count = 4;
    for (int edge = 0; edge < 32 && count; edge++) {
        double angle = (2 * edge + 1) * 3.14159265358979323846 / 32;
        Point normal{std::cos(angle), std::sin(angle)};
        double const distance = c.halfSize * std::cos(3.14159265358979323846 / 32);
        auto side = [&](Point p) {
            return normal.x * (p.x - c.transform.screenCenter.x) +
                   normal.z * (p.z - c.transform.screenCenter.z) - distance;
        };
        int out = 0;
        for (int i = 0; i < count; ++i) {
            auto p = a[i], q = a[(i + 1) % count];
            auto dp = side(p), dq = side(q);
            bool ip = dp <= 0, iq = dq <= 0;
            if (ip && out < int(b.size()))
                b[out++] = p;
            if (ip != iq && out < int(b.size())) {
                double t = dp / (dp - dq);
                b[out++] = {p.x + t * (q.x - p.x), p.z + t * (q.z - p.z)};
            }
        }
        count = out;
        a = b;
    }
    if (count >= 3) {
        std::array<ImVec2, 40> points{};
        for (int i = 0; i < count; ++i)
            points[i] = v(a[i]);
        draw.AddConvexPolyFilled(points.data(), count, color);
    }
}
// A legacy host or a failed texture allocation still displays the map. Bound
// that exceptional CPU path by its display resolution, while retaining every
// one-block colour sample in the immutable image and the persistent cache.
// The selected DX11 host uses the full image with point sampling above this path.
bool drawImageFallback(ImDrawList &draw, Snapshot const &snapshot, Canvas const &c) {
    TerrainImage const *image = nullptr;
    double best = 1e30;
    for (std::size_t i=0;i<snapshot.terrainImages.size();++i) {
        if(c.viewMask!=3&&!(c.viewMask&(1u<<i)))continue;
        auto const& candidate=snapshot.terrainImages[i];
        if (!candidate || candidate->side < 1 || candidate->side > 513 ||
            candidate->rgba.size() != std::size_t(candidate->side) * candidate->side)
            continue;
        double score = std::hypot(candidate->x - c.transform.worldCenter.x,
                                  candidate->z - c.transform.worldCenter.z) +
                       std::abs(candidate->radius - c.halfSize / c.transform.pixelsPerBlock);
        if (score < best) { best = score; image = candidate.get(); }
    }
    if (!image) return false;
    int const side = std::clamp(int(std::ceil(2 * c.halfSize)), 1, 192);
    double const step = 2 * c.halfSize / side;
    double const loX = c.transform.screenCenter.x - c.halfSize;
    double const loZ = c.transform.screenCenter.z - c.halfSize;
    double const cosine = std::cos(c.transform.angle), sine = std::sin(c.transform.angle);
    auto colorAt = [&](int x, int z) {
        double dx = (loX + (x + .5) * step - c.transform.screenCenter.x) / c.transform.pixelsPerBlock;
        double dz = (loZ + (z + .5) * step - c.transform.screenCenter.z) / c.transform.pixelsPerBlock;
        int ix = int(std::floor(c.transform.worldCenter.x + cosine * dx + sine * dz + .5)) - image->x + image->radius;
        int iz = int(std::floor(c.transform.worldCenter.z - sine * dx + cosine * dz + .5)) - image->z + image->radius;
        return ix >= 0 && iz >= 0 && ix < image->side && iz < image->side
                   ? image->rgba[std::size_t(iz) * image->side + ix] : 0u;
    };
    for (int z = 0; z < side; ++z) for (int x = 0; x < side; ++x) {
        auto color = colorAt(x, z);
        int end = x;
        while (end + 1 < side && colorAt(end + 1, z) == color) ++end;
        if (color >> 24) {
            double left = loX + x * step, right = loX + (end + 1) * step;
            double top = loZ + z * step, bottom = top + step;
            clippedQuad(draw, {{{left, top}, {right, top}, {right, bottom}, {left, bottom}}}, c, color);
        }
        x = end;
    }
    return true;
}
} // namespace
void drawMapLabel(ImDrawList &draw, char const* text,float x,float y,float width,float height) {
    if(!text||!std::isfinite(width)||!std::isfinite(height)||width<32||height<16)return;
    float fontSize=std::min(ImGui::GetFontSize(),std::max(8.0f,width/30));
    auto size=ImGui::GetFont()->CalcTextSizeA(fontSize,FLT_MAX,width-16,text);
    x=std::clamp(x,8.0f,std::max(8.0f,width-size.x-8));
    // Glyph ink may descend beyond CalcTextSizeA by a pixel after snapping.
    y=std::clamp(y,2.0f,std::max(2.0f,height-size.y-4));
    draw.PushClipRect({0,0},{width,height},true);
    ImVec4 clip{0,0,width,height};
    draw.AddText(ImGui::GetFont(),fontSize,{x,y},0xffffffff,text,nullptr,width-16,&clip);
    draw.PopClipRect();
}
void configureCanvas(Canvas &c, Settings const &s) noexcept {
    c.snap = s.snapPixels;
    c.radarNames = s.radarNames;
    c.distanceRadar = s.distanceRadar;
    c.waypoints = s.waypointMarkers;
    c.caveShade = s.caveShade;
    c.radarCategories = s.radarCategories;
    if (!s.radar)
        c.radarCategories.fill(false);
}
std::size_t drawMapCanvas(ImDrawList &draw, Snapshot const &snapshot, Canvas const &c) {
    if (!snapshot.cells || !std::isfinite(c.halfSize) || c.halfSize < 1 || c.halfSize > 1024 ||
        !std::isfinite(c.transform.pixelsPerBlock) || c.transform.pixelsPerBlock <= 0 ||
        c.transform.pixelsPerBlock > 128 || !std::isfinite(c.transform.angle) ||
        !coordinate(c.transform.worldCenter.x) || !coordinate(c.transform.worldCenter.z) ||
        !std::isfinite(c.transform.screenCenter.x) || !std::isfinite(c.transform.screenCenter.z))
        return 0;
    Point const lo{c.transform.screenCenter.x - c.halfSize, c.transform.screenCenter.z - c.halfSize},
        hi{c.transform.screenCenter.x + c.halfSize, c.transform.screenCenter.z + c.halfSize};
    draw.PushClipRect(v(lo), v(hi), true);
    if (c.circular)
        draw.AddCircleFilled(v(c.transform.screenCenter), float(c.halfSize), 0xec202225, 64);
    else
        draw.AddRectFilled(v(lo), v(hi), 0xec202225);
    std::size_t rendered = 0;
    bool texture=drawTerrainTexture(draw,snapshot,c);
    bool image=texture||drawImageFallback(draw,snapshot,c);
    if(image) {
        for(std::size_t i=0;i<snapshot.terrainImages.size();++i)
            if((c.viewMask==3||(c.viewMask&(1u<<i)))&&snapshot.terrainImages[i])
                rendered+=snapshot.terrainImages[i]->known;
    }
    std::size_t visited = 0;
    if(!image)for (auto const &cell : *snapshot.cells) {
        if (++visited > maxCells)
            break;
        if (rendered >= 4096)
            break;
        if (!(cell.viewMask & c.viewMask))
            continue;
        int stride = cell.strideFor(c.viewMask);
        if (stride < 1 || stride > 9)
            continue;
        double half = stride * 0.5;
        if (cell.key.layer != snapshot.layer || (snapshot.layer == Layer::Cave &&
            (cell.key.caveTop != snapshot.caveTop || cell.key.caveBottom != snapshot.caveBottom)))
            continue;
        auto center = c.transform.projectPixel({double(cell.x), double(cell.z)}, c.snap);
        double radius = half * c.transform.pixelsPerBlock * 1.415;
        if (std::abs(center.x - c.transform.screenCenter.x) > c.halfSize + radius ||
            std::abs(center.z - c.transform.screenCenter.z) > c.halfSize + radius)
            continue;
        if (c.circular) {
            double dx = center.x - c.transform.screenCenter.x, dz = center.z - c.transform.screenCenter.z;
            if (dx * dx + dz * dz > (c.halfSize + radius) * (c.halfSize + radius))
                continue;
        }
        std::array<Point, 4> q{c.transform.projectPixel({cell.x - half, cell.z - half}, c.snap),
                               c.transform.projectPixel({cell.x + half, cell.z - half}, c.snap),
                               c.transform.projectPixel({cell.x + half, cell.z + half}, c.snap),
                               c.transform.projectPixel({cell.x - half, cell.z + half}, c.snap)};
        auto color = cell.sample.known ? cell.sample.color :
            ((floorDiv(cell.x, stride) + floorDiv(cell.z, stride)) & 1 ? 0xff30383f : 0xff252c32);
        if (cell.sample.known && snapshot.layer == Layer::Cave && c.caveShade) {
            double depth = std::clamp(double(snapshot.caveTop - cell.sample.height) /
                                     std::max(1, snapshot.caveTop - snapshot.caveBottom), 0., 1.);
            double brightness = 1 - .3 * depth;
            color = (color & 0xff000000u) | std::uint32_t((color & 255) * brightness) |
                (std::uint32_t(((color >> 8) & 255) * brightness) << 8) |
                (std::uint32_t(((color >> 16) & 255) * brightness) << 16);
        }
        clippedQuad(draw, q, c, color);
        ++rendered;
    }
    std::size_t markers = 0;
    auto ordered = c.waypoints ? orderedWaypoints(snapshot.waypoints, snapshot.session.dimension,
                                                 c.transform.worldCenter, true) : std::vector<std::size_t>{};
    for (auto index : ordered) {
        auto const &w = snapshot.waypoints[index];
        if (w.dimension != snapshot.session.dimension || markers >= 256)
            continue;
        auto p = c.transform.projectPixel({w.x, w.z}, c.snap);
        if (!inside(p, c, 7))
            continue;
        draw.AddTriangleFilled({float(p.x), float(p.z) - 6}, {float(p.x) - 5, float(p.z) + 4},
                               {float(p.x) + 5, float(p.z) + 4}, w.color);
        ++markers;
    }
    std::size_t radarCount = 0, labels = 0;
    for (auto const &entity : snapshot.radar) {
        if (++radarCount > 128)
            break;
        auto category = std::size_t(entity.category);
        if (category >= c.radarCategories.size() || !c.radarCategories[category])
            continue;
        auto p = c.transform.projectPixel({entity.x, entity.z}, c.snap);
        if (!inside(p, c, 5))
            continue;
        auto color = entity.color;
        if (c.distanceRadar) {
            double distance = std::hypot(entity.x - snapshot.playerX, entity.z - snapshot.playerZ);
            double range = c.halfSize / c.transform.pixelsPerBlock;
            auto alpha = std::uint32_t(255 - 96 * std::clamp(distance / range, 0., 1.));
            color = (color & 0x00ffffffu) | (alpha << 24);
        }
        switch (elevation(entity.y, snapshot.playerY)) {
        case Elevation::Above:
            draw.AddTriangleFilled({float(p.x), float(p.z)-4}, {float(p.x)-3, float(p.z)+2},
                                   {float(p.x)+3, float(p.z)+2}, color); break;
        case Elevation::Below:
            draw.AddTriangle({float(p.x), float(p.z)+4}, {float(p.x)-3, float(p.z)-2},
                             {float(p.x)+3, float(p.z)-2}, color, 1.5f); break;
        case Elevation::Level: draw.AddCircleFilled(v(p), 3, color, 8); break;
        }
        if (c.radarNames && labels < 16 && !entity.name.empty()) {
            auto end = entity.name.c_str() + std::min<std::size_t>(63, entity.name.size());
            auto size = ImGui::CalcTextSize(entity.name.c_str(), end);
            Point endPoint{p.x + 6 + size.x, p.z + size.y};
            if (inside({p.x + 6, p.z - 3}, c, 2) && inside(endPoint, c, 2) &&
                inside({endPoint.x, p.z - 3}, c, 2) && inside({p.x + 6, endPoint.z}, c, 2)) {
                draw.AddText({float(p.x)+6, float(p.z)-3}, color, entity.name.c_str(), end);
                ++labels;
            }
        }
    }
    auto player = c.transform.projectPixel({snapshot.playerX, snapshot.playerZ}, c.snap);
    if (inside(player, c, 9)) {
        double const angle = (snapshot.yaw + 90) * 3.14159265358979323846 / 180 + c.transform.angle;
        Point tip{player.x + std::cos(angle) * 8, player.z + std::sin(angle) * 8},
            left{player.x + std::cos(angle + 2.5) * 6, player.z + std::sin(angle + 2.5) * 6},
            right{player.x + std::cos(angle - 2.5) * 6, player.z + std::sin(angle - 2.5) * 6};
        draw.AddTriangleFilled(v(tip), v(left), v(right), 0xffffffff);
        draw.AddTriangle(v(tip), v(left), v(right), 0xff111111, 1);
    }
    if (c.circular)
        draw.AddCircle(v(c.transform.screenCenter), float(c.halfSize), 0xffafb2bb, 64, 1.5f);
    else
        draw.AddRect(v(lo), v(hi), 0xffafb2bb, 0, 0, 1.5f);
    draw.PopClipRect();
    return rendered;
}
} // namespace praxis::companion::map
