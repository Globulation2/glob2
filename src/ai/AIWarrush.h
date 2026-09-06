// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2005 Eli Dupree

 
#pragma once

#include "AIImplementation.h"
#include <valarray>

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

//ugh. such a large amount of code to work around something simple like "Unit8 gradient[map->w][map->h];"
// Note: nct: I've moved this from AIWarrush.cpp to AIWarrush.h so that AIWarrush.cpp compiles
struct DynamicGradientMapArray
{
public:
	typedef Uint8 element_type;
	
	DynamicGradientMapArray(std::size_t w, std::size_t h) :
		width(w),
		array(w*h)
	{
	}

	//usage: gradient(x, y)
	const element_type &operator()(size_t x, size_t y) const { return array[y * width + x]; }
	element_type &operator()(size_t x, size_t y) { return array[y * width + x]; }
	element_type* c_array() { return &array[0]; }

private:
	// Only width is stored: it's the row stride for the row-major flat
	// buffer below (array[y*width + x]). Height isn't needed for indexing
	// and we don't bounds-check, so storing it would be dead weight. All
	// callers size the array to map->w * map->h and iterate within those
	// dimensions, so the height bound is enforced externally.
	std::size_t width;
	std::valarray<element_type> array;
};

class AIWarrush : public AIImplementation
{
	static const bool verbose = false;
public:
	AIWarrush(Player *player);
	AIWarrush(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AIWarrush();

	Player *player;
	Team *team;
	Game *game;
	Map *map;
	//! The amount of delay left before building a building. This delay is used to prevent
	//overly frequent building-creating requests (and potentially from building them extra times
	//on locations with units and getting extras, but I'm not sure on this one)
	int buildingDelay;
	int areaUpdatingDelay;

	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	std::shared_ptr<Order> getOrder(void);
private:
	void init(Player *player);
	//implementation functions to make the code more like the pseudocode;
	//these should be improved, and some should be moved to Team.h.
	Building *getBuildingWithoutWorkersAssigned(Sint32 shortTypeNum, int num_workers)const;
	bool allOfBuildingTypeAreCompleted(Sint32 shortTypeNum)const;
	bool allOfBuildingTypeAreFull(Sint32 shortTypeNum)const;
	bool allOfBuildingTypeAreFullyWorked(Sint32 shortTypeNum)const;
	int numberOfExtraBuildings()const;
	bool percentageOfBuildingsAreFullyWorked(int percentage)const;
	int numberOfUnitsWithSkillGreaterThanValue(int skill, int value)const;
	int numberOfUnitsWithSkillEqualToValue(int skill, int value)const;
	int numberOfBuildingsOfType(Sint32 shortTypeNum)const;
	bool isAnyUnitWithLessThanOneThirdFood()const;
	Building *getSwarmWithoutSettings(int workerRatio, int explorerRatio, int warriorRatio)const;
	Building *getSwarmAtRandom()const;
	//functions called by getOrder, filled with pseudocode and its product,
	//real code.
	std::shared_ptr<Order> placeGuardAreas(void);
	std::shared_ptr<Order> pruneGuardAreas(void);
	std::shared_ptr<Order> farm(void);
	std::shared_ptr<Order> setupExploreFlagForTeam(Team *enemy_team);
	bool locationIsAvailableForBuilding(int x, int y, int width, int height);
	void initializeGradientWithResource(DynamicGradientMapArray &gradient, Uint8 resource_type);
	std::shared_ptr<Order> buildBuildingOfType(Sint32 shortTypeNum);
};



