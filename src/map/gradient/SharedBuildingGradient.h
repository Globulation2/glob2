// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientSearch.h"
#include "MapInternal.h"
#include <chrono>
#include <atomic>
#include <climits>
#include <memory>
#include <mutex>

// A lease borrows one live generation. Retirement transfers ownership of its
// existing buffer/search to the lease; no array or search is copied.
struct SharedBuildingGradient
{
    inline static std::atomic<std::uint64_t> ownerWaitNs{0},ownerContentions{0};
    inline static std::atomic<std::uint64_t> retiredBytes{0},peakRetiredBytes{0};
    std::uint64_t accountedRetiredBytes=0;
    std::recursive_mutex mutex;
    std::uint16_t* values = nullptr;
    BuildingGradientSearch* search = nullptr;
    int width = 0, height = 0;
    std::unique_ptr<std::uint16_t[]> retiredValues;
    std::unique_ptr<BuildingGradientSearch> retiredSearch;
    void accountRetired()
    {
        if(!retiredValues)return;
        const auto bytes=sizeof(*this)+std::size_t(width)*height*sizeof(std::uint16_t)+(search?search->retainedBytes():0);
        if(bytes<=accountedRetiredBytes)return;
        const auto total=retiredBytes.fetch_add(bytes-accountedRetiredBytes)+bytes-accountedRetiredBytes;
        accountedRetiredBytes=bytes;
        auto peak=peakRetiredBytes.load();while(peak<total && !peakRetiredBytes.compare_exchange_weak(peak,total)){}
    }
    ~SharedBuildingGradient(){retiredBytes.fetch_sub(accountedRetiredBytes);}
    struct Guard {
        std::shared_ptr<SharedBuildingGradient> field;
        std::unique_lock<std::recursive_mutex> guard;
        explicit Guard(std::shared_ptr<SharedBuildingGradient> value):field(std::move(value)),
            guard(field ? std::unique_lock(field->mutex,std::defer_lock) : std::unique_lock<std::recursive_mutex>{}) {
            if(field && !guard.try_lock()) {
                const auto start=std::chrono::steady_clock::now();guard.lock();
                ownerWaitNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count(),std::memory_order_relaxed);
                ownerContentions.fetch_add(1,std::memory_order_relaxed);
            }
        }
    };
    static Guard lock(const std::shared_ptr<SharedBuildingGradient>& field) { return Guard(field); }
    int distance(int x, int y, std::uint64_t& waitNs, std::uint64_t& expansionNs, std::uint64_t& expansions)
    {
        using Clock=std::chrono::steady_clock;
        std::unique_lock guard(mutex,std::defer_lock);
        if(!guard.try_lock()) {
            const auto start=Clock::now();guard.lock();
            waitNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
        }
        bool expanded=false;
        const auto index=[&](int a,int b){return std::size_t(b&(height-1))*width+(a&(width-1));};
        const auto read=[&](int a,int b){const auto i=index(a,b);if(search && !search->resolved(i)){
            const auto start=Clock::now();++expansions;expanded=true;search->resolveForProposal(i);
            expansionNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
        }return values[i];};
        auto value=read(x,y);
        constexpr int neighbors[8][2]={{0,-1},{1,0},{0,1},{-1,0},{-1,-1},{1,-1},{1,1},{-1,1}};
        for(int d=0;d<8 && value<=GRADIENT_UNREACHABLE;++d)value=read(x+neighbors[d][0],y+neighbors[d][1]);
        if(expanded)accountRetired();
        return value>GRADIENT_UNREACHABLE?gradientTiles(value):INT_MAX;
    }
};
