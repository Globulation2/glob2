/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef MapGenerator_h
#define MapGenerator_h

#include "MapGenerationDescriptor.h"

class MapGeneratorPoint;
class Building;

///This class handles the new algorithms for map generation
class MapGenerator
{
public:
	///Generates a map from the given generation description
	bool generateMap(Game& game, MapGenerationDescriptor& descriptor);

	///This performs the concrete islands generator
	bool computeConcreteIslands(Game& game, MapGenerationDescriptor& descriptor);
	
	///This function takes the grid, an area number, and places points in it such that the points are spaced
	///as far from eachother as possible, bearing in mind weights
	bool splitUpPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights);
	
	///This function takes a grid and an area number, and devides that area into more areas
	void splitUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights, std::vector<int>& areaNumbers, bool grassOnly=false);
	
	///This function computes all of points that are borders
	void findBorderPoints(Game& game, std::vector<int>& grid, std::vector<MapGeneratorPoint>& points);
	
	//This function computes and prints the percentage of the map allocated to each area
	void computePercentageOfAreas(Game& game, std::vector<int>& grid);
	
	//This function takes a grid, and joins areas that share borders such that you get a smaller number of areas.
	//The target number of areas should be an integer devisor of the areas being joined
	void joinAreas(Game& game, std::vector<int>& grid, std::vector<int> toBeJoined, std::vector<int> target);

	///Adds a building to the map with the given typenum, level, under construction, team and location.
	///Returns the pointer if it could, NULL otherwise
	Building* addBuilding(Game& game, int x, int y, int team, int typenum, int level, bool underConstruction);
private:
	class Node
	{
	public:
		std::vector<int> original;
		std::vector<bool> borders;
	};


	bool joinLoop(Game& game, std::vector<Node> nodes, std::vector<Node>& result, int numberOfJoins);

	static const bool verbose=false;
};


class SquareComparator
{
public:
	SquareComparator(std::vector<int>& grid) : grid(grid) {}
	
	bool operator()(int lhs, int rhs)
	{
		if(grid[lhs] == grid[rhs])
			return lhs > rhs;
		return grid[lhs] > grid[rhs];
	}


	std::vector<int>& grid;
};



///This is a single point on the map, used by the map generator
class MapGeneratorPoint
{
public:
	MapGeneratorPoint(int x, int y) : x(x), y(y) {}
	int x;
	int y;
	bool operator>(const MapGeneratorPoint& rhs)
	{
		if(x == rhs.x)
			return y > rhs.y;
		return x > rhs.x;
	}
	bool operator<(const MapGeneratorPoint& rhs)
	{
		if(x == rhs.x)
			return y < rhs.y;
		return x < rhs.x;
	}
};





#endif
