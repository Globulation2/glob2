// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGradient.h"
#include "TerrainGradientWorkspace.h"
#include "map/TerrainRegistry.h"

namespace gradient_kernel::runtime_terrain
{
template <unsigned BucketCount, bool DirectProfiles = false, class TerrainAt>
void expandTerrainBucket(std::uint16_t *__restrict gradient, GradientBucket *queue,
						 std::size_t &pending, int cur, int limit, const field::Grid &grid,
						 const TerrainRegistry::Movement &movement,
						 TerrainGradientWorkspace &scratch, TerrainAt terrainAt)
{
	auto &bucket = queue[unsigned(cur) % BucketCount];
	const auto count = bucket.size;
	if (!count)
		return;
	const auto &costs = movement.profiles;
	const auto &steps = movement.steps;
	const unsigned stepCount = steps.size();
	std::array<std::uint16_t, BucketCount> values;
	for (unsigned s = 0; s < stepCount; ++s)
	{
		const auto step = steps[s];
		if (step >= BucketCount)
			continue;
		values[step] = unsigned(cur) + step <= unsigned(limit)
						   ? std::uint16_t(GRADIENT_AT_GOAL - unsigned(cur) - step)
						   : std::uint16_t(1);
	}
#if defined(GLOB2_GRADIENT_SSE2)
	auto &vectors = scratch.vectors;
	auto &limits = scratch.limits;
	const auto one = _mm_set1_epi16(1), bias = _mm_set1_epi16(short(0x8000));
	for (unsigned t = 0; t < costs.size(); ++t)
	{
		const short c = (costs[t].cardinal < BucketCount ? values[costs[t].cardinal] : 1),
					d = (costs[t].diagonal < BucketCount ? values[costs[t].diagonal] : 1);
		const auto v = _mm_setr_epi16(d, c, d, 1, d, c, d, 1);
		_mm_store_si128(reinterpret_cast<__m128i *>(vectors[t].lanes), v);
		_mm_store_si128(reinterpret_cast<__m128i *>(limits[t].lanes),
						_mm_xor_si128(_mm_sub_epi16(v, one), bias));
	}
#elif defined(GLOB2_GRADIENT_NEON)
	auto &vectors = scratch.vectors;
	auto &limits = scratch.limits;
	for (unsigned t = 0; t < costs.size(); ++t)
	{
		const auto c = (costs[t].cardinal < BucketCount ? values[costs[t].cardinal] : 1),
				   d = (costs[t].diagonal < BucketCount ? values[costs[t].diagonal] : 1);
		const auto half = vset_lane_u16(1, vset_lane_u16(c, vdup_n_u16(d), 1), 3);
		const auto v = vcombine_u16(half, half);
		vst1q_u16(vectors[t].lanes, v);
		vst1q_u16(limits[t].lanes, vsubq_u16(v, vdupq_n_u16(1)));
	}
#endif
	const auto *cells = bucket.cells.data();
	const bool masked = grid.powerOfTwo();
	const unsigned width = grid.width(), height = grid.height(),
				   shift = masked ? grid.widthShift() : 0;
	for (std::size_t begin = 0; begin < count; begin += CHUNK)
	{
		const auto end = std::min(count, begin + CHUNK);
		std::array<std::uint32_t *, BucketCount> ends;
		for (unsigned s = 0; s < stepCount; ++s)
		{
			if (steps[s] >= BucketCount)
				continue;
			auto &target = queue[(unsigned(cur) + steps[s]) % BucketCount];
			target.reserveExtra(8 * (end - begin));
			ends[steps[s]] = target.cells.data() + target.size;
		}
		for (auto ci = begin; ci < end; ++ci)
		{
			const auto i = cells[ci];
			if (gradient[i] != GRADIENT_AT_GOAL - cur)
				continue;
			const unsigned x = masked ? i & (width - 1) : i % width;
			const unsigned y = masked ? i >> shift : i / width;
			const unsigned left = x ? x - 1 : width - 1, right = x + 1 == width ? 0 : x + 1;
			const auto above = std::size_t(y ? y - 1 : height - 1) * width;
			const auto below = std::size_t(y + 1 == height ? 0 : y + 1) * width;
			const auto row = std::size_t(y) * width;
			const unsigned t =
				DirectProfiles ? unsigned(terrainAt(i)) : movement.profileIds[terrainAt(i)];
			const auto cost = costs[t];
			auto &ce = ends[cost.cardinal];
			auto &de = ends[cost.diagonal];
			const auto cv = values[cost.cardinal], dv = values[cost.diagonal];
			auto relax = [&](std::size_t n, std::uint16_t value, std::uint32_t *&cursor)
			{
				const bool better = unsigned(gradient[n]) - 1u < unsigned(value) - 1u;
				gradient[n] = better ? value : gradient[n];
				*cursor = static_cast<std::uint32_t>(n);
				cursor += better;
			};
#if defined(GLOB2_GRADIENT_SSE2)
			if (x >= 1 && x + 2 < width)
			{
				auto *a = gradient + above + x - 1, *b = gradient + below + x - 1;
				const auto old = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)a),
													_mm_loadl_epi64((const __m128i *)b));
				const auto better = _mm_cmplt_epi16(
					_mm_xor_si128(_mm_sub_epi16(old, one), bias),
					_mm_load_si128(reinterpret_cast<const __m128i *>(limits[t].lanes)));
				const unsigned mask = _mm_movemask_epi8(better);
				const auto merged = _mm_or_si128(
					_mm_and_si128(better, _mm_load_si128(
											  reinterpret_cast<const __m128i *>(vectors[t].lanes))),
					_mm_andnot_si128(better, old));
				_mm_storel_epi64((__m128i *)a, merged);
				_mm_storel_epi64((__m128i *)b, _mm_unpackhi_epi64(merged, merged));
				*de = above + x - 1;
				de += (mask >> 0) & 1;
				*ce = above + x;
				ce += (mask >> 2) & 1;
				*de = above + x + 1;
				de += (mask >> 4) & 1;
				*de = below + x - 1;
				de += (mask >> 8) & 1;
				*ce = below + x;
				ce += (mask >> 10) & 1;
				*de = below + x + 1;
				de += (mask >> 12) & 1;
			}
			else
#elif defined(GLOB2_GRADIENT_NEON)
			if (x >= 1 && x + 2 < width)
			{
				auto *a = gradient + above + x - 1, *b = gradient + below + x - 1;
				const auto old = vcombine_u16(vld1_u16(a), vld1_u16(b));
				const auto better =
					vcltq_u16(vsubq_u16(old, vdupq_n_u16(1)), vld1q_u16(limits[t].lanes));
				const auto merged = vbslq_u16(better, vld1q_u16(vectors[t].lanes), old);
				vst1_u16(a, vget_low_u16(merged));
				vst1_u16(b, vget_high_u16(merged));
				const auto mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(better, 8)), 0);
				*de = above + x - 1;
				de += (mask >> 0) & 1;
				*ce = above + x;
				ce += (mask >> 8) & 1;
				*de = above + x + 1;
				de += (mask >> 16) & 1;
				*de = below + x - 1;
				de += (mask >> 32) & 1;
				*ce = below + x;
				ce += (mask >> 40) & 1;
				*de = below + x + 1;
				de += (mask >> 48) & 1;
			}
			else
#endif
			{
				relax(above + left, dv, de);
				relax(above + x, cv, ce);
				relax(above + right, dv, de);
				relax(below + left, dv, de);
				relax(below + x, cv, ce);
				relax(below + right, dv, de);
			}
			relax(row + left, cv, ce);
			relax(row + right, cv, ce);
		}
		for (unsigned s = 0; s < stepCount; ++s)
		{
			if (steps[s] >= BucketCount)
				continue;
			auto &target = queue[(unsigned(cur) + steps[s]) % BucketCount];
			const auto newSize = std::size_t(ends[steps[s]] - target.cells.data());
			pending += newSize - target.size;
			target.size = newSize;
		}
	}
	pending -= count;
	bucket.clear();
}

template <unsigned BucketCount, bool DirectProfiles = false, class TerrainAt>
void propagate(std::uint16_t *gradient, int swim, int maxCost, field::Grid grid,
			   GradientWorkspace &workspace, TerrainAt terrainAt, const TerrainRegistry &registry)
{
	if (!workspace.terrain)
		workspace.terrain = std::make_unique<TerrainGradientWorkspace>();
	auto &scratch = *workspace.terrain;
	scratch.prepare(BucketCount);
	for (auto &bucket : scratch.buckets)
		bucket.clear();
	auto *buckets = scratch.buckets.data();
	auto &deferred = workspace.deferredSeeds;
	deferred.clear();
	std::size_t pending = 0;
	for (std::size_t i = 0; i < grid.cells(); ++i)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			const int cost = GRADIENT_AT_GOAL - gradient[i];
			if (cost < int(BucketCount))
			{
				buckets[cost].push(i);
				++pending;
			}
			else
				deferred.push_back({cost, int(i)});
		}
	std::sort(deferred.begin(), deferred.end());
	const int limit = std::min(maxCost, COST_LIMIT);
	std::size_t next = 0;
	for (int cur = 0; (pending || next < deferred.size()) && cur <= limit; ++cur)
	{
		if (!pending)
			cur = deferred[next].first;
		for (; next < deferred.size() && deferred[next].first == cur; ++next)
		{
			buckets[unsigned(cur) % BucketCount].push(deferred[next].second);
			++pending;
		}
		expandTerrainBucket<BucketCount, DirectProfiles>(gradient, buckets, pending, cur, limit,
														 grid, registry.movement(swim), scratch,
														 terrainAt);
	}
}
} // namespace gradient_kernel::runtime_terrain
namespace gradient_kernel
{
inline void propagateTerrainProfiles(std::uint16_t *gradient, int swim, int maxCost,
									 field::Grid grid, GradientWorkspace &workspace,
									 const std::uint8_t *profiles, const TerrainRegistry &registry,
									 unsigned buckets)
{
	const auto profileAt = [profiles](std::size_t i) { return profiles[i]; };
	if (buckets == 64)
		runtime_terrain::propagate<64, true>(gradient, swim, maxCost, grid, workspace, profileAt,
											 registry);
	else if (buckets == 128)
		runtime_terrain::propagate<128, true>(gradient, swim, maxCost, grid, workspace, profileAt,
											  registry);
	else
		runtime_terrain::propagate<256, true>(gradient, swim, maxCost, grid, workspace, profileAt,
											  registry);
}
template <class TerrainAt>
void propagateTerrainField(std::uint16_t *gradient, int swim, int maxCost, field::Grid grid,
						   GradientWorkspace &workspace, TerrainAt terrainAt, bool modifiedCosts,
						   const TerrainRegistry &registry, unsigned buckets)
{
	if (registry.size() == TERRAIN_COUNT)
	{
		propagateTerrainField(gradient, swim, maxCost, grid, workspace, terrainAt, modifiedCosts);
		return;
	}
	if (!modifiedCosts)
	{
		const int sole = registry.soleSwimmingType();
		if (sole >= 0)
			propagateField(gradient, swim, maxCost, grid, workspace,
						   [&](std::size_t i) { return terrainAt(i) == sole; });
		else
			propagateField(gradient, swim, maxCost, grid, workspace,
						   [&](std::size_t i) { return registry.swimming(terrainAt(i)); });
		return;
	}
	if (buckets == 64)
		runtime_terrain::propagate<64>(gradient, swim, maxCost, grid, workspace, terrainAt,
									   registry);
	else if (buckets == 128)
		runtime_terrain::propagate<128>(gradient, swim, maxCost, grid, workspace, terrainAt,
										registry);
	else
		runtime_terrain::propagate<256>(gradient, swim, maxCost, grid, workspace, terrainAt,
										registry);
}
} // namespace gradient_kernel
