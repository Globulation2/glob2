// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>

#include <algorithm>

#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include "Unit.h"
#include "field/AirPathfind.h"
#include "field/TerrainMovementCosts.h"
#include <climits>

namespace
{
auto airDistancesFor(const Map& map,const Unit& unit)
{
    // Building choice ignores transient flyers; steering handles their occupied
    // cells. Terrain barriers and traversal speeds remain exact.
    return field::AirDistanceField(map.getW(),map.getH(),unit.posX,unit.posY,
        [&map](int x,int y) { return map.terrainPropertiesAt(x,y).flyable; },
        [&map](int x,int y) { return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,map.terrainPropertiesAt(x,y).airSpeedQ8); },
        unit.performance[FLY] && map.hasAirTerrainConstraints());
}
template<class Field>
int airDistanceTiles(Field& field,Building& building)
{
    const unsigned cost=field.costTo(building.getMidX(),building.getMidY());
    return cost==Field::unreachable ? INT_MAX : int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
}
}

Sint32 starvationLimitedTravelDistance(const Unit *unit)
{
	return std::max(0, unit->hungry) / unit->race->hungriness + unit->hp;
}

Building *Team::findNearestHeal(Unit *unit)
{
	if (unit->hungry < 0)
		return NULL;
	if (unit->performance[FLY])
	{
		Sint32 x = unit->posX;
		Sint32 y = unit->posY;
		Sint32 maxDist = starvationLimitedTravelDistance(unit);
		Building *choosen = NULL;
		Sint32 bestDist2 = maxDist * maxDist;
        auto airRoutes=airDistancesFor(*map,*unit);
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			Building *b=(*bi);
			if (!b->canOfferService(unit, HEAL)) continue;
            const int travel=airRoutes.enabled()?airDistanceTiles(airRoutes,*b):0;
            if(airRoutes.enabled() && travel>=maxDist) continue;
            Sint32 dist2 = airRoutes.enabled()?travel*travel:map->warpDistSquare(x,y,b->posX,b->posY);
			if (dist2 < bestDist2)
			{
				choosen = b;
				bestDist2 = dist2;
			}
		}
		return choosen;
	}
	else
	{
		Sint32 x = unit->posX;
		Sint32 y = unit->posY;
		Sint32 maxDist = starvationLimitedTravelDistance(unit);
		Building *choosen=  NULL;
		Sint32 bestDist = maxDist;
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			if (!(*bi)->canOfferService(unit, HEAL)) continue;
			int buildingDist;//initialized in buildingAvailable next line
			if (map->buildingAvailable((*bi), unit->swimClass(), x, y, &buildingDist, BuildingRoute::Footprint) && (buildingDist < bestDist))
			{
				choosen = (*bi);
				bestDist = buildingDist;
			}
		}
		return choosen;
	}
}




Building *Team::findNearestFood(Unit *unit)
{
	MapHeader& header=game->mapHeader;
    auto airRoutes=airDistancesFor(*map,*unit);

	bool concurency = false;//Becomes true if there is a team whose inn-view is on for us but who is not allied to us.
	for (int ti= 0; ti < header.getNumberOfTeams(); ti++)
		if (ti != teamNumber && (game->teams[ti]->sharedVisionFood & me) && !(game->teams[ti]->allies & me))
		{
			concurency = true;
			break;
		}

	// first, we check for the best food an enemy can offer:
	Sint32 bestEnemyHappyness = 0;
	Sint32 maxDist = starvationLimitedTravelDistance(unit);
	Building *bestEnemyFood = NULL;
	if (concurency)
	{
		if (unit->verbose)
			printf("guid=(%d), Team::findNearestFood(), concurency\n", unit->gid);
		if (unit->performance[FLY])
		{
			Sint32 bestDist = maxDist;
			for (int ti = 0; ti < header.getNumberOfTeams(); ti++)
			{
				if (ti == teamNumber)
					continue;
				Team *team = game->teams[ti];
				if (!(team->sharedVisionFood & me) || (team->allies & me))
					continue;
				for (std::list<Building *>::iterator bi = team->canFeedUnit.begin(); bi != team->canFeedUnit.end(); ++bi)
				{
			if (!(*bi)->canOfferService(unit, FEED)) continue;
					Sint32 dist = airRoutes.enabled()?airDistanceTiles(airRoutes,**bi):
                        1 + (Sint32)sqrt(map->warpDistSquare(unit->posX,unit->posY,(*bi)->posX,(*bi)->posY));
					if (dist >= maxDist
						|| !(*bi)->canConvertUnit()
						)
					{
						continue;
					}
					int happyness = (*bi)->availableHappynessLevel();
					if (happyness > bestEnemyHappyness)
					{
						bestEnemyHappyness = happyness;
						bestDist = dist;
						bestEnemyFood = *bi;
					}
					else if (happyness == bestEnemyHappyness && dist < bestDist)
					{
						bestDist = dist;
						bestEnemyFood = *bi;
					}
				}
			}
		}
		else
		{
			Sint32 bestDist = maxDist;
			for (int ti = 0; ti < header.getNumberOfTeams(); ti++)
			{
				if (ti == teamNumber)
					continue;
				Team *team = game->teams[ti];
				if (!(team->sharedVisionFood & me) || (team->allies & me))
					continue;
				for (std::list<Building *>::iterator bi = team->canFeedUnit.begin(); bi != team->canFeedUnit.end(); ++bi)
				{
			if (!(*bi)->canOfferService(unit, FEED)) continue;
					int dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
					if ((dist >= maxDist && !map->hasTerrainMovementModifiers())
						|| !(*bi)->canConvertUnit()
						)
					{
						continue;
					}
					if (!map->buildingAvailable(*bi, unit->swimClass(), unit->posX, unit->posY, &dist, BuildingRoute::Footprint))
						continue;
					if (dist >= maxDist)
						continue;
					int happyness = (*bi)->availableHappynessLevel();
					if (happyness > bestEnemyHappyness)
					{
						bestEnemyHappyness = happyness;
						bestDist = dist;
						bestEnemyFood = *bi;
					}
					else if (happyness == bestEnemyHappyness && dist < bestDist)
					{
						bestDist = dist;
						bestEnemyFood = *bi;
					}
				}
			}
		}
		if (unit->verbose && bestEnemyFood)
			printf("guid=(%d), Team::findNearestFood(), bestEnemyHappyness=%d, bestEnemyFood->gid=%d\n", unit->gid, bestEnemyHappyness, bestEnemyFood->gid);
	}

	//Second, we check if we have any satisfactory inns on our team.
	// That mean it has to be better or equal than the enemy food.
	if (unit->performance[FLY])
	{
		Sint32 bestDist = maxDist;
		Building *choosenFood = NULL;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			if (!(*bi)->canOfferService(unit, FEED)) continue;
			if ((*bi)->availableHappynessLevel() < bestEnemyHappyness)
				continue;
			Sint32 dist = airRoutes.enabled()?airDistanceTiles(airRoutes,**bi):
                        1 + (Sint32)sqrt(map->warpDistSquare(unit->posX,unit->posY,(*bi)->posX,(*bi)->posY));
			if (dist >= bestDist)
				continue;
			bestDist = dist;
			choosenFood = *bi;
		}
		if (choosenFood)
			return choosenFood;
	}
	else
	{
		Sint32 bestDist = maxDist;
		Building *choosenFood = NULL;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			if (!(*bi)->canOfferService(unit, FEED)) continue;
			if ((*bi)->availableHappynessLevel() < bestEnemyHappyness)
				continue;
			int dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
			if (dist >= bestDist && !map->hasTerrainMovementModifiers())
				continue;

			if (!map->buildingAvailable(*bi, unit->swimClass(), unit->posX, unit->posY, &dist, BuildingRoute::Footprint))
				continue;
			if (dist >= bestDist)
				continue;
			bestDist = dist;
			choosenFood = *bi;
		}
		if (choosenFood)
			return choosenFood;
	}

	return bestEnemyFood;
}




Building *Team::findBestUpgrade(Unit *unit)
{
	// No upgrades must not waste idle units on training visits with no benefit.
	if (game->gameHeader.isUnitUpgradesDisabled()) return nullptr;
	Building *choosen=NULL;
	Sint32 score=Team::UPGRADE_SCORE_NONE;
	int x=unit->posX;
	int y=unit->posY;
    auto airRoutes=airDistancesFor(*map,*unit);
	//TODO: This is bad code. If WALK ever ceases to be the first ability or ARMOR ever ceases
	//to be the last, this code will fail.
	for (int ability=(int)WALK; ability<NB_ABILITY; ability++)
	{
		if (!unit->canLearn[ability])
			continue;
		if (unit->verbose)
			printf("guid=(%d) unit->canLearn[ability=%d]\n", unit->gid, ability);
		int actLevel=unit->level[ability];
		for (std::list<Building *>::iterator bi=canUpgrade[ability].begin(); bi!=canUpgrade[ability].end(); ++bi)
		{
			Building *b=(*bi);
			if (unit->verbose)
				printf("guid=(%d)  b->gid=%d, b->type->level=%d, actLevel=%d\n", unit->gid, b->gid, b->type->level, actLevel);
			if (!b->canOfferService(unit, ability))
				continue;
            std::int64_t distanceSquared=map->warpDistSquare(b->posX,b->posY,x,y);
            if(airRoutes.enabled())
            {
                const int distance=airDistanceTiles(airRoutes,*b);
                if(distance==INT_MAX) continue;
                distanceSquared=std::int64_t(distance)*distance;
            }
            else if(!unit->performance[FLY] && map->hasTerrainMovementModifiers())
            {
                int distance=0;
                if(!map->buildingAvailable(b,unit->swimClass(),x,y,&distance, BuildingRoute::Footprint)) continue;
                distanceSquared=std::int64_t(distance)*distance;
            }
            const auto capacity=b->maxUnitInside-b->unitsInside.size();
            if(!capacity) continue;
            const Sint32 newScore=std::min<std::int64_t>(INT_MAX,(distanceSquared<<Q8_FIXED_POINT_SHIFT)/capacity);
			if (newScore<score)
			{
				unit->destinationPurpose=(Sint32)ability;
				choosen=b;
				score=newScore;
			}
		}
	}
	return choosen;
}




int Team::maxBuildLevel(void)
{
	int maxLevel=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->performance[BUILD])
		{
			int unitLevel=u->workerLevel();
			if (unitLevel>maxLevel)
				maxLevel=unitLevel;
		}
	}
	return maxLevel;
}
