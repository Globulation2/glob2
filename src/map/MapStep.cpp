// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"
#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#include <algorithm>


// growResources, syncStep, fog of war, discovery, explored area

void Map::growResources(void)
{
	int dy=(syncRand()&0x3);
	for (int y=dy; y<h; y+=4)
	{
		for (int x=(syncRand()&0xF); x<w; x+=(syncRand()&0x1F))
		{
			const Resource &r = getResource(x, y);
			if (r.type!=NO_RES_TYPE)
			{
				// we look around to see if there is any water :
				// TODO: uses UnderMap.
				int dwax=(syncRand()&0xF)-(syncRand()&0xF);
				int dway=(syncRand()&0xF)-(syncRand()&0xF);
				int wax1=x+dwax;
				int way1=y+dway;

				int wax2=x+dway*2;
				int way2=y+dwax*2;

				int wax3=x-dwax;
				int way3=y-dway;

				// alga, wood and corn are limited by near underground. Others are not.
				bool expand=true;
				if (r.type == ALGA)
					expand = isWater(wax1, way1) && isSand(wax2, way2);
				else if (r.type == WOOD)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));
				else if (r.type == CORN)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));

				// Growth rate of corn is 1/CORN_GROWTH_DIVISOR
				if(r.type == CORN && expand)
					if(syncRand() % CORN_GROWTH_DIVISOR != 0)
						expand = false;

				if (expand)
				{
					if (r.amount<=(syncRand()&7))
					{
						// we grow resource:
						if(canResourcesGrow(x, y))
							incResource(x, y, r.type, r.variety);
					}
					else if (globalContainer->resourcesTypes.get(r.type)->expendable)
					{
						// we extend resource:
						int dx, dy;
						Unit::dxDyFromDirection((syncRand()&7), &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if(canResourcesGrow(nx, ny))
							incResource(nx, ny, r.type, r.variety);
					}
				}
			}
		}
	}
}


#ifndef YOG_SERVER_ONLY
void Map::syncStep(Uint32 stepCounter)
{
	growResources();
	for (int i=0; i<sizeSector; i++)
		sectors[i].step();
	game->animations->step();

	if (stepCounter & 1)
	{
		int team = (stepCounter >> 1) & 31;
		if (team < game->mapHeader.getNumberOfTeams())
			updateExploredArea(team);
	}
	
	// We only update one gradient per step, round robin over the gradients in use:
	// A freshly loaded map may have no lazily allocated fields yet. Scan once,
	// reset the round-robin flags, then scan once more; an empty set is done.
	for (int pass = 0; pass < 2; ++pass)
	{
		int numberOfTeam=game->mapHeader.getNumberOfTeams();
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESOURCES; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					if (resourcesGradient[t][r][s] && !gradientUpdated[t][r][s])
					{
						updateResourcesGradient(t, r, s);
						gradientUpdated[t][r][s]=true;
						return;
					}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(guardAreasGradient[t][s] && !guardGradientUpdated[t][s])
				{
					updateGuardAreasGradient(t, s);
					guardGradientUpdated[t][s]=true;
					return;
				}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(clearAreasGradient[t][s] && !clearGradientUpdated[t][s])
				{
					updateClearAreasGradient(t, s);
					clearGradientUpdated[t][s]=true;
					return;
				}
				

		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESOURCES; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					gradientUpdated[t][r][s]=false;
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
			{
				guardGradientUpdated[t][s]=false;
				clearGradientUpdated[t][s]=false;
			}
	}
}
#endif  // !YOG_SERVER_ONLY

void Map::switchFogOfWar(void)
{
	memset(fogOfWar, 0, size*sizeof(Uint32));
	if (fogOfWar == &fogOfWarA[0])
		fogOfWar = &fogOfWarB[0];
	else
		fogOfWar = &fogOfWarA[0];
}

void Map::setMapDiscovered(int x, int y, Uint32 sharedVision)
{
	size_t index = coordToIndex(x, y);
	mapDiscovered[index] |= sharedVision;
	fogOfWarA[index] |= sharedVision;
	fogOfWarB[index] |= sharedVision;
}

void Map::setMapDiscovered(int x, int y, int w, int h,  Uint32 sharedVision)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setMapDiscovered(dx, dy, sharedVision);
}

void Map::setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[Team::MAX_COUNT])
{
	Uint16 bgid = cases[coordToIndex(x, y)].building;
	if (bgid != NOGBID)
	{
		int id = Building::GIDtoID(bgid);
		int team = Building::GIDtoTeam(bgid);
		assert(id>=0);
		assert(id<Building::MAX_COUNT);
		assert(team>=0);
		assert(team<Team::MAX_COUNT);
		teams[team]->myBuildings[id]->seenByMask|=sharedVision;
	}
}

void Map::setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[Team::MAX_COUNT])
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setMapBuildingsDiscovered(dx, dy, sharedVision, teams);
}

void Map::setMapExploredByUnit(int x, int y, int w, int h, int team)
{
	for (int dx = x; dx < x + w; dx++)
		for (int dy = y; dy < y + h; dy++)
			exploredArea[team][coordToIndex(dx, dy)] = EXPLORED_FRESH;
}

void Map::setMapExploredByBuilding(int x, int y, int w, int h, int team)
{
	for (int dx = x; dx < x + w; dx++)
		for (int dy = y; dy < y + h; dy++)
			if (exploredArea[team][coordToIndex(dx, dy)] < EXPLORED_BY_BUILDING_MIN)
				exploredArea[team][coordToIndex(dx, dy)] = EXPLORED_BY_BUILDING_MIN;
}

void Map::unsetMapDiscovered(void)
{
	fill(mapDiscovered, 0u);
}

bool Map::isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const
{
	assert((x1<x2) && (y1<y2));
	for(int x=x1;x<=x2;x++)
	{
		for(int y=y1;y<=y2;y++)
		{
			if(isMapDiscovered(x,y,visionMask))
			{
				return true;
			}
		}
	}
	return false;
}

void Map::setMapDiscovered(void)
{
	fill(mapDiscovered, ~0u);
}

void Map::computeDisplayedForbidden(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedForbiddenView.set(i, (cases[i].forbidden & teamMask) != 0);
}

void Map::computeDisplayedGuardArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedGuardAreaView.set(i, (cases[i].guardArea & teamMask) != 0);
}

void Map::computeDisplayedClearArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedClearAreaView.set(i, (cases[i].clearArea & teamMask) != 0);
}


void Map::initExploredArea(int teamNumber)
{
	std::fill(exploredArea[teamNumber], exploredArea[teamNumber] + size, 0);
}

// Seed a team's explored area from its discovery map. Used when the file
// carries no explored area (map files, saves older than
// EXPLORED_AREA_SAVED_VERSION_MINOR): every discovered tile is stamped as
// freshly explored so explorers head outward from the start area instead of
// treating the whole map as unexplored.
void Map::makeDiscoveredAreasExplored(int teamNumber)
{
	assert(game->teams[teamNumber]);
	assert(game->teams[teamNumber]->me);
	assert(exploredArea[teamNumber]);
	for (int x = 0; x < getW(); x++)
		for (int y = 0; y < getH(); y++)
			if (isMapDiscovered(x, y, game->teams[teamNumber]->me))
				setMapExploredByUnit(x, y, 1, 1, teamNumber);
}

void Map::updateExploredArea(int teamNumber)
{
	for (size_t i = 0; i < size; i++)
		if (exploredArea[teamNumber][i] > 0)
			exploredArea[teamNumber][i]--;
}


