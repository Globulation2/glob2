// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The area-grid toolkit the point-dispersion generators (Concrete islands, Isles, Contested
// commons) and the older resource passes are built on: a grid of integer area numbers, points
// as MapGeneratorPoint, and in-out vector arguments. New generators should design on the tile
// masks and Torus of shared/Grid.h instead; nothing here composes with them without conversion.
#include <vector>
class Game;
class Map;
class Building;
struct BuildingType;
struct GenerationContext;
class ListComparator
{
  public:
	ListComparator(std::vector<int> &list) : list(list) {}

	bool operator()(int lhs, int rhs) { return list[lhs] < list[rhs]; }

	std::vector<int> &list;
};

/// This is a single point on the map, used by the map generator
class MapGeneratorPoint
{
  public:
	MapGeneratorPoint(int x, int y) : x(x), y(y) {}
	int x;
	int y;
};

namespace MapGeneration
{
bool divideUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				  std::vector<int> &weights, std::vector<int> &areaNumbers);
void createOval(Map &map, std::vector<int> &grid, int areaN, int x, int y, int width, int height);
enum class PointSearch
{
	Local,
	WholeRegion
};
// Weights travel with points. WholeRegion is bounded best-response, not an optimum guarantee.
int splitUpPoints(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				  std::vector<MapGeneratorPoint> &points, std::vector<int> &weights,
				  PointSearch search = PointSearch::Local, int maxPasses = 200);
void splitUpArea(Map &map, GenerationContext &context, std::vector<int> &grid, int areaN,
				 std::vector<MapGeneratorPoint> &points, std::vector<int> &weights,
				 std::vector<int> &areaNumbers, bool grassOnly = false);
void getAllPoints(Map &map, std::vector<int> &grid, int areaN,
				  std::vector<MapGeneratorPoint> &points);
void getAllOtherPoints(Map &map, std::vector<int> &grid, int areaN,
					   std::vector<MapGeneratorPoint> &points);
void getAllPointsLine(Map &map, int x1, int y1, int x2, int y2,
					  std::vector<MapGeneratorPoint> &points);
void findBorderPoints(Map &map, std::vector<int> &grid, std::vector<MapGeneratorPoint> &points);
void chooseRandomPoints(Map &map, GenerationContext &context,
						std::vector<MapGeneratorPoint> &points, int n);
} // namespace MapGeneration
