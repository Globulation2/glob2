#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
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
#include <memory>
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			if (pred(x, y))
				++countPerColumn.at(x);
		}
	}
	std::vector<int> cursor(w);
	int total = 0;
	for (int x = 0; x < w; ++x)
	{
		::MapGeneration::generationCheckpoint();
		cursor.at(x) = total;
		total += countPerColumn.at(x);
	}
	const size_t base = points.size();
	points.resize(base + total, MapGeneratorPoint(0, 0));
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			if (pred(x, y))
				points.at(base + cursor.at(x)++) = MapGeneratorPoint(x, y);
		}
	}
}
} // namespace

// Cuts area areaN into weights.size() parts: spreads that many seed points evenly over it
// (splitUpPoints, all at equal weight), then grows the parts from them at the given weights
// (splitUpArea), relabelling the area's tiles with areaNumbers. Fails when the area has fewer tiles
// than parts. divideUpPlayerLands uses it to cut a colony's land into twelve equal zones.
bool divideUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				  std::vector<int> &weights, std::vector<int> &areaNumbers)
{
	std::vector<MapGeneratorPoint> points;
	std::vector<int> splitWeights;
	for (unsigned int i = 0; i < weights.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
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

// Labels every tile inside the axis-aligned ellipse of the given width and height centred on
// (x, y) with areaN, through the wrap. The test is x^2 * h^2 + y^2 * w^2 < w^2 * h^2 in integers
// (on half-axes), so it has no rounding drift across platforms.
void createOval(Map &map, std::vector<int> &grid, int areaN, int x, int y, int width, int height)
{
	std::int64_t h2 = std::int64_t(height / 2) * (height / 2);
	std::int64_t w2 = std::int64_t(width / 2) * (width / 2);
	std::int64_t t2 = h2 * w2;
	for (int px = -(width / 2); px < (width / 2); ++px)
	{
		::MapGeneration::generationCheckpoint();
		int nx = map.normalizeX(x + px);
		std::int64_t px2 = std::int64_t(px) * px * h2;
		for (int py = -(height / 2); py < (height / 2); ++py)
		{
			::MapGeneration::generationCheckpoint();
			int ny = map.normalizeY(y + py);
			std::int64_t py2 = std::int64_t(py) * py * w2;
			if (px2 + py2 < t2)
			{
				grid.at(ny * map.getW() + nx) = areaN;
			}
		}
	}
}

// Places one point per entry of `points` inside area areaN, as far apart as it can, and returns the
// least distance between any two (half the smaller map side for a single point, 0 on failure). This
// is how the 2008 toolkit spreads colonies evenly without any geometry: it only needs a grid of
// area labels, so it works on any shape of land, wrapped round the torus.
//
// Stage 1, farthest-point sampling: a random tile of the area seeds a walking-distance flood (other
// areas block it); the first point is a random tile at the flood's greatest distance, and each next
// point a random tile farthest from all points placed so far. This gives a good spread in one pass.
//
// Stage 2, relaxation: repeatedly move each point to the tile that maximises its smallest weighted
// squared straight-line distance to the other points, until no point moves (or maxPasses runs out).
// Local searches only the 7x7 around the point, so points creep; WholeRegion searches every free
// tile of the area, so points jump. A weight scales the distances measured *to* that point, so a
// light point is the one whose distance binds: Contested commons gives its commons seed half a
// colony's weight, and every colony then keeps its distance from the commons first. With equal
// weights, as every other caller passes, this is plain maximin spacing.
//
// Last, the points (and weights with them) are shuffled, so which colony lands on which point is
// random rather than decided by the order the sampling happened to find them in.
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
		collectPointsColumnOrder(
			w, map.getH(), [&](int x, int y) { return grid.at(y * w + x) == areaN; },
			startingPoints);
	}

	if (startingPoints.size() < points.size())
		return 0;

	Uint32 n = context.stream("regions")() % startingPoints.size();

	std::vector<MapGeneratorPoint> obstacles;
	getAllOtherPoints(map, grid, areaN, obstacles);
	std::vector<MapGeneratorPoint> sources;
	sources.push_back(startingPoints.at(n));
	std::vector<int> heights;
	computeDistances(map, sources, obstacles, heights);
	sources.clear();

	for (unsigned int i = 0; i < points.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int w = map.getW(), h2 = map.getH();
		// The original single pass tracked a running max and reset its candidate list on every
		// strict increase; by construction that converges to exactly "every tile at the eventual
		// global max", in scan order, whatever the intermediate history was. Two explicit passes
		// - find the max, then collect every tile equal to it - reach the identical set in the
		// identical order without carrying that reset logic through a stride-w memory access.
		int max = 0;
		for (int y = 0; y < h2; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				max = std::max(max, heights.at(y * w + x));
			}
		}
		std::vector<MapGeneratorPoint> possible;
		collectPointsColumnOrder(
			w, h2, [&](int x, int y) { return heights.at(y * w + x) >= max; }, possible);
		int n = context.stream("regions")() % possible.size();
		points.at(i) = possible.at(n);
		sources.push_back(points.at(i));
		computeDistances(map, sources, obstacles, heights);
	}
	if (points.size() == 1)
		return std::min(map.getW(), map.getH()) / 2;
	startingPoints.clear();
	heights.clear();
	sources.clear();
	obstacles.clear();

	// A point's score at a tile is the smallest weighted squared distance to any other point.
	// The whole-region search reads that off two per-tile fields, the nearest and second-nearest
	// weighted distance over every point (a point's own entry is skipped by taking the second),
	// kept current as points move, instead of looping over every other point at every tile for
	// every point, which cost map area times points squared per pass and, behind a fixed
	// evaluation budget, refused any map past four colonies at 256 or any colony count at 512.
	// The values are the same minimums, so a pass makes the same moves as that loop did.
	struct NearestTwo
	{
		Map &map;
		const std::vector<MapGeneratorPoint> &points;
		const std::vector<int> &weights;
		std::vector<std::int64_t> first, second;
		std::vector<int> firstId, secondId;
		NearestTwo(Map &map, const std::vector<MapGeneratorPoint> &points,
				   const std::vector<int> &weights)
			: map(map), points(points), weights(weights), first(map.getW() * map.getH()),
			  second(first.size()), firstId(first.size()), secondId(first.size())
		{
			for (std::size_t t = 0; t < first.size(); ++t)
			{
				::MapGeneration::generationCheckpoint();
				rebuild(t);
			}
		}
		std::int64_t value(int x, int y, unsigned j) const
		{
			return std::int64_t(map.warpDistSquare(x, y, points.at(j).x, points.at(j).y)) *
				   weights.at(j);
		}
		void rebuild(std::size_t t)
		{
			const int x = int(dimensionRemainder(t, map.getW())), y = int(t / map.getW());
			first.at(t) = second.at(t) = std::numeric_limits<int>::max();
			firstId.at(t) = secondId.at(t) = -1;
			for (unsigned j = 0; j < points.size(); ++j)
			{
				::MapGeneration::generationCheckpoint();
				offer(t, value(x, y, j), int(j));
			}
		}
		void offer(std::size_t t, std::int64_t v, int id)
		{
			if (v < first.at(t))
			{
				second.at(t) = first.at(t);
				secondId.at(t) = firstId.at(t);
				first.at(t) = v;
				firstId.at(t) = id;
			}
			else if (v < second.at(t))
			{
				second.at(t) = v;
				secondId.at(t) = id;
			}
		}
		std::int64_t excluding(std::size_t t, int id) const
		{
			return firstId.at(t) == id ? second.at(t) : first.at(t);
		}
		// Point id now stands at its new position: refresh every tile's pair.
		void moved(int id)
		{
			const int w = map.getW();
			for (std::size_t t = 0; t < first.size(); ++t)
			{
				::MapGeneration::generationCheckpoint();
				const std::int64_t v = value(int(dimensionRemainder(t, w)), int(t / w), id);
				if (firstId.at(t) == id)
				{
					if (v <= second.at(t))
						first.at(t) = v;
					else
						rebuild(t); // its successor is unknown
				}
				else if (secondId.at(t) == id)
				{
					if (v < first.at(t))
					{
						second.at(t) = first.at(t);
						secondId.at(t) = firstId.at(t);
						first.at(t) = v;
						firstId.at(t) = id;
					}
					else if (v <= second.at(t))
						second.at(t) = v;
					else
						rebuild(t);
				}
				else
					offer(t, v, id);
			}
		}
	};
	std::unique_ptr<NearestTwo> nearest;
	std::vector<int> occupants;
	if (search == PointSearch::WholeRegion)
	{
		nearest = std::make_unique<NearestTwo>(map, points, weights);
		occupants.assign(grid.size(), 0);
		for (const auto &p : points)
		{
			::MapGeneration::generationCheckpoint();
			++occupants.at(p.y * map.getW() + p.x);
		}
	}

	bool cont = true;
	std::int64_t minDist = std::numeric_limits<int>::max();
	int passes = 0;
	while (cont)
	{
		::MapGeneration::generationCheckpoint();
		if (++passes > maxPasses)
			throw GenerationFailure("Point dispersion did not converge within its pass budget");
		minDist = std::numeric_limits<int>::max();
		bool changed = false;
		for (unsigned int i = 0; i < points.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (search == PointSearch::WholeRegion)
			{
				const int w = map.getW();
				const std::size_t home = points.at(i).y * w + points.at(i).x;
				std::int64_t best = nearest->excluding(home, int(i));
				minDist = std::min(best, minDist);
				std::size_t bestTile = home;
				// Row-major over the grid, updating on strict improvement only, so the same
				// candidate wins an exact tie as before.
				for (std::size_t t = 0; t < grid.size(); ++t)
				{
					::MapGeneration::generationCheckpoint();
					if (t == home || grid.at(t) != areaN || occupants.at(t) != 0)
						continue;
					const std::int64_t score = nearest->excluding(t, int(i));
					if (score > best)
					{
						best = score;
						bestTile = t;
					}
				}
				if (bestTile != home)
				{
					changed = true;
					--occupants.at(home);
					++occupants.at(bestTile);
					points.at(i).x = int(dimensionRemainder(bestTile, w));
					points.at(i).y = int(bestTile / w);
					nearest->moved(int(i));
				}
				continue;
			}
			std::int64_t best = std::numeric_limits<int>::max();
			for (unsigned int j = 0; j < points.size(); ++j)
			{
				::MapGeneration::generationCheckpoint();
				if (i == j)
					continue;
				std::int64_t dist =
					std::int64_t(map.warpDistSquare(points.at(i).x, points.at(i).y, points.at(j).x,
													points.at(j).y)) *
					weights.at(j);
				best = std::min(dist, best);
			}
			minDist = std::min(best, minDist);
			std::int64_t orig = best;
			int best_x = -1;
			int best_y = -1;
			auto tryCandidate = [&](int nx, int ny)
			{
				if (nx == points.at(i).x && ny == points.at(i).y)
					return;
				if (grid.at(ny * map.getW() + nx) != areaN)
					return;
				std::int64_t score = std::numeric_limits<int>::max();
				bool invalid = false;
				for (unsigned int j = 0; j < points.size(); ++j)
				{
					::MapGeneration::generationCheckpoint();
					if (i == j)
						continue;
					if (nx == points.at(j).x && ny == points.at(j).y)
					{
						invalid = true;
						break;
					}
					std::int64_t dist =
						std::int64_t(map.warpDistSquare(nx, ny, points.at(j).x, points.at(j).y)) *
						weights.at(j);
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
			// PointSearch::Local's window is the 7x7 area right around the point itself.
			for (int dx = -3; dx <= 3; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				for (int dy = -3; dy <= 3; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					tryCandidate(map.normalizeX(points.at(i).x + dx),
								 map.normalizeY(points.at(i).y + dy));
				}
			}
			if (best_x != -1)
			{
				if (best != orig)
					changed = true;
				points.at(i).x = best_x;
				points.at(i).y = best_y;
			}
		}
		if (!changed)
		{
			cont = false;
		}
	}

	for (unsigned int i = 0; i < points.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		for (unsigned int j = 0; j < points.size(); ++j)
		{
			::MapGeneration::generationCheckpoint();
			if (i != j && points.at(i).x == points.at(j).x && points.at(i).y == points.at(j).y)
				return 0;
		}
	}
	// Fisher-Yates with one draw per element, same as the former
	// std::random_shuffle + boost::random_number_generator pairing.
	for (size_t i = 1; i < points.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		size_t j = context.bounded("regions", i + 1);
		if (i != j)
		{
			std::swap(points.at(i), points.at(j));
			std::swap(weights.at(i), weights.at(j));
		}
	}
	return int(::MapGeneration::Numeric::sqrt(double(minDist)));
}

// Grows a region from each point over area areaN until the area is used up, and labels each tile
// with its region's area number. A multi-source flood where every source advances at its own rate:
// each round a region earns `weights[p]` tiles of credit and spends one per tile it claims, so a
// region of weight 10 grows ten times as fast as one of weight 1 and ends up with about ten times
// the ground where they compete. Frontier tiles are kept in a list with each new tile inserted at a
// random position, so growth picks its next tile at random from the frontier: regions come out as
// organic blobs rather than the diamonds a breadth-first flood makes. Claims go 8-way. With
// buildableOnly, regions only claim terrain that permits buildings.
void splitUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				 std::vector<MapGeneratorPoint> &points, std::vector<int> &weights,
				 std::vector<int> &areaNumbers, bool buildableOnly)
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
		::MapGeneration::generationCheckpoint();
		grid.at(points.at(i).y * map.getW() + points.at(i).x) = i;
		gradient.at(points.at(i).y << wDec | points.at(i).x) = 1;
		squares.at(i).push_back(points.at(i).y << wDec | points.at(i).x);

		current.push_back(1);
		count.push_back(1);
	}

	bool cont = true;
	while (cont)
	{
		::MapGeneration::generationCheckpoint();
		bool found = false;
		for (unsigned int p = 0; p < points.size(); ++p)
		{
			::MapGeneration::generationCheckpoint();
			expansion.at(p) += weights.at(p);
			if (!squares.at(p).empty())
				found = true;
			while (expansion.at(p) > 0 && !squares.at(p).empty())
			{
				::MapGeneration::generationCheckpoint();
				Uint32 deltaAddrG = squares.at(p).back();
				squares.at(p).pop_back();

				size_t y = deltaAddrG >> wDec; // Calculate the coordinates of
				size_t x = deltaAddrG & wMask; // the current field and of the

				size_t yu = ((y - 1) & hMask); // fields next to it.
				size_t yd = ((y + 1) & hMask); // We live on a torus! If we are on
				size_t xl = ((x - 1) & wMask); // the "last line" of the map, the
				size_t xr = ((x + 1) & wMask); // next line is the line 0 again.

				int t = grid.at((y << wDec) | x);
				assert(t < (int)points.size());
				int g = gradient.at((y << wDec) | x) + 1;
				grid.at((y << wDec) | x) = areaNumbers.at(t);

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

				if (g != current.at(p))
				{
					current.at(p) = g;
					count.at(p) = 0;
				}

				for (int ci = 0; ci < 8; ci++) // Check for each of this fields if we
				{
					::MapGeneration::generationCheckpoint(); // can improve its gradient value
					addr = &gradient.at(deltaAddrC[ci]);
					side = *addr;
					if (side == 0 && grid.at(deltaAddrC[ci]) == areaN)
					{
						if (buildableOnly && !map.terrainPropertiesAt(deltaAddrC[ci]).buildable)
							continue;
						*addr = g;
						grid.at(deltaAddrC[ci]) = t;
						count.at(p) += 1;
						expansion.at(p) -= 1;

						Uint32 randLocation = context.stream("regions")() % count.at(p);
						std::vector<int>::iterator i = squares.at(p).begin();
						std::advance(i, randLocation);
						squares.at(p).insert(i, deltaAddrC[ci]);
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
	collectPointsColumnOrder(
		w, map.getH(), [&](int x, int y) { return grid.at(y * w + x) == areaN; }, points);
}

void getAllOtherPoints(Map &map, std::vector<int> &grid, int areaN,
					   std::vector<MapGeneratorPoint> &points)
{
	const int w = map.getW();
	collectPointsColumnOrder(
		w, map.getH(), [&](int x, int y) { return grid.at(y * w + x) != areaN; }, points);
}

// The tiles of a straight line from (x1, y1) towards (x2, y2), the short way round the torus, as a
// 4-connected staircase (a diagonal step adds both tiles of the corner), so a stroke along it has
// no diagonal gaps a unit could not walk across. The end tile itself is not included.
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
			::MapGeneration::generationCheckpoint();
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
			::MapGeneration::generationCheckpoint();
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

// Every tile with a differently labelled tile among its eight neighbours: the boundaries between
// areas, which Concrete islands digs its channels along.
void findBorderPoints(Map &map, std::vector<int> &grid, std::vector<MapGeneratorPoint> &points)
{
	const int w = map.getW();
	collectPointsColumnOrder(
		w, map.getH(),
		[&](int x, int y)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				for (int dy = -1; dy <= 1; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					if (grid.at(map.normalizeY(y + dy) * w + map.normalizeX(x + dx)) !=
						grid.at(y * w + x))
						return true;
				}
			}
			return false;
		},
		points);
}

// Keeps a random n of the points (all of them when there are fewer), by a partial Fisher-Yates
// shuffle drawing from the "regions" stream.
void chooseRandomPoints(Map &map, GenerationContext &context,
						std::vector<MapGeneratorPoint> &points, int n)
{
	n = std::min(int(points.size()), n);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		int r = context.stream("regions")() % (points.size() - i);
		std::iter_swap(points.begin() + i, points.begin() + i + r);
	}
	points.erase(points.begin() + n, points.end());
}
} // namespace MapGeneration
