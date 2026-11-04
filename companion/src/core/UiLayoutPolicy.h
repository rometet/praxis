#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace praxis::companion {
struct HudRect { float x{}, y{}, width{}, height{}; };
inline bool hudIntersects(HudRect a, HudRect b, float gap=8) noexcept {
    return a.width>0&&a.height>0&&b.width>0&&b.height>0
        &&a.x<b.x+b.width+gap&&a.x+a.width+gap>b.x
        &&a.y<b.y+b.height+gap&&a.y+a.height+gap>b.y;
}
// Keep requested anchors when possible; move an intersecting panel to the
// nearest free edge of the reserved HUD regions without changing saved config.
inline HudRect placeHud(HudRect requested,float width,float height,std::span<HudRect const> obstacles) noexcept {
    auto clamp=[&](HudRect r) {
        r.width=std::clamp(r.width,0.0f,std::max(0.0f,width));
        r.height=std::clamp(r.height,0.0f,std::max(0.0f,height));
        r.x=std::clamp(r.x,0.0f,std::max(0.0f,width-r.width));
        r.y=std::clamp(r.y,0.0f,std::max(0.0f,height-r.height));return r;
    };
    auto clear=[&](HudRect r) {for(auto o:obstacles)if(hudIntersects(r,o))return false;return true;};
    auto best=clamp(requested);if(clear(best))return best;
    float distance=std::numeric_limits<float>::max();
    auto consider=[&](float x,float y) {
        auto r=clamp({x,y,requested.width,requested.height});if(!clear(r))return;
        float d=(r.x-requested.x)*(r.x-requested.x)+(r.y-requested.y)*(r.y-requested.y);
        if(d<distance){best=r;distance=d;}
    };
    for(auto a:obstacles) {
        for(float x:{requested.x,a.x+a.width+8,a.x-requested.width-8})
            for(float y:{requested.y,a.y+a.height+8,a.y-requested.height-8})consider(x,y);
        // Intersections may require crossing the edges of two different panels.
        for(auto b:obstacles)consider(a.x+a.width+8,b.y+b.height+8);
    }
    return best;
}
// Minecraft reserves its top-left logical HUD corner for the paper doll.
inline HudRect paperDollBounds(float logicalPerPixelX,float logicalPerPixelY) noexcept {
    if(!std::isfinite(logicalPerPixelX)||!std::isfinite(logicalPerPixelY)
       ||logicalPerPixelX<=0||logicalPerPixelY<=0)return {};
    return {0,0,72/logicalPerPixelX,80/logicalPerPixelY};
}
inline float nativeTextScale(float pixels,float logicalPerPixelY,float logicalLineHeight) noexcept {
    if(!std::isfinite(pixels)||!std::isfinite(logicalPerPixelY)||!std::isfinite(logicalLineHeight)
       ||pixels<=0||logicalPerPixelY<=0||logicalLineHeight<=0)return 0;
    return pixels*logicalPerPixelY/logicalLineHeight;
}
inline bool currentPreviewDraw(unsigned long long generation,unsigned long long revision,
    unsigned long long drawnGeneration,unsigned long long drawnRevision,
    unsigned long long drawnMs,unsigned long long now) noexcept {
    return drawnMs&&generation==drawnGeneration&&revision==drawnRevision&&now>=drawnMs&&now-drawnMs<=220;
}
} // namespace praxis::companion
