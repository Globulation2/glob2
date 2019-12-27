 /*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  Copyright (C) 2005 Eli Dupree
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

#ifndef __AI_WARRUSH_H
#define __AI_WARRUSH_H

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
		height(h),
		array(w*h)
	{
	}

	//usage: gradient(x, y)
	const element_type &operator()(size_t x, size_t y) const { return array[y * width + x]; }
	element_type &operator()(size_t x, size_t y) { return array[y * width + x]; }
	element_type* c_array() { return &array[0]; }

private:
	std::size_t width;
	std::size_t height;
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

	boost::shared_ptr<Order> getOrder(void);
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
	boost::shared_ptr<Order> placeGuardAreas(void);
	boost::shared_ptr<Order> pruneGuardAreas(void);
	boost::shared_ptr<Order> farm(void);
	boost::shared_ptr<Order> setupExploreFlagForTeam(Team *enemy_team);
	bool locationIsAvailableForBuilding(int x, int y, int width, int height);
	void initializeGradientWithResource(DynamicGradientMapArray &gradient, Uint8 resource_type);
	boost::shared_ptr<Order> buildBuildingOfType(Sint32 shortTypeNum);
};

#endif


