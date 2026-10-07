// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include "AINumbi.h"
#include "NumbiQueries.h"
#include "ai/engine/AIDecision.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"
#include <array>

using std::shared_ptr;

void AINumbi::nextMainBuilding(Intent intent)
{
	int& anchor = mainBuilding[static_cast<unsigned>(intent)];
	for (int offset = 1; offset <= Building::MAX_COUNT; ++offset)
	{
		const int id = (anchor + offset) % Building::MAX_COUNT;
		if (observation->buildingSlots(teamNumber)[id]) { anchor = id; return; }
	}
	anchor = 0;
}

int AINumbi::nbFreeAround(int posX, int posY, int width, int height)
{
	int px=posX+observation->width;
	int py=posY+observation->height;
	int x, y;

	int valid=AI_NUMBI_PLACEMENT_SCORE_INIT;
	int r;
	for (r=AI_NUMBI_OUTER_MARGIN_R_MIN; r<=AI_NUMBI_OUTER_MARGIN_R_MAX; r++)
	{
		y=py-r;
		int ew=1;
		for (x=px-ew; x<px+width+ew; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px-ew; x<px+width+ew; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
	}
	for (r=1; r<=1; r++)
	{
		y=py-r;
		for (x=px; x<px+width; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px; x<px+width; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py; y<py+height; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
	}

	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		y=py-r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int wu=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		y=py+height-1+r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!queries->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	wu+=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		bool anyBuild=false;
		x=px-r;
		for (y=py; y<py+height; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int hu=r;
	for (r=1; r<=AI_NUMBI_FREE_REGION_SCAN_RANGE; r++)
	{
		bool anyBuild=false;
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!queries->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	hu+=r;

	valid-=(wu)*(hu);

	return valid;
}

void AINumbi::squareCircleScan(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my)
{
	if (x>=mx)
	{
		dx=0;
		dy=1;
		mx++;
	}
	else if (y>=my)
	{
		dx=-1;
		dy=0;
		my++;
	}
	else if (x<=sx)
	{
		dx=0;
		dy=-1;
		sx--;
	}
	else if (y<=sy)
	{
		dx=1;
		dy=0;
		sy--;
	}
	x+=dx;
	y+=dy;
}

bool AINumbi::findNewEmplacement(Intent intent, int typeNum, int *posX, int *posY)
{
	telemetry.set(AITrace::AI1::AINumbi_findNewEmplacement_input_buildingType, static_cast<int>(intent));
	telemetry.count(AITrace::AI1::AINumbi_findNewEmplacement_calls);
	const auto result = [&](bool found) {
		return telemetry.returnedBool(AITrace::AI1::AINumbi_findNewEmplacement_result,
			AITrace::AI1::AINumbi_findNewEmplacement_true, found);
	};
	int& anchor = mainBuilding[static_cast<unsigned>(intent)];
	const AIEngine::BuildingView* origin = observation->buildingSlots(teamNumber)[anchor];
	if (!origin) { nextMainBuilding(intent); origin = observation->buildingSlots(teamNumber)[anchor]; }
	if (!origin) return result(false);
	const auto* placement = &queries->kind(typeNum);
	const auto* completed = placement->site
		? &queries->kind(placement->next) : placement;
	const int width = placement->width, height = placement->height;
	// Compile the relevant operating inputs once, outside the placement scan.
	std::array<bool, MaterialSlotCount> needs{};
	for (int resource = 0; resource < MaterialSlotCount; ++resource)
	{
		const auto& p = completed->semantics;
		needs[resource] = (p.feeding.enabled && p.feeding.cost[resource] > 0)
			|| (p.healing.enabled && p.healing.cost[resource] > 0);
		for (const auto& recipe : p.production.recipes)
			needs[resource] = needs[resource] || (recipe.enabled && recipe.cost[resource] > 0);
	}
	const int initial = nbFreeAround(origin->posX, origin->posY, width, height);
	if (initial <= AI_NUMBI_PLACEMENT_SCORE_MIN && placement->semantics.occupiesGround)
	{ nextMainBuilding(intent); return result(false); }
	const int margin = provides(*origin, Intent::ProduceWorker) ? AI_NUMBI_SWARM_MARGIN : 0;
	const int bx = origin->posX + observation->width, by = origin->posY + observation->height;
	int sx = bx-width-margin, sy = by-height-margin;
	int px = sx+1, py = sy, mx = bx+queries->kind(*origin).width+margin;
	int my = by+queries->kind(*origin).height+margin, dx = 1, dy = 0;
	--sy;
	int best = -1;
	for (int i = 0; i < AI_NUMBI_SCAN_ITERATIONS; ++i)
	{
		squareCircleScan(dx, dy, sx, sy, px, py, mx, my);
		if (placement->semantics.occupiesGround && !queries->isFreeForBuilding(px, py, width, height)) continue;
		const int score = placement->semantics.occupiesGround
			? nbFreeAround(px, py, width, height) : AI_NUMBI_PLACEMENT_SCORE_INIT;
		if (score <= AI_NUMBI_PLACEMENT_SCORE_MIN || score <= best
			|| !queries->checkRoomForBuilding(px, py, typeNum, teamNumber)) continue;
		bool supplied = true;
		for (int resource = 0; resource < MaterialSlotCount && supplied; ++resource)
			if (needs[resource])
			{
				int rx, ry, distance;
				supplied = queries->resourceAvailableUpdate(teamNumber, resource, 0, px, py, &rx, &ry, &distance)
					&& distance <= AI_NUMBI_WHEAT_DISTANCE_BIAS + width*height;
			}
		if (!supplied) continue;
		*posX = px; *posY = py; best = score;
	}
	nextMainBuilding(intent);
	return result(best >= 0);
}
