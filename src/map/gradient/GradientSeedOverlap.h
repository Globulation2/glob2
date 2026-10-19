// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientFieldIdentity.h"
#include "field/GradientConstants.h"
#include <cstdint>
#include <cstddef>

namespace gradient_seed_overlap {
inline bool supported(GradientFieldKey key) {
    return key.swim<AIR_MOVEMENT_CLASS && (key.family==GradientFieldFamily::Materials || key.family==GradientFieldFamily::Clear);
}
inline const char* supportReason(GradientFieldKey key) {
    if(key.swim>=AIR_MOVEMENT_CLASS)return "air_unvalidated";
    switch(key.family) {
    case GradientFieldFamily::Materials:return "natural_ground";
    case GradientFieldFamily::Clear:return "clear_ground";
    case GradientFieldFamily::Guard:return "guard_weighted_seeds";
    case GradientFieldFamily::Forbidden:return "forbidden_border_seeds";
    case GradientFieldFamily::Markets:return "market_supplier_seeds";
    }
    return "unsupported";
}
struct Patch {std::uint16_t goals=0,open=0;auto operator<=>(const Patch&)const=default;};
struct Difference {
    std::uint16_t goals=0,passability=0,goalAdded=0,goalRemoved=0,blocked=0,opened=0;
};
inline std::uint16_t recoveredSeed(std::uint16_t value) {
    return value==GRADIENT_AT_GOAL?GRADIENT_AT_GOAL:value==GRADIENT_FORBIDDEN?GRADIENT_FORBIDDEN:GRADIENT_UNREACHABLE;
}
inline Difference difference(Patch old,Patch now) {
    const auto nongGoals=std::uint16_t(~(old.goals|now.goals));
    return {std::uint16_t(old.goals^now.goals),std::uint16_t((old.open^now.open)&nongGoals),
        std::uint16_t(now.goals&~old.goals),std::uint16_t(old.goals&~now.goals),
        std::uint16_t(old.open&~now.open&nongGoals),std::uint16_t(now.open&~old.open&nongGoals)};
}
template<class SeedAt> Patch read(int x,int y,int width,int height,SeedAt seedAt) {
    const int x0=(x&(width-1))&~3,y0=(y&(height-1))&~3;
    Patch result;
    for(int dy=0;dy<4 && y0+dy<height;++dy)for(int dx=0;dx<4 && x0+dx<width;++dx) {
        const auto value=seedAt(std::size_t(y0+dy)*width+x0+dx);
        const auto bit=std::uint16_t(1u<<(dy*4+dx));
        if(value==GRADIENT_AT_GOAL)result.goals|=bit;
        else if(value==GRADIENT_UNREACHABLE)result.open|=bit;
    }
    return result;
}
struct History {
    bool known=false;
    std::uint64_t generation=0;
    std::uint32_t publicationTick=0;
    std::uint64_t goalQueries=0,passabilityQueries=0,queries=0,goalAddedQueries=0,goalRemovedQueries=0,blockedQueries=0,openedQueries=0;
    // Borrowed generation identity only: no seed or propagated baseline buffer.
    const std::uint16_t* publishedCells=nullptr;
    void published(std::uint32_t tick,const std::uint16_t* cells) {
        const auto next=generation+1;*this={};known=true;generation=next;publicationTick=tick;publishedCells=cells;
    }
    void record(const Difference& d) {
        ++queries;goalQueries+=d.goals!=0;passabilityQueries+=d.passability!=0;
        goalAddedQueries+=d.goalAdded!=0;goalRemovedQueries+=d.goalRemoved!=0;blockedQueries+=d.blocked!=0;openedQueries+=d.opened!=0;
    }
};
}
