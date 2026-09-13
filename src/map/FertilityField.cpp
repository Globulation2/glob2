// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "FertilityField.h"

#include "Map.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <utility>

namespace
{
	const int KERNEL_RADIUS = 15;
	const int KERNEL_WIDTH = 31;
	const int BOX_WIDTH = 16;
	const std::uint32_t OFFSET_WEIGHT[KERNEL_WIDTH] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
		15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
	};
}

namespace Fertility
{

int Field::wx(int x, int offset) const
{
	return wrappedX[(offset + KERNEL_WIDTH) * width + x];
}

int Field::wy(int y, int offset) const
{
	return wrappedY[(offset + KERNEL_WIDTH) * height + y];
}

void Field::buildWrappedIndexes()
{
	// The rolling boxes reach +/-16; the mirrored sand lookups reach +/-30.
	wrappedX.resize((KERNEL_WIDTH * 2 + 1) * width);
	wrappedY.resize((KERNEL_WIDTH * 2 + 1) * height);
	for (int offset = -KERNEL_WIDTH; offset <= KERNEL_WIDTH; ++offset)
	{
		for (int x = 0; x < width; ++x)
			wrappedX[(offset + KERNEL_WIDTH) * width + x] = (x + offset % width + width) % width;
		for (int y = 0; y < height; ++y)
			wrappedY[(offset + KERNEL_WIDTH) * height + y] = (y + offset % height + height) % height;
	}
}

void Field::buildWaterConvolution(const std::vector<std::uint8_t>& water)
{
	const int size = width * height;
	first.assign(size, 0);
	second.assign(size, 0);
	fertility.assign(size, 0);

	// A length-16 forward box followed by a length-16 backward box is exactly the
	// triangular 16-|offset| kernel. Four linear passes instead of 961 taps per tile.
	for (int y = 0; y < height; ++y)
	{
		std::uint32_t sum = 0;
		for (int dx = 0; dx < BOX_WIDTH; ++dx)
			sum += water[y * width + wx(0, dx)];
		for (int x = 0; x < width; ++x)
		{
			first[y * width + x] = sum;
			sum -= water[y * width + x];
			sum += water[y * width + wx(x, BOX_WIDTH)];
		}
	}
	for (int y = 0; y < height; ++y)
	{
		std::uint32_t sum = 0;
		for (int dx = -KERNEL_RADIUS; dx <= 0; ++dx)
			sum += first[y * width + wx(0, dx)];
		for (int x = 0; x < width; ++x)
		{
			second[y * width + x] = sum;
			sum -= first[y * width + wx(x, -KERNEL_RADIUS)];
			sum += first[y * width + wx(x, 1)];
		}
	}
	for (int x = 0; x < width; ++x)
	{
		std::uint32_t sum = 0;
		for (int dy = 0; dy < BOX_WIDTH; ++dy)
			sum += second[wy(0, dy) * width + x];
		for (int y = 0; y < height; ++y)
		{
			first[y * width + x] = sum;
			sum -= second[y * width + x];
			sum += second[wy(y, BOX_WIDTH) * width + x];
		}
	}
	for (int x = 0; x < width; ++x)
	{
		std::uint32_t sum = 0;
		for (int dy = -KERNEL_RADIUS; dy <= 0; ++dy)
			sum += first[wy(0, dy) * width + x];
		for (int y = 0; y < height; ++y)
		{
			fertility[y * width + x] = sum;
			sum -= first[wy(y, -KERNEL_RADIUS) * width + x];
			sum += first[wy(y, 1) * width + x];
		}
	}
}

void Field::rebuild(int newWidth, int newHeight, const std::vector<std::uint8_t>& water,
	const std::vector<std::uint8_t>& sand, Path requestedPath)
{
	assert(newWidth > 0 && newHeight > 0);
	assert(water.size() == size_t(newWidth) * newHeight);
	assert(sand.size() == water.size());
	width = newWidth;
	height = newHeight;
	waterTiles = 0;
	sandTiles = 0;
	for (size_t i = 0; i < water.size(); ++i)
	{
		waterTiles += water[i] != 0;
		sandTiles += sand[i] != 0;
	}
	buildWrappedIndexes();
	usedPath = requestedPath;
	if (usedPath == Path::Adaptive)
	{
		// The convolution is four passes over the map; each splat is a 31x31 stamp.
		const std::uint32_t sandCost = std::uint32_t(4 * water.size()) + 961u * std::uint32_t(sandTiles);
		const std::uint32_t waterCost = 961u * std::uint32_t(waterTiles);
		usedPath = sandCost <= waterCost ? Path::SandCorrection : Path::WaterSplat;
	}

	if (usedPath == Path::SandCorrection)
	{
		buildWaterConvolution(water);
		// Every (water, sand) pair straddling a tile removes the weight the convolution
		// credited it, so each subtraction cancels a contribution that was really added.
		for (int sy = 0; sy < height; ++sy)
		{
			for (int sx = 0; sx < width; ++sx)
			{
				if (!sand[sy * width + sx])
					continue;
				const int* const xWrap = &wrappedX[KERNEL_WIDTH * width + sx];
				const int* const yWrap = &wrappedY[KERNEL_WIDTH * height + sy];
				for (int dy = -KERNEL_RADIUS; dy <= KERNEL_RADIUS; ++dy)
				{
					const std::uint32_t yWeight = OFFSET_WEIGHT[dy + KERNEL_RADIUS];
					std::uint32_t* const targetRow = &fertility[yWrap[dy * height] * width];
					const std::uint8_t* const waterRow = &water[yWrap[2 * dy * height] * width];
					for (int dx = -KERNEL_RADIUS; dx <= KERNEL_RADIUS; ++dx)
						if (waterRow[xWrap[2 * dx * width]])
							targetRow[xWrap[dx * width]] -=
								yWeight * OFFSET_WEIGHT[dx + KERNEL_RADIUS];
				}
			}
		}
	}
	else
	{
		fertility.assign(size_t(width) * height, 0);
		for (int waterY = 0; waterY < height; ++waterY)
		{
			for (int waterX = 0; waterX < width; ++waterX)
			{
				if (!water[waterY * width + waterX])
					continue;
				const int* const xWrap = &wrappedX[KERNEL_WIDTH * width + waterX];
				const int* const yWrap = &wrappedY[KERNEL_WIDTH * height + waterY];
				for (int dy = -KERNEL_RADIUS; dy <= KERNEL_RADIUS; ++dy)
				{
					const std::uint32_t yWeight = OFFSET_WEIGHT[dy + KERNEL_RADIUS];
					std::uint32_t* const targetRow = &fertility[yWrap[-dy * height] * width];
					const std::uint8_t* const sandRow = &sand[yWrap[-2 * dy * height] * width];
					for (int dx = -KERNEL_RADIUS; dx <= KERNEL_RADIUS; ++dx)
						if (!sandRow[xWrap[-2 * dx * width]])
							targetRow[xWrap[-dx * width]] +=
								yWeight * OFFSET_WEIGHT[dx + KERNEL_RADIUS];
				}
			}
		}
	}
}

void Field::gate(const std::vector<std::uint8_t>& keep)
{
	assert(keep.size() == fertility.size());
	for (size_t i = 0; i < fertility.size(); ++i)
		if (!keep[i])
			fertility[i] = 0;
}

std::uint32_t Field::at(int x, int y) const
{
	assert(fertility.size() == size_t(width) * height);
	if (x < 0 || x >= width) x = (x % width + width) % width;
	if (y < 0 || y >= height) y = (y % height + height) % height;
	return fertility[y * width + x];
}

namespace
{
	/// 8-connected over grass from every takeable wheat or wood tile.
	std::vector<std::uint8_t> depositReach(const Map& map)
	{
		const int w = map.getW(), h = map.getH();
		std::vector<std::uint8_t> reached(size_t(w) * h, 0);
		std::queue<std::pair<int, int>> frontier;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (map.isResourceTakeable(x, y, CORN) || map.isResourceTakeable(x, y, WOOD))
				{
					reached[size_t(y) * w + x] = 1;
					frontier.emplace(x, y);
				}
		while (!frontier.empty())
		{
			const auto [px, py] = frontier.front();
			frontier.pop();
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int nx = map.normalizeX(px + dx), ny = map.normalizeY(py + dy);
					std::uint8_t& cell = reached[size_t(ny) * w + nx];
					if (!cell && map.isGrass(nx, ny))
					{
						cell = 1;
						frontier.emplace(nx, ny);
					}
				}
		}
		return reached;
	}
}

Field forMap(const Map& map, bool gateOnReachableDeposits)
{
	const int w = map.getW(), h = map.getH();
	std::vector<std::uint8_t> water(size_t(w) * h, 0), sand(size_t(w) * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			water[size_t(y) * w + x] = map.isWater(x, y);
			sand[size_t(y) * w + x] = map.isSand(x, y);
		}
	Field field;
	field.rebuild(w, h, water, sand);
	if (gateOnReachableDeposits)
	{
		std::vector<std::uint8_t> keep = depositReach(map);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (!map.isGrass(x, y))
					keep[size_t(y) * w + x] = 0;
		field.gate(keep);
	}
	return field;
}

std::uint32_t usefulExpansionCapacity(std::uint32_t fertility, int amount,
	int availableNeighbors, bool wheat)
{
	amount = std::max(0, std::min(8, amount));
	availableNeighbors = std::max(0, std::min(8, availableNeighbors));
	// amount/8 * neighbours/8, and wheat only clears CORN_GROWTH_DIVISOR one time in three.
	const std::uint32_t divisor = wheat ? 192u : 64u;
	return fertility * std::uint32_t(amount) * std::uint32_t(availableNeighbors) / divisor;
}

bool withinPercentBand(std::uint32_t fertility, int minimumPercent, int maximumPercent)
{
	minimumPercent = std::max(0, std::min(100, minimumPercent));
	maximumPercent = std::max(0, std::min(100, maximumPercent));
	if (minimumPercent > maximumPercent)
		std::swap(minimumPercent, maximumPercent);
	const std::uint64_t scaled = std::uint64_t(fertility) * 100u;
	return scaled >= std::uint64_t(minimumPercent) * kScale
		&& scaled <= std::uint64_t(maximumPercent) * kScale;
}

}
