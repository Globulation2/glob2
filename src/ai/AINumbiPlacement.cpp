// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using std::shared_ptr;

void AINumbi::nextMainBuilding(const int buildingType)
{
	//printf("AI: nextMainBuilding(%d)\n", buildingType);
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[i])/*&&((myBuildings[i]->type->shortTypeNum==buildingType)||(myBuildings[i]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[i];
				break;
			}
		if (b==NULL)
		{
			mainBuilding[buildingType]=0;
			//printf("AI: no more building !.\n");
		}
		else
			mainBuilding[buildingType]=Building::GIDtoID(b->gid);
	}
	else
	{
		//printf("AI: nextMainBuilding uid=%d\n", b->UID);
		int id=Building::GIDtoID(b->gid);
		// [POSSIBLE BUG H1] The mask AI_NUMBI_BUILDING_INDEX_MASK (=0xFF, i.e. 255)
		// is hardcoded but the loop bound is Building::MAX_COUNT (=1024). When
		// (i+id) exceeds 255 the index wraps within the first 256 building slots,
		// missing buildings 256..1023. The constant intentionally does NOT alias
		// `Building::MAX_COUNT - 1` — renaming would change behavior. Preserved
		// verbatim; flagged for fix-time review (do not "fix" here).
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK])/*&&((myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK]->type->shortTypeNum==buildingType)||(myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[(i+id)&AI_NUMBI_BUILDING_INDEX_MASK];
				break;
			}
		mainBuilding[buildingType]=Building::GIDtoID(b->gid);
		//printf("AI: nextMainBuilding newuid=%d\n", b->UID);
	}
}

int AINumbi::nbFreeAround(const int buildingType, int posX, int posY, int width, int height)
{
	int px=posX+map->getW();
	int py=posY+map->getH();
	int x, y;

	int valid=AI_NUMBI_PLACEMENT_SCORE_INIT;
	int r;
	for (r=AI_NUMBI_OUTER_MARGIN_R_MIN; r<=AI_NUMBI_OUTER_MARGIN_R_MAX; r++)
	{
		y=py-r;
		int ew=1;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_OUTER_EDGE_PENALTY+(r-AI_NUMBI_OUTER_MARGIN_R_MIN)*AI_NUMBI_OUTER_EDGE_PENALTY;
				break;
			}
	}
	for (r=1; r<=1; r++)
	{
		y=py-r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		y=py+height-1+r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}

		x=px-r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=AI_NUMBI_INNER_EDGE_PENALTY;
				break;
			}
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
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
			if (!map->isFreeForBuilding(x, y))
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
			if (!map->isFreeForBuilding(x, y))
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
			if (!map->isFreeForBuilding(x, y))
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
			if (!map->isFreeForBuilding(x, y))
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

bool AINumbi::parseBuildingType(const int buildingType)
{
	return (buildingType==IntBuildingType::DEFENSE_BUILDING);
}

void AINumbi::squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my)
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

bool AINumbi::findNewEmplacement(const int buildingType, int *posX, int *posY)
{
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		nextMainBuilding(buildingType);
		b=myBuildings[mainBuilding[buildingType]];
	}
	if (b==NULL)
	{
		for (int i=0; i<IntBuildingType::NB_BUILDING; i++)
		{
			if (myBuildings[mainBuilding[i]])
			{
				b=myBuildings[mainBuilding[i]];
				break;
			}
		}
	}
	if (b==NULL)
	{
		// TODO : scan the units and find a ressoucefull place.
		return false;
	}
	int typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int width=bt->width;
	int height=bt->height;

	int valid=nbFreeAround(buildingType, b->posX, b->posY, width, height);
	//printf("AI: findNewEmplacement(%d) valid=(%d), uid=(%d), s=(%d, %d).\n", buildingType, valid, b->UID, width, height);
	if (valid>AI_NUMBI_PLACEMENT_SCORE_MIN)
	{
		// [POSSIBLE BUG L9] `maxr` is computed below but never read — the spiral
		// scan further down uses AI_NUMBI_SCAN_ITERATIONS (=4096) directly.
		// Preserved verbatim for replay determinism; do not "fix".
		[[maybe_unused]] int maxr;
		if (b->type->shortTypeNum==0)
			maxr=AI_NUMBI_SWARM_SEARCH_RADIUS;
		else
			maxr=AI_NUMBI_NONSWARM_SEARCH_RADIUS;

		int dx, dy, sx, sy, px, py, mx, my;
		int margin;
		if (b->type->shortTypeNum)
			margin=0;
		else
			margin=AI_NUMBI_SWARM_MARGIN;

		int bposX=b->posX+map->getW();
		int bposY=b->posY+map->getH();

		sx=bposX-width-margin;
		sy=bposY-height-margin;

		px=sx;
		py=sy;

		mx=bposX+b->type->width+margin;
		my=bposY+b->type->height+margin;

		sy--;
		px++;
		dx=1;
		dy=0;

		int bestValid=-1;
		// Note: AI_NUMBI_SCAN_ITERATIONS is intentionally NOT derived from `maxr`
		// above (see L9 comment); it is the original literal preserved as-is.
		for (int i=0; i<AI_NUMBI_SCAN_ITERATIONS; i++)
		{
			squareCircleScann(dx, dy, sx, sy, px, py, mx, my);

			if (map->isFreeForBuilding(px, py, width, height))
			{
				int valid=nbFreeAround(buildingType, px, py, width, height);
				if ((valid>AI_NUMBI_PLACEMENT_SCORE_MIN)&&(game->checkRoomForBuilding(px, py, bt, player->team->teamNumber)))
				{
					int rx, ry, dist;
					bool nr=map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, px, py, &rx, &ry, &dist);
					if (nr)
					{
						if (((dist<=(AI_NUMBI_CORN_DISTANCE_BIAS+width*height))&&(buildingType<=AI_NUMBI_NEAR_CORN_TYPE_CUTOFF))||((dist>=(AI_NUMBI_CORN_DISTANCE_BIAS+width*height))&&(buildingType>AI_NUMBI_NEAR_CORN_TYPE_CUTOFF)))
						{
							//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
							if (valid>bestValid)
							{
								*posX=px;
								*posY=py;
								bestValid=valid;
								if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
									nextMainBuilding(buildingType);
							}
						}
					}
					else if (buildingType!=AI_NUMBI_NEAR_CORN_TYPE_CUTOFF)
					{
						//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
						if (valid>bestValid)
						{
							*posX=px;
							*posY=py;
							bestValid=valid;
							if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
								nextMainBuilding(buildingType);
						}
					}
				}
			}
		}
		if (bestValid>-1)
			return true;
		nextMainBuilding(buildingType);
		return false;
	}
	nextMainBuilding(buildingType);
	return false;
}
