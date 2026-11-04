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
// Frozen research candidates trained on3424 decisions; no default policy change.
// Source summary SHA256: 27a139b4b1e00cdd1888515bb6d2989a215dbb8a4f38588469816a8f10ad1c53
inline constexpr std::string_view LearnedR24Support4Name="discovery3424-r24-support4-v1";
inline constexpr std::array<Entry,21> LearnedR24Support4{{
    {0,0,0},
    {1,0,1},
    {-1,7,1},
    {13,-5,1},
    {23,5,1},
    {-21,-9,0},
    {0,21,1},
    {15,-17,1},
    {6,-7,1},
    {-11,12,1},
    {-20,12,0},
    {22,-4,1},
    {1,23,1},
    {-13,-20,0},
    {1,1,3},
    {0,2,3},
    {-13,20,0},
    {2,0,3},
    {0,-24,0},
    {14,15,1},
    {0,-1,3},
}};
inline constexpr auto LearnedR24Support4Frames=allFrames(LearnedR24Support4);
// Source summary SHA256: 51e2b063e813bec7a7dda5178ef02f911b9d08cccc4bec72fd75abf8a2c85c4e
inline constexpr std::string_view LearnedR32Support4Name="discovery3424-r32-support4-v1";
inline constexpr std::array<Entry,22> LearnedR32Support4{{
    {0,0,0},
    {1,0,1},
    {-1,7,1},
    {13,-5,1},
    {29,11,1},
    {-6,31,1},
    {-21,-9,0},
    {6,-7,1},
    {19,-21,1},
    {24,21,1},
    {-11,12,1},
    {-5,-31,1},
    {3,31,1},
    {-30,-8,0},
    {21,2,1},
    {-27,14,0},
    {20,-24,1},
    {1,1,3},
    {29,-7,0},
    {0,4,3},
    {2,0,3},
    {6,-28,0},
}};
inline constexpr auto LearnedR32Support4Frames=allFrames(LearnedR32Support4);
inline constexpr std::array<std::string_view,3> Names{IllustrativeName,LearnedR24Support4Name,LearnedR32Support4Name};
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
    if(direction>=8)return {};
    if(name==IllustrativeName)return IllustrativeFrames[direction].view();
    if(name==LearnedR24Support4Name)return LearnedR24Support4Frames[direction].view();
    if(name==LearnedR32Support4Name)return LearnedR32Support4Frames[direction].view();
    return {};
}
}
