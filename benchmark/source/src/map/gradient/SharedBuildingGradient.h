// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientSearch.h"
#include "MapInternal.h"
#include <climits>
#include <memory>
#include <mutex>
#include <functional>

// A lease borrows one live building route generation for hiring workers. The
// owner and workers serialize on its mutex. Retirement transfers ownership of
// the existing buffer/search to the lease; no array or search is copied.
struct SharedBuildingGradient
{
    // Experimental worker-owned cold generations, transferred at hiring publication.
    bool probePrivate=false, probeLocked=false;
    int probeResource=0;
    unsigned probeEpoch=0, probeGeneration=0, probeTick=0;
    std::function<void(SharedBuildingGradient&)> probeInitialize;
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
    // Settles only the queried cells; settled values match a full build.
    int distance(int x, int y)
    {
        std::lock_guard guard(mutex);
        if (probeInitialize) {probeInitialize(*this); probeInitialize={};}
        const auto read=[&](int a,int b){
            const auto i=std::size_t(b&(height-1))*width+(a&(width-1));
            if(search && !search->resolved(i)) search->resolveForProposal(i);
            return values[i];
        };
        auto value=read(x,y);
        constexpr int neighbors[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int d=0;d<8 && value<=GRADIENT_UNREACHABLE;++d)value=read(x+neighbors[d][0],y+neighbors[d][1]);
        return value>GRADIENT_UNREACHABLE?gradientTiles(value):INT_MAX;
    }
};
