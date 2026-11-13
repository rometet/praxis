#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace praxis::companion::foodshape {
// Derive a thin silhouette from the active resource pack's hunger background;
// no Lamium source code or texture is bundled. Four-neighbor contour pixels
// follow the icon itself, not the square UI element's bounding rectangle.
enum class Portion { Full, RightHalf, LeftHalf };
enum class Unit { None, Half, Full };
inline Unit units(float value,int icon) noexcept {
    if (!std::isfinite(value)||icon<0||icon>=10)return Unit::None;
    int const amount=static_cast<int>(std::floor(std::clamp(value,0.f,20.f)));
    if(amount>=2*icon+2)return Unit::Full;
    if(amount==2*icon+1)return Unit::Half;
    return Unit::None;
}
struct Rgba {std::uint8_t r{},g{},b{},a{255};};
inline std::vector<std::uint8_t> contour(
    std::span<std::uint8_t const> src,unsigned width,unsigned height,Rgba shade,Portion part
) {
    if(!width||!height||width>256||height>256||src.size()!=std::size_t(width)*height*4)
        return {};
    std::vector<std::uint8_t> result(src.size(),0);
    auto opaque=[&](int x,int y) noexcept {
        return x>=0&&y>=0&&x<static_cast<int>(width)&&y<static_cast<int>(height)
            &&src[(std::size_t(y)*width+std::size_t(x))*4+3]>=128;
    };
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        if((part==Portion::RightHalf && x<width/2)
            ||(part==Portion::LeftHalf && x>=width/2))continue;
        if(!opaque(static_cast<int>(x),static_cast<int>(y)))continue;
        if(opaque(static_cast<int>(x)-1,static_cast<int>(y))
            &&opaque(static_cast<int>(x)+1,static_cast<int>(y))
            &&opaque(static_cast<int>(x),static_cast<int>(y)-1)
            &&opaque(static_cast<int>(x),static_cast<int>(y)+1))continue;
        auto const index=(std::size_t(y)*width+x)*4;
        result[index]=shade.r;result[index+1]=shade.g;
        result[index+2]=shade.b;result[index+3]=shade.a;
    }
    return result;
}
} // namespace praxis::companion::foodshape
