// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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

	///This performs the isles generator
	bool computeIsles(Game& game, MapGenerationDescriptor& descriptor);

	///This function devides up the player lands using the standard method
	bool divideUpPlayerLands(Game& game, MapGenerationDescriptor& descriptor, std::vector<int>& grid, std::vector<int>& teamAreaNumbers, int& areaNumber);

private:
	///This is a function that takes an area and devides it up into several smaller areas using the
	///given area numbers and weights. This is bassically a combination of splitUpPoints and splitUpArea
	bool divideUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<int>& weights, std::vector<int>& areaNumbers);
	
	///This creates an area with the shape of an oval with the given width and height
	void createOval(Game& game, std::vector<int>& grid, int areaN, int x, int y, int width, int height);
	
	///This function takes the grid, an area number, and places points in it such that the points are spaced
	///as far from eachother as possible, bearing in mind weights. Returns 0 if failure, otherwise returns 
	///the minimum distance between points that was accomplished
	int splitUpPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights);
	
	///This function takes a grid and an area number, and devides that area into more areas
	void splitUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights, std::vector<int>& areaNumbers, bool grassOnly=false);
	
	///This function fills the vector with all of the points in a specific area
	void getAllPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points);
	
	///This function fills the vector with all of points except those in a specific area
	void getAllOtherPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points);
	
	///This function gets all of the points in a straight line from x1,y1 to x2,y2
	void getAllPointsLine(Game& game, int x1, int y1, int x2, int y2, std::vector<MapGeneratorPoint>& points);

	///This function computes all of points that are borders
	void findBorderPoints(Game& game, std::vector<int>& grid, std::vector<MapGeneratorPoint>& points);

	///This function fills all given points area with a certain ressource. It will fill in a randomly sized
	///square over each grid space no larger than maxFillSize
	void fillInResource(Game& game, std::vector<MapGeneratorPoint>& points, int ressourceType, int maxFillSize);
	
	///This chooses n-random squares from a the points vector, and eliminates the rest
	void chooseRandomPoints(Game& game, std::vector<MapGeneratorPoint>& points, int n);
	
	///This function chooses all points that would be free for the building to build on, eliminates the rest
	void chooseFreeForBuildingSquares(Game& game, std::vector<MapGeneratorPoint>& points, BuildingType* type, int team);
	
	///This function chooses all the points that would be free for ground units
	void chooseFreeForGroundUnits(Game& game, std::vector<MapGeneratorPoint>& points, int team);
	
	///This function chooses all the points that are bordering on the given building
	void chooseTouchingBuilding(Game& game, std::vector<MapGeneratorPoint>& points, Building* building);

	///This function adjusts the heightmap value of point given by the given value
	void adjustHeightmapFromPoints(Game& game, std::vector<MapGeneratorPoint>& points, std::vector<int>& heightmap, int value);
	
	///This function adjusts the heightmap values from a standard perlin noise. Spread is how much the value can go up or down
	void adjustHeightmapFromPerlinNoise(Game& game, std::vector<int>& heights, int spread);
	
	///This function computes the distance of every point from the given points, putting these distances
	///into the given heightmap. The points given as sources are considered to be a distance of 1. The points
	///given as obstacles are considered to be a distance of -1
	void computeDistances(Game& game, std::vector<MapGeneratorPoint>& sources, std::vector<MapGeneratorPoint>& obstacles, std::vector<int>& heightmap);
	
	///Computes the average height/distance of a area on a heightmap
	int computeAverageDistance(Game& game, std::vector<int>& grid, int areaN, const std::vector<int>& heightmap);

	///Adds a building to the map with the given typenum, level, under construction, team and location.
	///Returns the pointer if it could, NULL otherwise
	Building* addBuilding(Game& game, int x, int y, int team, int typenum, int level, bool underConstruction);

	static const bool verbose=false;
};


class ListComparator
{
public:
	ListComparator(std::vector<int>& list) : list(list) {}
	
	bool operator()(int lhs, int rhs)
	{
		return list[lhs] < list[rhs];
	}

	std::vector<int>& list;
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
