#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "field/Influence.h"
#include "field/TerrainTravel.h"
#include "AITelemetryFields.h"
#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
#include "Game.h"
#include <algorithm>
#include "Order.h"
#include "Player.h"
#include "Unit.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

std::shared_ptr<Order>AICastor::findGoodBuilding(Sint32 typeNum, bool food, bool defense, bool critical)
{
	telemetry.set(AITrace::AI2::AICastor_findGoodBuilding_input_critical, critical);
	telemetry.set(AITrace::AI2::AICastor_findGoodBuilding_input_defense, defense);
	telemetry.set(AITrace::AI2::AICastor_findGoodBuilding_input_food, food);
	telemetry.set(AITrace::AI2::AICastor_findGoodBuilding_input_typeNum, typeNum);
	telemetry.count(AITrace::AI2::AICastor_findGoodBuilding_calls);
	int w=observation->width;
	int h=observation->height;
	const auto* placement = (&queries->kind(typeNum).resolvedType);
 const auto* completed = placement->isBuildingSite ? (&queries->kind(placement->nextLevel).resolvedType) : placement;
 food = completed->semantics.feeding.enabled && completed->semantics.feeding.cost[WHEAT] > 0;
 for (const auto& recipe : completed->semantics.production.recipes)
  food |= recipe.enabled && recipe.cost[WHEAT] > 0;
 defense = queries->matches(placement->isBuildingSite ? placement->nextLevel : typeNum, AIPlanning::BuildingIntent::ProjectileDefense);
 int bw=placement->width;
	int bh=(&queries->kind(typeNum).resolvedType)->height;

	//int hDec=observation->heightDec;
	int wDec=std::countr_zero(unsigned(observation->width));
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	size_t size=w*h;

	Uint32 me=observedTeam->view->mask;

	// minWork computation:
	Sint32 bestWorkScore=AI_CASTOR_BEST_WORK_SCORE_FLOOR;
	for (size_t i=0; i<size; i++)
	{
		if ((observation->visibilityAt(i).discovered&me)==0)
			continue;
		Uint8 work=workAbilityMap[i];
		if (bestWorkScore<work)
			bestWorkScore=work;
	}
	Sint32 minWork=bestWorkScore*AI_CASTOR_MINWORK_MULT;
	if (critical)
	{
		if (minWork>AI_CASTOR_MINWORK_CRITICAL_CAP_PER_CORNER*AI_CASTOR_CORNERS)
			minWork=AI_CASTOR_MINWORK_CRITICAL_CAP_PER_CORNER*AI_CASTOR_CORNERS;
	}
	else
	{
		if (minWork>AI_CASTOR_MINWORK_NORMAL_CAP_PER_CORNER*AI_CASTOR_CORNERS)
			minWork=AI_CASTOR_MINWORK_NORMAL_CAP_PER_CORNER*AI_CASTOR_CORNERS;
	}

	// wheatGradientLimit computation:
	Uint32 wheatGradientLimit;
	if (food)
	{
		if (critical)
			wheatGradientLimit=(AI_CASTOR_WHEAT_GRADIENT_PEAK-AI_CASTOR_WHEAT_GRADIENT_CRITICAL_FOOD_OFFSET)*AI_CASTOR_CORNERS;
		else
			wheatGradientLimit=(AI_CASTOR_WHEAT_GRADIENT_PEAK-AI_CASTOR_WHEAT_GRADIENT_NORMAL_FOOD_OFFSET)*AI_CASTOR_CORNERS;
	}
	else
	{
		if (critical)
			wheatGradientLimit=(AI_CASTOR_WHEAT_GRADIENT_PEAK-AI_CASTOR_WHEAT_GRADIENT_CRITICAL_OTHER_OFFSET)*AI_CASTOR_CORNERS;
		else
			wheatGradientLimit=(AI_CASTOR_WHEAT_GRADIENT_PEAK-AI_CASTOR_WHEAT_GRADIENT_NORMAL_OTHER_OFFSET)*AI_CASTOR_CORNERS;
	}

	// we find the best place possible:
	size_t bestIndex=0;
	Sint32 bestScore=0;
	
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			size_t corner0=(x|(y<<wDec));
			size_t corner1=(((x+bw-1)&wMask)|(y<<wDec));
			size_t corner2=(x|(((y+bh-1)&hMask)<<wDec));
			size_t corner3=(((x+bw-1)&wMask)|(((y+bh-1)&hMask)<<wDec));
			
			if (critical
				&& (observation->visibilityAt(corner0).discovered&me)==0
				&& (observation->visibilityAt(corner1).discovered&me)==0
				&& (observation->visibilityAt(corner2).discovered&me)==0
				&& (observation->visibilityAt(corner3).discovered&me)==0)
				continue;
			
			Uint8 space=spaceForBuildingMap[corner0];
			if (placement->semantics.occupiesGround && space<std::max(bw,bh))
				continue;
			
			Sint32 work=workAbilityMap[corner0]+workAbilityMap[corner1]+workAbilityMap[corner2]+workAbilityMap[corner3];
			if (work<minWork)
				continue;
			
			Uint32 wheatGradient=wheatGradientAt(corner0)+wheatGradientAt(corner1)+wheatGradientAt(corner2)+wheatGradientAt(corner3);
			if (!defense)
			{
				if (food)
				{
					if (wheatGradient<wheatGradientLimit)
						continue;
				}
				else
				{
					if (wheatGradient>wheatGradientLimit)
						continue;
				}
			}
			
			Uint32 enemyRange=enemyRangeMap[corner0]+enemyRangeMap[corner1]+enemyRangeMap[corner2]+enemyRangeMap[corner3];
			if (enemyRange>AI_CASTOR_CORNERS*(AI_CASTOR_WHEAT_GRADIENT_PEAK-AI_CASTOR_ENEMY_RANGE_REJECT_OFFSET))
				continue;

			Sint32 wheatGrowth=wheatGrowthMap[corner0]+wheatGrowthMap[corner1]+wheatGrowthMap[corner2]+wheatGrowthMap[corner3];

			Uint8 neighbour=buildingNeighbourMap[corner0];
			Uint8 directNeighboursCount=(neighbour>>AI_CASTOR_NEIGHBOUR_DIRECT_SHIFT)&AI_CASTOR_NEIGHBOUR_MASK; // [0, 7]
			Uint8 farNeighboursCount=(neighbour>>AI_CASTOR_NEIGHBOUR_FAR_SHIFT)&AI_CASTOR_NEIGHBOUR_MASK; // [0, 7]
			if ((neighbour&AI_CASTOR_NEIGHBOUR_DIRTY_BIT)||(directNeighboursCount>AI_CASTOR_NEIGHBOUR_MAX_DIRECT))
				continue;


			Sint32 score;
			if (defense)
				score=((work<<AI_CASTOR_SCORE_DEFENSE_WORK_SHIFT)+wheatGradient+(enemyRange<<AI_CASTOR_SCORE_DEFENSE_ENEMY_SHIFT))*(AI_CASTOR_SCORE_DEFENSE_NEIGHBOUR_BIAS+(directNeighboursCount<<AI_CASTOR_SCORE_NEIGHBOUR_DIRECT_SHIFT)+farNeighboursCount);
			else if (food)
				score=((wheatGrowth<<AI_CASTOR_SCORE_FOOD_GROWTH_SHIFT)+work+(wheatGradient>>AI_CASTOR_SCORE_FOOD_GRADIENT_SHIFT)-enemyRange)*(AI_CASTOR_SCORE_FOOD_NEIGHBOUR_BIAS+(directNeighboursCount<<AI_CASTOR_SCORE_NEIGHBOUR_DIRECT_SHIFT)+farNeighboursCount);
			else
				score=(AI_CASTOR_SCORE_NORMAL_BIAS+work-(wheatGrowth<<AI_CASTOR_SCORE_NORMAL_GROWTH_SHIFT)-enemyRange)*(AI_CASTOR_SCORE_NORMAL_NEIGHBOUR_BIAS+(directNeighboursCount<<AI_CASTOR_SCORE_NEIGHBOUR_DIRECT_SHIFT)+farNeighboursCount);

			if (bestScore<score && queries->checkRoomForBuilding(x,y,typeNum,teamNumber))
			{
				bestScore=score;
				bestIndex=corner0;
			}
		}

	telemetry.set(AITrace::AI2::placement_bestScore, bestScore);
	telemetry.set(AITrace::AI2::placement_bestIndex, bestIndex);
	telemetry.set(AITrace::AI2::placement_minWork, minWork);
	telemetry.set(AITrace::AI2::placement_bestWorkScore, bestWorkScore);
	if (bestScore>0)
	{
		Sint32 x=(bestIndex&(observation->width-1));
		Sint32 y=((bestIndex>>std::countr_zero(unsigned(observation->width)))&(observation->height-1));
		return telemetry.returnedOrder(
			AITrace::AI2::AICastor_findGoodBuilding_result,
			queries->createOrder(teamNumber, x, y, typeNum, 1, 1));
	}

	return telemetry.returnedOrder(AITrace::AI2::AICastor_findGoodBuilding_result,
								   shared_ptr<Order>());
}

void AICastor::updateGlobalGradientNoObstacle(Uint8 *gradient)
{
	field::directionalInfluence(gradient,{observation->width,observation->height},
		field::ZeroFloorPinned<AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE>{});
}

void AICastor::updateGlobalGradient(Uint8 *gradient)
{
    if(observation->terrainMovementModifiers)
    {
		field::expandTerrainInfluence(
			gradient, observation->width, observation->height, [&](std::size_t i) { return observation->terrainAt(i).type; },
			*observation->terrain);
		return;
    }
	field::directionalInfluence(gradient,{observation->width,observation->height},
		field::BlockedUnitFloor<AI_CASTOR_GRADIENT_WALL>{});
}
