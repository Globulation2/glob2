// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ResourceClaims::Mixed
{
// Explicit experimental assets. Tags: bit0 initial nearest origin, bit1 direct
// resource check. Coordinates are canonical; no runtime rotation in the loop.
struct Entry { std::int16_t x,y; std::uint8_t tag; };
struct View { std::span<const std::int16_t> x,y; std::span<const std::uint8_t> tag; };
inline constexpr std::string_view Format="mixed-v1";
inline constexpr std::string_view Transform="nearest-d4-v1";
inline constexpr std::string_view IllustrativeName="illustrative258-middle13-v1";
inline constexpr std::array<Entry,13> Illustrative{{
    {0,0,0},
    {1,0,1},
    {-9,34,1},
    {6,3,1},
    {18,-40,0},
    {3,1,3},
    {-10,10,1},
    {2,-30,0},
    {5,-62,2},
    {12,11,2},
    {-53,-18,1},
    {2,2,0},
    {51,-24,0}
}};
template<std::size_t N> struct Planes {
    std::array<std::int16_t,N> x{},y{};
    std::array<std::uint8_t,N> tag{};
    constexpr View view() const {return {x,y,tag};}
};
template<std::size_t N> constexpr auto allFrames(const std::array<Entry,N>& entries)
{
    std::array<Planes<N>,8> result{};
    for(unsigned f=0;f<8;++f)for(std::size_t i=0;i<N;++i) {
        int x=(f&4)?entries[i].y:entries[i].x;
        int y=(f&4)?entries[i].x:entries[i].y;
        result[f].x[i]=std::int16_t((f&1)?-x:x);
        result[f].y[i]=std::int16_t((f&2)?-y:y);
        result[f].tag[i]=entries[i].tag;
    }
    return result;
}
inline constexpr auto IllustrativeFrames=allFrames(Illustrative);
// Powers-of-two map dimensions and valid origin are guaranteed by the engine.
// Missing/coincident nearest is identity; half-world ties are negative.
constexpr unsigned frame(unsigned width,unsigned height,unsigned origin,unsigned nearest)
{
    if(nearest==UINT32_MAX)return 0;
    const int dx=int((nearest%width-origin%width+width/2)&(width-1))-int(width/2);
    const int dy=int((nearest/width-origin/width+height/2)&(height-1))-int(height/2);
    const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    return unsigned(dx<0)|(unsigned(dy<0)<<1)|(unsigned(ay>ax)<<2);
}
inline View view(std::string_view name,unsigned direction)
{
    if(name!=IllustrativeName || direction>=8)return {};
    return IllustrativeFrames[direction].view();
}
}
