// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// The learned claim search pattern: 24 checks around a fetching worker, fitted
// offline to recorded claim decisions so that, in order, they find claimable
// stock as early as possible within a 32-tile radius. Selection walks the
// entries in order and takes the first available claim, so the order is part
// of the simulation and the entries must never change without a SIM_REVISION.
namespace ResourceClaims::Pattern
{
// Each entry is an offset plus a tag. Tag bit 0: the offset is taken from the
// nearest material destination recorded at the worker's cell (the destination
// plane), instead of from the worker. Tag bit 1: check the resource on the
// probed tile itself; otherwise check the destination the plane records there.
struct Entry { std::int16_t x,y; std::uint8_t tag; };
struct View { std::span<const std::int16_t> x,y; std::span<const std::uint8_t> tag; };
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
// Entries are written in the canonical frame, where the nearest destination
// lies at dx>=0, dy>=0, dy<=dx from the worker. The eight dihedral frames map
// that layout onto the worker's actual direction to its nearest destination,
// so the same offsets apply on every side of a field and the search loop never
// rotates coordinates. Fit identity (provenance only):
// c1f194c374d584fbcf95a28ae42b7595b7cbbf1cd1fb05a9ad46cc653f242438
inline constexpr std::string_view Name="final-r32-exact24-assembled-v1";
inline constexpr std::array<Entry,24> Entries{{
    {0,0,0},
    {1,0,1},
    {-1,0,0},
    {13,-6,1},
    {-1,7,1},
    {-28,-8,0},
    {6,31,0},
    {25,14,1},
    {4,-21,1},
    {-2,-2,0},
    {21,-13,0},
    {-26,12,0},
    {-13,-29,0},
    {31,-1,0},
    {-5,29,1},
    {-9,30,0},
    {6,-2,1},
    {21,24,1},
    {2,-30,0},
    {5,29,1},
    {-30,-1,0},
    {28,-13,0},
    {1,-2,3},
    {1,1,3},
}};
inline constexpr auto Frames=allFrames(Entries);
// The frame index for a worker at origin whose nearest destination is nearest:
// bit 0 mirrors x, bit 1 mirrors y, bit 2 swaps the axes. Map dimensions are
// powers of two. No (or a coincident) nearest destination is the identity
// frame; a half-world tie counts as negative.
constexpr unsigned frame(unsigned width,unsigned height,unsigned origin,unsigned nearest)
{
    if(nearest==UINT32_MAX)return 0;
    const int dx=int((nearest%width-origin%width+width/2)&(width-1))-int(width/2);
    const int dy=int((nearest/width-origin/width+height/2)&(height-1))-int(height/2);
    const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    return unsigned(dx<0)|(unsigned(dy<0)<<1)|(unsigned(ay>ax)<<2);
}
inline View view(unsigned direction) { return direction<8 ? Frames[direction].view() : View{}; }
}
