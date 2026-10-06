// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cassert>
#include <cstdint>
#include <vector>

namespace AIMaxima::FarmGeometry
{
// Map dimensions are positive powers of two. Keep toroidal aliases on thin
// maps: these operations are OR/AND, so repeated neighbors retain their meaning.
inline void validate(int width,int height)
{
    assert(width>0 && !(width&(width-1)) && height>0 && !(height&(height-1)));
}
inline std::vector<std::uint8_t> adjacentSeeds(const std::vector<std::uint8_t>& seeds,
    int width,int height)
{
    validate(width,height);
    std::vector<std::uint8_t> adjacent(width*height,0);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
    {
        const auto bits=seeds[y*width+x];
        if(!bits) continue;
        for(int dy=-1;dy<=1;++dy)
        {
            const int row=((y+dy)&(height-1))*width;
            for(int dx=-1;dx<=1;++dx)
                if(dx||dy) adjacent[row+((x+dx)&(width-1))]|=bits;
        }
    }
    return adjacent;
}
template<class FoodAt>
std::vector<std::uint8_t> foodExterior(int width,int height,FoodAt foodAt)
{
    validate(width,height);
    std::vector<std::uint8_t> dilated(width*height,0),exterior(width*height,0);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
    {
        if(!foodAt(y*width+x)) continue;
        for(int dy=-1;dy<=1;++dy)
        {
            const int row=((y+dy)&(height-1))*width;
            for(int dx=-1;dx<=1;++dx) dilated[row+((x+dx)&(width-1))]=1;
        }
    }
    for(int y=0;y<height;++y)
    {
        const int rows[]={((y-1)&(height-1))*width,y*width,((y+1)&(height-1))*width};
        for(int x=0;x<width;++x)
        {
            bool inside=true;
            for(int row:rows) for(int dx=-1;dx<=1;++dx)
                inside=inside && dilated[row+((x+dx)&(width-1))];
            exterior[y*width+x]=!inside;
        }
    }
    return exterior;
}
}
