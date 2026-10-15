// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientSearch.h"
#include "MapInternal.h"
#include <climits>
#include <memory>
#include <mutex>

// A lease borrows one live building route generation for hiring workers. The
// published values are complete and immutable. Retirement transfers ownership
// of the existing buffer to the lease; no array is copied.
struct SharedBuildingGradient
{
    std::recursive_mutex mutex;
    std::uint16_t* values = nullptr;
    BuildingGradientSearch* search = nullptr;
    int width = 0, height = 0;
    std::unique_ptr<std::uint16_t[]> retiredValues;
    std::unique_ptr<BuildingGradientSearch> retiredSearch;
    // Holds a reference so retirement cannot destroy a locked mutex.
    struct Guard {
        std::shared_ptr<SharedBuildingGradient> field;
        std::unique_lock<std::recursive_mutex> guard;
        explicit Guard(std::shared_ptr<SharedBuildingGradient> value):field(std::move(value)),
            guard(field ? std::unique_lock(field->mutex) : std::unique_lock<std::recursive_mutex>{}) {}
    };
    static Guard lock(const std::shared_ptr<SharedBuildingGradient>& field) { return Guard(field); }
    // Pure distance lookup: a missing field is represented by an absent lease.
    int distance(int x, int y)
    {
        std::lock_guard guard(mutex);
        const auto read=[&](int a,int b){
            const auto i=std::size_t(b&(height-1))*width+(a&(width-1));
            assert(!search || search->complete());
            return values[i];
        };
        auto value=read(x,y);
        constexpr int neighbors[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int d=0;d<8 && value<=GRADIENT_UNREACHABLE;++d)value=read(x+neighbors[d][0],y+neighbors[d][1]);
        return value>GRADIENT_UNREACHABLE?gradientTiles(value):INT_MAX;
    }
};
