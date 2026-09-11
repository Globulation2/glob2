// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Regions.h"
#include "Distances.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "HeightMap.h"
#include "Map.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
using namespace MapGeneration;

namespace MapGeneration
{
namespace
{
// Collects every (x, y) with pred(x, y) true, in the same x-major, y-minor order a plain
// `for x { for y { if (pred(x, y)) points.push_back(...) } }` scan produces - several callers
// pick a result by a random index, so the elements must land in that same order, not merely be
// the same set. But such a scan reads a row-major grid against its grain: for fixed x, striding
// by a full row (map.getW() ints) on every y step touches a new cache line almost every time,
// where the same predicate evaluated y-outer, x-inner would stay within one line for many
// consecutive tiles. This gets both: a row-major counting pass sizes each output column, then a
// second row-major pass drops each point straight into its final slot - two sequential-access
// passes over the grid instead of one that strides across it, same result either way.
template <typename Pred>
void collectPointsColumnOrder(int w, int h, Pred pred, std::vector<MapGeneratorPoint> &points)
{
	std::vector<int> countPerColumn(w, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (pred(x, y))
				++countPerColumn[x];
	std::vector<int> cursor(w);
	int total = 0;
	for (int x = 0; x < w; ++x)
	{
		cursor[x] = total;
		total += countPerColumn[x];
	}
	const size_t base = points.size();
	points.resize(base + total, MapGeneratorPoint(0, 0));
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (pred(x, y))
				points[base + cursor[x]++] = MapGeneratorPoint(x, y);
}
} // namespace

bool divideUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				  std::vector<int> &weights, std::vector<int> &areaNumbers)
{
	std::vector<MapGeneratorPoint> points;
	std::vector<int> splitWeights;
	for (unsigned int i = 0; i < weights.size(); ++i)
	{
		points.push_back(MapGeneratorPoint(0, 0));
		splitWeights.push_back(1);
	}
	if (!splitUpPoints(map, context, grid, areaN, points, splitWeights))
	{
		return false;
	}
	splitUpArea(map, context, grid, areaN, points, weights, areaNumbers);
	return true;
}

void createOval(Map &map, std::vector<int> &grid, int areaN, int x, int y, int width, int height)
{
	std::int64_t h2 = std::int64_t(height / 2) * (height / 2);
	std::int64_t w2 = std::int64_t(width / 2) * (width / 2);
	std::int64_t t2 = h2 * w2;
	for (int px = -(width / 2); px < (width / 2); ++px)
	{
		int nx = map.normalizeX(x + px);
		std::int64_t px2 = std::int64_t(px) * px * h2;
		for (int py = -(height / 2); py < (height / 2); ++py)
		{
			int ny = map.normalizeY(y + py);
			std::int64_t py2 = std::int64_t(py) * py * w2;
			if (px2 + py2 < t2)
			{
				grid[ny * map.getW() + nx] = areaN;
			}
		}
	}
}

int splitUpPoints(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				  std::vector<MapGeneratorPoint> &points, std::vector<int> &weights,
				  PointSearch search, int maxPasses)
{
	if (grid.size() != size_t(map.getW()) * map.getH() || points.empty() ||
		weights.size() != points.size() || maxPasses <= 0 ||
		std::any_of(weights.begin(), weights.end(), [](int w) { return w <= 0 || w > 10000; }))
		throw GenerationFailure("Invalid point dispersion inputs");
	std::vector<MapGeneratorPoint> startingPoints;
	{
		const int w = map.getW();
		collectPointsColumnOrder(w, map.getH(), [&](int x, int y) { return grid[y * w + x] == areaN; },
								 startingPoints);
	}

	if (startingPoints.size() < points.size())
		return 0;

	Uint32 n = context.stream("regions")() % startingPoints.size();

	std::vector<MapGeneratorPoint> obstacles;
	getAllOtherPoints(map, grid, areaN, obstacles);
	std::vector<MapGeneratorPoint> sources;
	sources.push_back(startingPoints[n]);
	std::vector<int> heights;
	computeDistances(map, sources, obstacles, heights);
	sources.clear();

	for (unsigned int i = 0; i < points.size(); ++i)
	{
		const int w = map.getW(), h2 = map.getH();
		// The original single pass tracked a running max and reset its candidate list on every
		// strict increase; by construction that converges to exactly "every tile at the eventual
		// global max", in scan order, whatever the intermediate history was. Two explicit passes
		// - find the max, then collect every tile equal to it - reach the identical set in the
		// identical order without carrying that reset logic through a stride-w memory access.
		int max = 0;
		for (int y = 0; y < h2; ++y)
			for (int x = 0; x < w; ++x)
				max = std::max(max, heights[y * w + x]);
		std::vector<MapGeneratorPoint> possible;
		collectPointsColumnOrder(w, h2, [&](int x, int y) { return heights[y * w + x] >= max; },
								 possible);
		int n = context.stream("regions")() % possible.size();
		points[i] = possible[n];
		sources.push_back(points[i]);
		computeDistances(map, sources, obstacles, heights);
	}
	if (points.size() == 1)
		return std::min(map.getW(), map.getH()) / 2;
	startingPoints.clear();
	heights.clear();
	sources.clear();
	obstacles.clear();

	bool cont = true;
	std::int64_t minDist = std::numeric_limits<int>::max();
	int passes = 0;
	std::uint64_t evaluations = 0;
	while (cont)
	{
		if (++passes > maxPasses)
			throw GenerationFailure("Point dispersion did not converge within its pass budget");
		minDist = std::numeric_limits<int>::max();
		bool changed = false;
		for (unsigned int i = 0; i < points.size(); ++i)
		{
			std::int64_t best = std::numeric_limits<int>::max();
			for (unsigned int j = 0; j < points.size(); ++j)
			{
				if (i == j)
					continue;
				std::int64_t dist = std::int64_t(map.warpDistSquare(points[i].x, points[i].y,
																	points[j].x, points[j].y)) *
									weights[j];
				best = std::min(dist, best);
			}
			minDist = std::min(best, minDist);
			std::int64_t orig = best;
			int best_x = -1;
			int best_y = -1;
			auto tryCandidate = [&](int nx, int ny)
			{
				if (nx == points[i].x && ny == points[i].y)
					return;
				if (grid[ny * map.getW() + nx] != areaN)
					return;
				std::int64_t score = std::numeric_limits<int>::max();
				bool invalid = false;
				for (unsigned int j = 0; j < points.size(); ++j)
				{
					if (i == j)
						continue;
					if (nx == points[j].x && ny == points[j].y)
					{
						invalid = true;
						break;
					}
					if (search == PointSearch::WholeRegion && ++evaluations > 20000000)
						throw GenerationFailure(
							"Point dispersion exhausted its distance-evaluation budget");
					std::int64_t dist =
						std::int64_t(map.warpDistSquare(nx, ny, points[j].x, points[j].y)) *
						weights[j];
					score = std::min(dist, score);
				}
				if (invalid)
					return;
				if (score > best)
				{
					best = score;
					best_x = nx;
					best_y = ny;
				}
			};
			if (search == PointSearch::WholeRegion)
			{
				// A full-grid scan: nesting the row coordinate outside the column coordinate
				// keeps it within the grain of grid's row-major layout instead of striding
				// across it a full row at a time on every step. This can change which
				// exactly-tied candidate wins a placement (best only updates on strict
				// improvement), but never which candidates exist or how good the eventual
				// choice is - already documented as bounded best-response, not a single
				// guaranteed optimum, so a different tie winner is not a different guarantee.
				for (int ny = 0; ny < map.getH(); ++ny)
					for (int nx = 0; nx < map.getW(); ++nx)
						tryCandidate(nx, ny);
			}
			else
			{
				// PointSearch::Local's window is the 7x7 area right around the point itself -
				// small enough that traversal order was never going to matter for cache
				// behavior - so its enumeration order is left exactly as it was.
				for (int dx = -3; dx <= 3; ++dx)
					for (int dy = -3; dy <= 3; ++dy)
						tryCandidate(map.normalizeX(points[i].x + dx),
									 map.normalizeY(points[i].y + dy));
			}
			if (best_x != -1)
			{
				if (best != orig)
					changed = true;
				points[i].x = best_x;
				points[i].y = best_y;
			}
		}
		if (!changed)
		{
			cont = false;
		}
	}

	for (unsigned int i = 0; i < points.size(); ++i)
	{
		for (unsigned int j = 0; j < points.size(); ++j)
		{
			if (i != j && points[i].x == points[j].x && points[i].y == points[j].y)
				return 0;
		}
	}
	// Fisher-Yates with one draw per element, same as the former
	// std::random_shuffle + boost::random_number_generator pairing.
	for (size_t i = 1; i < points.size(); ++i)
	{
		size_t j = context.bounded("regions", i + 1);
		if (i != j)
		{
			std::swap(points[i], points[j]);
			std::swap(weights[i], weights[j]);
		}
	}
	return int(std::sqrt(double(minDist)));
}

void splitUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				 std::vector<MapGeneratorPoint> &points, std::vector<int> &weights,
				 std::vector<int> &areaNumbers, bool grassOnly)
{
	std::vector<int> gradient(map.getW() * map.getH(), 0);

	Uint32 wDec = map.wDec;
	Uint32 hMask = map.hMask;
	Uint32 wMask = map.wMask;

	// Frontier cells are inserted at a random position (not just pushed/popped from one end) so
	// the flood grows into an organic, non-directional shape rather than a stack's snake or a
	// queue's rings - that randomness is what's wanted here, not an accident to remove. But nothing
	// about that requires a linked list: std::vector supports the identical operations (random
	// std::advance is O(1) pointer arithmetic on its random-access iterator; insert/erase shift
	// the tail via a contiguous memmove) while never chasing a pointer to a separately allocated
	// node the way every std::list link does. Same elements, same positions, same output.
	std::vector<std::vector<int>> squares(points.size());
	std::vector<int> expansion(points.size(), 0);
	std::vector<int> current;
	std::vector<int> count;

	for (unsigned int i = 0; i < points.size(); ++i)
	{
		grid[points[i].y * map.getW() + points[i].x] = i;
		gradient[points[i].y << wDec | points[i].x] = 1;
		squares[i].push_back(points[i].y << wDec | points[i].x);

		current.push_back(1);
		count.push_back(1);
	}

	bool cont = true;
	while (cont)
	{
		bool found = false;
		for (unsigned int p = 0; p < points.size(); ++p)
		{
			expansion[p] += weights[p];
			if (!squares[p].empty())
				found = true;
			while (expansion[p] > 0 && !squares[p].empty())
			{
				Uint32 deltaAddrG = squares[p].back();
				squares[p].pop_back();

				size_t y = deltaAddrG >> wDec; // Calculate the coordinates of
				size_t x = deltaAddrG & wMask; // the current field and of the

				size_t yu = ((y - 1) & hMask); // fields next to it.
				size_t yd = ((y + 1) & hMask); // We live on a torus! If we are on
				size_t xl = ((x - 1) & wMask); // the "last line" of the map, the
				size_t xr = ((x + 1) & wMask); // next line is the line 0 again.

				int t = grid[(y << wDec) | x];
				assert(t < (int)points.size());
				int g = gradient[(y << wDec) | x] + 1;
				grid[(y << wDec) | x] = areaNumbers[t];

				size_t deltaAddrC[8];
				int *addr;
				int side;

				deltaAddrC[0] = (yu << wDec) | xl; // Calculate the positions of the
				deltaAddrC[1] = (yu << wDec) | x;  // 8 fields next to us from their
				deltaAddrC[2] = (yu << wDec) | xr; // coordinates.
				deltaAddrC[3] = (y << wDec) | xr;
				deltaAddrC[4] = (yd << wDec) | xr;
				deltaAddrC[5] = (yd << wDec) | x;
				deltaAddrC[6] = (yd << wDec) | xl;
				deltaAddrC[7] = (y << wDec) | xl;

				if (g != current[p])
				{
					current[p] = g;
					count[p] = 0;
				}

				for (int ci = 0; ci < 8; ci++) // Check for each of this fields if we
				{							   // can improve its gradient value
					addr = &gradient[deltaAddrC[ci]];
					side = *addr;
					if (side == 0 && grid[deltaAddrC[ci]] == areaN)
					{
						if (grassOnly && !map.isGrass(deltaAddrC[ci]))
							continue;
						*addr = g;
						grid[deltaAddrC[ci]] = t;
						count[p] += 1;
						expansion[p] -= 1;

						Uint32 randLocation = context.stream("regions")() % count[p];
						std::vector<int>::iterator i = squares[p].begin();
						std::advance(i, randLocation);
						squares[p].insert(i, deltaAddrC[ci]);
					}
				}
			}
		}
		if (!found)
			cont = false;
	}
}

void getAllPoints(Map &map, std::vector<int> &grid, int areaN,
				  std::vector<MapGeneratorPoint> &points)
{
	const int w = map.getW();
	collectPointsColumnOrder(w, map.getH(), [&](int x, int y) { return grid[y * w + x] == areaN; },
							 points);
}

void getAllOtherPoints(Map &map, std::vector<int> &grid, int areaN,
					   std::vector<MapGeneratorPoint> &points)
{
	const int w = map.getW();
	collectPointsColumnOrder(w, map.getH(), [&](int x, int y) { return grid[y * w + x] != areaN; },
							 points);
}

void getAllPointsLine(Map &map, int x1, int y1, int x2, int y2,
					  std::vector<MapGeneratorPoint> &points)
{
	int startX = x1;
	int endX = x2;
	int startY = y1;
	int endY = y2;

	int dirX = (endX > startX ? 1 : -1);
	int distX = std::abs(endX - startX);
	if (distX > map.getW() / 2)
	{
		dirX = -dirX;
		distX = map.getW() - distX;
	}

	int dirY = (endY > startY ? 1 : -1);
	int distY = std::abs(endY - startY);
	if (distY > map.getH() / 2)
	{
		dirY = -dirY;
		distY = map.getH() - distY;
	}

	if (distX > distY)
	{
		int px = 0;
		int py = 0;
		int y = startY;
		for (int x = startX; x != endX;)
		{
			px += 1;
			points.push_back(MapGeneratorPoint(x, y));
			if (std::abs(px * distY - py * distX) > std::abs(px * distY - (py + 1) * distX))
			{
				y = map.normalizeY(y + dirY);
				points.push_back(MapGeneratorPoint(x, y));
				py += 1;
			}
			x = map.normalizeX(x + dirX);
		}
	}
	else
	{
		int px = 0;
		int py = 0;
		int x = startX;
		for (int y = startY; y != endY;)
		{
			py += 1;
			points.push_back(MapGeneratorPoint(x, y));
			if (std::abs(py * distX - px * distY) > std::abs(py * distX - (px + 1) * distY))
			{
				x = map.normalizeX(x + dirX);
				points.push_back(MapGeneratorPoint(x, y));
				px += 1;
			}
			y = map.normalizeY(y + dirY);
		}
	}
}

void findBorderPoints(Map &map, std::vector<int> &grid, std::vector<MapGeneratorPoint> &points)
{
	const int w = map.getW();
	collectPointsColumnOrder(
		w, map.getH(),
		[&](int x, int y)
		{
			for (int dx = -1; dx <= 1; ++dx)
				for (int dy = -1; dy <= 1; ++dy)
					if (grid[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)] !=
						grid[y * w + x])
						return true;
			return false;
		},
		points);
}

void chooseRandomPoints(Map &map, GenerationContext &context,
						std::vector<MapGeneratorPoint> &points, int n)
{
	n = std::min(int(points.size()), n);
	for (int i = 0; i < n; ++i)
	{
		int r = context.stream("regions")() % (points.size() - i);
		std::iter_swap(points.begin() + i, points.begin() + i + r);
	}
	points.erase(points.begin() + n, points.end());
}
} // namespace MapGeneration
