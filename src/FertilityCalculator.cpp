// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "FertilityCalculator.h"

#include "Map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <queue>
#include <utility>
#include <vector>
#include <stdexcept>

namespace
{
	// 31x31 weighting kernel: water tiles within Chebyshev distance kFertilityRadius
	// of a grass tile contribute weight = int(kSqrtScale * sqrt((R-|dx|)*(R-|dy|))).
	constexpr int kFertilityRadius = 15;
	constexpr int kKernelSide = 2 * kFertilityRadius + 1;
	constexpr float kSqrtScale = 4.2f;

	constexpr std::array<std::pair<int, int>, 8> kBfsNeighbors{{
		{-1, -1}, { 0, -1}, { 1, -1},
		{-1,  0},           { 1,  0},
		{-1,  1}, { 0,  1}, { 1,  1},
	}};

	using DistanceMap = std::vector<std::optional<Uint16>>;

	const std::array<Uint16, kKernelSide * kKernelSide>& fertilityKernel()
	{
		static const auto kernel = []() {
			std::array<Uint16, kKernelSide * kKernelSide> k{};
			for (int ny = -kFertilityRadius; ny <= kFertilityRadius; ++ny)
			{
				for (int nx = -kFertilityRadius; nx <= kFertilityRadius; ++nx)
				{
					const int value = (kFertilityRadius - std::abs(nx))
					                  * (kFertilityRadius - std::abs(ny));
					const int idx = (ny + kFertilityRadius) * kKernelSide
					                + (nx + kFertilityRadius);
					k[idx] = static_cast<Uint16>(
						int(kSqrtScale * std::sqrt(static_cast<float>(value))));
				}
			}
			return k;
		}();
		return kernel;
	}

}

namespace FertilityCalculator
{
    struct Job::State
    {
        Map& map;
        const std::size_t size;
        DistanceMap distance;
        std::queue<std::pair<int, int>> frontier;
        std::vector<Uint16> fertility;
        enum Phase { Seed, Reach, Kernel, Ready, Committed } phase = Seed;
        std::size_t cursor = 0, visited = 0;
        int kernelOffset = 0;
        int kernelX = 0, kernelY = 0;
        std::size_t kernelIndex = 0;
        Uint16 total = 0, maximum = 0;
        explicit State(Map& map) : map(map), size(static_cast<std::size_t>(map.getW()) * map.getH()),
            distance(size), fertility(size, 0) {}
        std::pair<int, int> coordinate() const
        { return {static_cast<int>(cursor / map.getH()), static_cast<int>(cursor % map.getH())}; }
    };
    Job::Job(Map& map) : state(std::make_unique<State>(map)) {}
    Job::~Job() = default;
    bool Job::ready() const { return state->phase >= State::Ready; }
    bool Job::advance(std::size_t operations)
    {
        auto& s = *state;
        const auto& kernel = fertilityKernel();
        while (operations-- && !ready()) {
            if (s.phase == State::Seed) {
                if (s.cursor == s.size) { s.cursor = 0; s.phase = State::Reach; continue; }
                const auto [x, y] = s.coordinate();
                if (s.map.isResourceTakeable(x, y, CORN) || s.map.isResourceTakeable(x, y, WOOD)) {
                    s.distance[s.map.coordToIndex(x, y)] = 0;
                    s.frontier.emplace(x, y);
                }
                ++s.cursor;
            } else if (s.phase == State::Reach) {
                if (s.frontier.empty()) { s.phase = State::Kernel; continue; }
                const auto [x, y] = s.frontier.front(); s.frontier.pop(); ++s.visited;
                const Uint16 depth = static_cast<Uint16>(*s.distance[s.map.coordToIndex(x, y)] + 1);
                for (const auto [dx, dy] : kBfsNeighbors) {
                    const int nx = s.map.normalizeX(x + dx), ny = s.map.normalizeY(y + dy);
                    auto& cell = s.distance[s.map.coordToIndex(nx, ny)];
                    if (!cell && s.map.isGrass(nx, ny)) { cell = depth; s.frontier.emplace(nx, ny); }
                }
            } else {
                if (s.cursor == s.size) { s.phase = State::Ready; continue; }
                if (s.kernelOffset == 0) {
                    const auto [x, y] = s.coordinate();
                    const auto index = s.map.coordToIndex(x, y);
                    if (!s.map.isGrass(x, y) || !s.distance[index]) { ++s.cursor; continue; }
                    s.kernelX = x;
                    s.kernelY = y;
                    s.kernelIndex = index;
                }
                const int nx = s.kernelOffset % kKernelSide - kFertilityRadius;
                const int ny = s.kernelOffset / kKernelSide - kFertilityRadius;
                if (s.map.isWater(s.kernelX + nx, s.kernelY + ny)) s.total += kernel[s.kernelOffset];
                if (++s.kernelOffset == kKernelSide * kKernelSide) {
                    s.fertility[s.kernelIndex] = s.total;
                    s.maximum = std::max(s.maximum, s.total);
                    s.total = 0; s.kernelOffset = 0; ++s.cursor;
                }
            }
        }
        return ready();
    }
    float Job::progress() const
    {
        const auto& s = *state;
        if (ready()) return 1.f;
        if (!s.size) return 0.f;
        if (s.phase == State::Seed) return .1f * s.cursor / s.size;
        if (s.phase == State::Reach) return .1f + .1f * s.visited / s.size;
        return .2f + .8f * s.cursor / s.size;
    }
    void Job::commit()
    {
        auto& s = *state;
        if (!ready()) throw std::logic_error("Cannot commit incomplete fertility");
        if (s.phase == State::Committed) return;
        for (int x = 0; x < s.map.getW(); ++x)
            for (int y = 0; y < s.map.getH(); ++y)
                s.map.getTile(x, y).fertility = s.fertility[s.map.coordToIndex(x, y)];
        s.map.fertilityMaximum = s.maximum;
        s.phase = State::Committed;
    }
    void compute(Map& map, const ProgressCallback& progress)
    {
        Job job(map);
        while (!job.advance(16384)) if (progress) progress(job.progress());
        job.commit();
        if (progress) progress(1.f);
    }
}
