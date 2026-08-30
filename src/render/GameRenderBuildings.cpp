// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>

#include "AICastor.h"

#include <assert.h>

#include <set>
#include <string>
#include <algorithm>
#include <sstream>


#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"


#include "Brush.h"
#include "FertilityCalculatorDialog.h"


// Building rendering. Split from Game_render.cpp.


struct BuildingPosComp
{
	bool operator () (Building * const & a, Building * const & b)
	{
		if (a->posY != b->posY)
			return a->posY < b->posY;
		else
			return a->posX < b->posX;
	}
};


void Game::drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Building *building = teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
	assert(building);
	BuildingType *type=building->type;
	Team *team=building->owner;

	int imgid;
	if (type->crossConnectMultiImage)
	{
		// Cross-connect grid lookup. Only non-virtual buildings have
		// crossConnectMultiImage, and non-virtual buildings can never be
		// moved by the player, so the authoritative posX/posY is correct
		// here — no pending shadow to consult.
		auto sameTypeNeighbour = [&](int nx, int ny)
		{
			Uint16 b = map.getBuilding(nx, ny);
			return (b != NOGBID)
				&& (Building::GIDtoTeam(b) == team->teamNumber)
				&& (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type);
		};
		int add = 0;
		if (sameTypeNeighbour(building->posX, building->posY-1))               // up
			add |= (1<<3);
		if (sameTypeNeighbour(building->posX, building->posY+type->height))    // bottom
			add |= (1<<2);
		if (sameTypeNeighbour(building->posX-1, building->posY))               // left
			add |= (1<<1);
		if (sameTypeNeighbour(building->posX+type->width, building->posY))     // right
			add |= (1<<0);
		imgid = type->gameSpriteImage + add;
	}
	else
	{
		// hpMax+1 (not hpMax) so that at full HP the integer division stays strictly
		// below gameSpriteCount, leaving damageImgShift == 0 (pristine sprite). Using
		// plain hpMax would yield shift == -1 at hp == hpMax and trip the assert below.
		assert(building->hp <= type->hpMax);
		int damageImgShift = type->gameSpriteCount - ((building->hp * type->gameSpriteCount) / (type->hpMax+1)) - 1;
		assert(damageImgShift >= 0);
		imgid = type->gameSpriteImage + damageImgShift;
	}
//	int x, y;
	int dx, dy;


	// select buildings and set the team colors
	Sprite *buildingSprite = type->gameSpritePtr;
	dx = (type->width<<5)-buildingSprite->getW(imgid);
	dy = (type->height<<5)-buildingSprite->getH(imgid);
	buildingSprite->setBaseColor(team->color);

	// draw building
	globalContainer->gfx->drawSprite(x+dx, y+dy, buildingSprite, imgid);
	globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

	if ((drawOptions & DRAW_BUILDING_RECT) != 0)
	{
		int batW=(type->width )<<5;
		int batH=(type->height)<<5;
		globalContainer->gfx->drawRect(x, y, batW, batH, 255, 255, 255, 127);

		BuildingType *upgradedType=globalContainer->buildingsTypes.getLastLevel(building->typeNum);
		int upgradedBatX=x+((upgradedType->decLeft-type->decLeft)<<5);
		int upgradedBatY=y+((upgradedType->decTop-type->decTop)<<5);
		int upgradedBatW=(upgradedType->width)<<5;
		int upgradedBatH=(upgradedType->height)<<5;

		globalContainer->gfx->drawRect(upgradedBatX, upgradedBatY, upgradedBatW, upgradedBatH, 255, 255, 255, 127);
	}

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) && (building->owner->sharedVisionOther & visibleTeams))
	{
		// TODO : find better color for this
		if (type->hpMax)
		{
			int maxWidth, actWidth, addDec;
			float hpRatio=(float)building->hp/(float)type->hpMax;
			if (type->width==1)
			{
				maxWidth=8;
				actWidth=1+(int)(7.0f*hpRatio);
				addDec=2;
			}
			else
			{
				maxWidth=16;
				actWidth=1+(int)(15.0f*hpRatio);
				addDec=7;
			}
			int decy=(type->height*32);
			int healDecx=(type->width-(maxWidth>>3))*16+addDec;

			if (building->hp!=type->hpMax || !building->type->crossConnectMultiImage)
				drawHealthBar(x+healDecx, y+decy-4, maxWidth, actWidth, hpRatio);
		}

		if (building->maxUnitInside>0)
			drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
		if (building->maxUnitWorking>0)
			drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 0, 255, 255, 255, 255, 64, 0);

		if ((type->canFeedUnit) || (type->unitProductionTime))
			drawBuildingResourceBar(x+1, y+1, type, type->maxRessource[CORN], building->ressources[CORN], 255, 255, 120);

		if (type->maxBullets)
			drawBuildingResourceBar(x+1, y+1, type, type->maxBullets, building->bullets, 200, 200, 200);
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << building->owner->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = x+(((type->width<<5)-accessW)>>1);
		int accessY = y+(((type->height<<5)-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}

	if(highlightBuildingType & (1<<building->shortTypeNum))
	{
		globalContainer->gfx->drawSprite(x + buildingSprite->getW(imgid)/2 - 16, y-36, globalContainer->gamegui, 36);
	}
}


void Game::drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	std::set<Building*> drawnBuildings;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getBuilding(x+viewportX, y+viewportY);
			if (gid!=NOGBID) // Then this is a building
			{
				//globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 128, 0);
				//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 128, 0);

				int id = Building::GIDtoID(gid);
				int team = Building::GIDtoTeam(gid);

				Building *building=teams[team]->myBuildings[id];
				if(drawnBuildings.find(building)==drawnBuildings.end())
				{
					assert(building); // if this fails, and unwanted garbage-UID is on the ground.
					if (((drawOptions & DRAW_WHOLE_MAP) != 0)
						|| Building::GIDtoTeam(gid)==localTeam
						|| (building->seenByMask & visibleTeams)
						|| map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))
					{
						int px,py;
						const Sint32 dispX = buildingGuiState ? displayedPosX(*buildingGuiState, *building) : building->posX;
						const Sint32 dispY = buildingGuiState ? displayedPosY(*buildingGuiState, *building) : building->posY;
						map.mapCaseToDisplayable(dispX, dispY, &px, &py, viewportX, viewportY);
					 	drawMapBuilding(px, py, gid, viewportX, viewportY, localTeam, drawOptions);
						drawnBuildings.insert(building);
					}
				}
			}
		}
	if(visibleBuildings)
		*visibleBuildings = drawnBuildings;
}
