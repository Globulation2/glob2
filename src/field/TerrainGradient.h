// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "GradientPropagation.h"
#include "PreparedTerrainCosts.h"

#ifndef GLOB2_GRADIENT_BENCH_EVENT
#define GLOB2_GRADIENT_BENCH_EVENT(name, count) ((void)0)
#endif

namespace gradient_kernel
{
// General terrain-cost relaxation. Equal edge costs share one queue cursor,
// including cardinal/diagonal aliases, rather than assuming four distinct costs.
// Reserve once per chunk, outside neighbor loops; retain vector relaxation on
// SSE2/NEON and the same scalar wrapped-edge path as the legacy specialization.
template<bool Masked, std::size_t N, class ClassAt>
void expandPreparedTerrainBucketAddressed(std::uint16_t *__restrict gradient, GradientBucket *queue,
    std::size_t &pending, int cur, int limit, const field::Grid &grid,
    const PreparedTerrainCosts<N> &profile, ClassAt classAt)
{
    auto &bucket = queue[unsigned(cur) % BUCKETS];
    const auto count = bucket.size;
    if (!count) return;
    GLOB2_GRADIENT_BENCH_EVENT(occupied, 1);
    GLOB2_GRADIENT_BENCH_EVENT(popped, count);
    const auto &steps = profile.steps;
    const unsigned stepCount = profile.stepCount;
    // Class/slot membership is invariant; only candidate values depend on cur.
    std::array<std::uint16_t, 2 * N> values;
    for (unsigned s = 0; s < stepCount; ++s)
    {
        const auto step = steps[s];
        values[s] = unsigned(cur) + step <= unsigned(limit)
            ? std::uint16_t(GRADIENT_AT_GOAL - unsigned(cur) - step) : std::uint16_t(1);
    }
    struct LayerClass { std::uint16_t cardinalValue, diagonalValue, cardinalSlot, diagonalSlot; };
    std::array<LayerClass, N> layer;
    for (unsigned t = 0; t < profile.classCount; ++t)
    {
        const auto cs = profile.cardinalSlots[t], ds = profile.diagonalSlots[t];
        layer[t] = {values[cs], values[ds], cs, ds};
    }
#if defined(GLOB2_GRADIENT_SSE2)
    __m128i vectors[N], limits[N];
    const auto one = _mm_set1_epi16(1), bias = _mm_set1_epi16(short(0x8000));
    for (unsigned t = 0; t < profile.classCount; ++t)
    {
        const short c = values[profile.cardinalSlots[t]], d = values[profile.diagonalSlots[t]];
        vectors[t] = _mm_setr_epi16(d,c,d,1,d,c,d,1);
        limits[t] = _mm_xor_si128(_mm_sub_epi16(vectors[t], one), bias);
    }
#elif defined(GLOB2_GRADIENT_NEON)
    uint16x8_t vectors[N], limits[N];
    for (unsigned t = 0; t < profile.classCount; ++t)
    {
        const auto c = values[profile.cardinalSlots[t]], d = values[profile.diagonalSlots[t]];
        const auto half = vset_lane_u16(1, vset_lane_u16(c, vdup_n_u16(d), 1), 3);
        vectors[t] = vcombine_u16(half, half);
        limits[t] = vsubq_u16(vectors[t], vdupq_n_u16(1));
    }
#endif
    // Positive edge costs below BUCKETS keep every target distinct from this
    // source bucket, so reserving target storage cannot invalidate cells.
    const auto *cells = bucket.cells.data();
    const unsigned width = grid.width(), height = grid.height(), shift = Masked ? grid.widthShift() : 0;
    for (std::size_t begin = 0; begin < count; begin += CHUNK)
    {
        const auto end = std::min(count, begin + CHUNK);
        std::array<std::uint32_t *, 2 * N> ends;
        for (unsigned s = 0; s < stepCount; ++s)
        {
            auto &target = queue[(unsigned(cur) + steps[s]) % BUCKETS];
            GLOB2_GRADIENT_BENCH_EVENT(chunkReserves, 1);
            target.reserveExtra(profile.maxAppends[s] * (end - begin));
            ends[s] = target.cells.data() + target.size;
        }
        for (auto ci = begin; ci < end; ++ci)
        {
            const auto i = cells[ci];
            if (gradient[i] != GRADIENT_AT_GOAL - cur) { GLOB2_GRADIENT_BENCH_EVENT(stale, 1); continue; }
            const unsigned x = Masked ? i & (width-1) : i % width;
            const unsigned y = Masked ? i >> shift : i / width;
            const unsigned left = x ? x-1 : width-1, right = x+1 == width ? 0 : x+1;
            const auto above = std::size_t(y ? y-1 : height-1) * width;
            const auto below = std::size_t(y+1 == height ? 0 : y+1) * width;
            const auto row = std::size_t(y) * width;
            const unsigned t = classAt(i);
            const auto descriptor = layer[t];
            const auto cs = descriptor.cardinalSlot, ds = descriptor.diagonalSlot;
            auto &ce = ends[cs];
            auto &de = ends[ds];
            const auto cv = descriptor.cardinalValue, dv = descriptor.diagonalValue;
            auto relax = [&](std::size_t n, std::uint16_t value, std::uint32_t *&cursor) {
                const bool better = unsigned(gradient[n])-1u < unsigned(value)-1u;
                gradient[n] = better ? value : gradient[n];
                *cursor = static_cast<std::uint32_t>(n); cursor += better;
            };
#if defined(GLOB2_GRADIENT_SSE2)
            if (x >= 1 && x+2 < width)
            {
                auto *a = gradient + above+x-1, *b = gradient + below+x-1;
                const auto old = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)a), _mm_loadl_epi64((const __m128i *)b));
                const auto better = _mm_cmplt_epi16(_mm_xor_si128(_mm_sub_epi16(old,one),bias),limits[t]);
                const unsigned mask = _mm_movemask_epi8(better);
                const auto merged = _mm_or_si128(_mm_and_si128(better,vectors[t]),_mm_andnot_si128(better,old));
                _mm_storel_epi64((__m128i *)a,merged);
                _mm_storel_epi64((__m128i *)b,_mm_unpackhi_epi64(merged,merged));
                *de=above+x-1; de+=(mask>>0)&1; *ce=above+x; ce+=(mask>>2)&1; *de=above+x+1; de+=(mask>>4)&1;
                *de=below+x-1; de+=(mask>>8)&1; *ce=below+x; ce+=(mask>>10)&1; *de=below+x+1; de+=(mask>>12)&1;
            }
            else
#elif defined(GLOB2_GRADIENT_NEON)
            if (x >= 1 && x+2 < width)
            {
                auto *a = gradient + above+x-1, *b = gradient + below+x-1;
                const auto old = vcombine_u16(vld1_u16(a),vld1_u16(b));
                const auto better = vcltq_u16(vsubq_u16(old,vdupq_n_u16(1)),limits[t]);
                const auto merged = vbslq_u16(better,vectors[t],old);
                vst1_u16(a,vget_low_u16(merged)); vst1_u16(b,vget_high_u16(merged));
                const auto mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(better,8)),0);
                *de=above+x-1; de+=(mask>>0)&1; *ce=above+x; ce+=(mask>>8)&1; *de=above+x+1; de+=(mask>>16)&1;
                *de=below+x-1; de+=(mask>>32)&1; *ce=below+x; ce+=(mask>>40)&1; *de=below+x+1; de+=(mask>>48)&1;
            }
            else
#endif
            {
                relax(above+left,dv,de); relax(above+x,cv,ce); relax(above+right,dv,de);
                relax(below+left,dv,de); relax(below+x,cv,ce); relax(below+right,dv,de);
            }
            relax(row+left,cv,ce); relax(row+right,cv,ce);
        }
        for (unsigned s = 0; s < stepCount; ++s)
        {
            auto &target = queue[(unsigned(cur)+steps[s])%BUCKETS];
            const auto newSize = std::size_t(ends[s]-target.cells.data());
            GLOB2_GRADIENT_BENCH_EVENT(relaxations, newSize-target.size);
            pending += newSize-target.size; target.size=newSize;
        }
    }
    pending -= count; bucket.clear();
}

template<std::size_t N, class ClassAt>
void expandPreparedTerrainBucket(std::uint16_t *gradient, GradientBucket *queue,
    std::size_t &pending, int cur, int limit, const field::Grid &grid,
    const PreparedTerrainCosts<N> &profile, ClassAt classAt)
{
    if (grid.powerOfTwo())
        expandPreparedTerrainBucketAddressed<true>(gradient,queue,pending,cur,limit,grid,profile,classAt);
    else
        expandPreparedTerrainBucketAddressed<false>(gradient,queue,pending,cur,limit,grid,profile,classAt);
}

template<std::size_t N, class TerrainAt>
void expandTerrainBucket(std::uint16_t *gradient, GradientBucket *queue,
    std::size_t &pending, int cur, int limit, const field::Grid &grid,
    const PreparedTerrainCosts<N> &profile, TerrainAt terrainAt)
{
    expandPreparedTerrainBucket(gradient, queue, pending, cur, limit, grid, profile,
        [&](std::size_t i) { return profile.terrainClasses[terrainAt(i)]; });
}

// Compatibility entry point for independently supplied cost tables.
template<std::size_t N, class TerrainAt>
void expandTerrainBucket(std::uint16_t *gradient, GradientBucket *queue,
    std::size_t &pending, int cur, int limit, const field::Grid &grid,
    const std::array<EntrySteps, N> &costs, TerrainAt terrainAt)
{
    const PreparedTerrainCosts<N> profile(costs);
    expandTerrainBucket(gradient, queue, pending, cur, limit, grid, profile, terrainAt);
}

template<std::size_t N, class ClassAt>
void propagatePreparedTerrainField(std::uint16_t *gradient, int maxCost,
    field::Grid grid, GradientWorkspace &workspace, const PreparedTerrainCosts<N> &profile, ClassAt classAt)
{
    auto *buckets=workspace.buckets.data();
    for (auto &bucket:workspace.buckets) bucket.clear();
    auto &deferred=workspace.deferredSeeds; deferred.clear();
    std::size_t pending=0;
    auto enqueueSeed = [&](std::size_t i) {
        if(gradient[i] <= GRADIENT_UNREACHABLE) return;
        const int cost=GRADIENT_AT_GOAL-gradient[i];
        if(cost<int(BUCKETS)) {buckets[cost].push(i);++pending;}
        else deferred.push_back({cost,int(i)});
    };
    // Prove equivalence across every non-forbidden cell, including goals and
    // currently unreachable cells. A distant cheaper tile may still provide
    // the best route, and a goal's entry cost affects its incoming edges.
    // Collect seeds during the same scan; after the first mismatch only seed
    // collection is needed. N is outside the prepared class index range.
    unsigned uniformClass = N;
    std::size_t i = 0;
    for (; i < grid.cells(); ++i)
    {
        if (gradient[i] == GRADIENT_FORBIDDEN) continue;
        const unsigned c = classAt(i);
        if (uniformClass == N) uniformClass = c;
        else if (uniformClass != c) break;
        enqueueSeed(i);
    }
    const bool uniform = i == grid.cells();
    for (; i < grid.cells(); ++i) enqueueSeed(i);
    std::sort(deferred.begin(),deferred.end());
    const int limit=std::min(maxCost,COST_LIMIT);
    auto sweep = [&](const auto &selectedProfile, auto selectedClassAt) {
        std::size_t next=0;
        for(int cur=0;(pending || next<deferred.size()) && cur<=limit;++cur)
        {
            if(!pending) cur=deferred[next].first;
            for(;next<deferred.size()&&deferred[next].first==cur;++next)
            {buckets[unsigned(cur)%BUCKETS].push(deferred[next].second);++pending;}
            expandPreparedTerrainBucket(gradient,buckets,pending,cur,limit,grid,selectedProfile,selectedClassAt);
        }
    };
    if (uniform && uniformClass < N)
    {
        const PreparedTerrainCosts<1> single(std::array<EntrySteps, 1>{profile.classes[uniformClass]});
        sweep(single, [](std::size_t) { return 0; });
    }
    else
        sweep(profile, classAt);
}

template<class TerrainAt>
void propagateTerrainField(std::uint16_t *gradient, int swim, int maxCost,
    field::Grid grid, GradientWorkspace &workspace, TerrainAt terrainAt, bool modifiedCosts)
{
    if (!modifiedCosts)
    {
        propagateField(gradient,swim,maxCost,grid,workspace,[&](std::size_t i) {
            return terrainUsesSwimming(terrainAt(i));
        });
        return;
    }
    const auto &profile = PREPARED_TERRAIN_COSTS[swim];
    propagatePreparedTerrainField(gradient,maxCost,grid,workspace,profile,
        [&](std::size_t i) { return profile.terrainClasses[terrainAt(i)]; });
}
}
