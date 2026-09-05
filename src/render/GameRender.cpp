// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <set>


#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "SDLCompat.h"


#include "Brush.h"
#include "DynamicClouds.h"
#include "FertilityCalculatorDialog.h"


#include "GameRenderInternal.h"

// Map rendering orchestrator and shared helpers. Split from Game_render.cpp.


void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth)
{
	assert(maxLength>=0);
	assert(maxLength<65536);
	assert(actLength<=maxLength);

	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
		/*globalContainer->gfx->drawHorzLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawHorzLine(x, y+barWidth+1, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawVertLine(x+i*3, y+1, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, maxLength*3+1, barWidth+2, 0, 0, 0);

		if (orientation==LEFT_TO_RIGHT)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
		}
	}
	else if ((orientation==BOTTOM_TO_TOP) || (orientation==TOP_TO_BOTTOM))
	{
		/*globalContainer->gfx->drawVertLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawVertLine(x+barWidth+1, y, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawHorzLine(x+1, y+i*3, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, barWidth+2, maxLength*3+1, 0, 0, 0);

		if (orientation==TOP_TO_BOTTOM)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
	}
	else
		assert(false);
}


void Game::drawHealthBar(int x, int y, int maxLength, int actLength, float hpRatio)
{
	if (hpRatio > 0.6f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 78, 187, 78);
	else if (hpRatio > 0.3f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 255, 0);
	else
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 0, 0);
}


void Game::drawBuildingResourceBar(int x, int y, BuildingType* type, int maxValue, int currentValue, Uint8 r, Uint8 g, Uint8 b)
{
	// Shrink the bar (3px per unit + 1) until it fits within the building's height minus 10px of padding.
	int bDiv = 1;
	assert(type->height != 0);
	while (((maxValue * 3 + 1) / bDiv) > ((type->height * 32) - 10))
		bDiv++;
	drawPointBar(x, y, BOTTOM_TO_TOP, maxValue / bDiv, currentValue / bDiv, r, g, b, 1 + bDiv);
}



bool Game::isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y)
{

	left += viewportX;
	right += viewportX;
	top += viewportY;
	bot += viewportY;

	if((x >= left-1 && x <= right) || (x+map.getW() >= left-1 && x+map.getW() <= right))
	{
		if((y >= top-1 && y <= bot) || (y+map.getH() >= top-1 && y+map.getH() <= bot))
		{
			return true;
		}
	}
	return false;
}



void Game::drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int localTeam, ViewState& view, Uint32 drawOptions, std::set<Building*> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState)
{
	static int time = 0;
	static DynamicClouds ds(&globalContainer->settings);
	int left=(sx>>5);
	int top=(sy>>5);
	int right=((sx+sw+31)>>5);
	int bot=((sy+sh+31)>>5);

	time++;
	drawMapWater(sw, sh, viewportX, viewportY, time);
	drawMapTerrain(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapRessources(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapGroundUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, view);
	drawMapDebugAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, view);
	drawMapGroundBuildings(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, visibleBuildings, buildingGuiState);
	drawMapAirUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, view);
	if((drawOptions & DRAW_SCRIPT_AREAS) != 0)
		drawMapScriptAreas(left, top, right, bot, viewportX, viewportY);
	drawMapBulletsExplosionsDeathAnimations(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);

	// compute and draw cloud shadow if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		ds.compute(viewportX, viewportY, sw, sh, SDL_GetTicks64()/40, map.getW(), map.getH());
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::SHADOW);
	}

	drawMapFogOfWar(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapOverlayMaps(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawUnitPathLines(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, view);

	// draw cloud overlay if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0 && (drawOptions & DRAW_NO_CLOUD_LAYER) == 0)
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::CLOUD);

	// Draw units that are off the screen for the selected building

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if(view.selectedBuilding != NULL && (view.selectedBuilding->owner->sharedVisionOther & visibleTeams))
	{
		for(std::list<Unit*>::iterator i = view.selectedBuilding->unitsWorking.begin(); i!=view.selectedBuilding->unitsWorking.end(); ++i)
		{
			Unit* unit = *i;
			if(!isOnScreen(left, top, right, bot, viewportX, viewportY, unit->posX, unit->posY))
			{
				drawUnitOffScreen(0, topMargin, sw - rightMargin, sh-topMargin, viewportX, viewportY, unit, drawOptions);
			}
		}
	}

	// we look on the whole map for buildings
	// TODO : increase speed, do not count on graphic clipping
	if (!globalContainer->replaying || globalContainer->replayShowFlags)
	{
		// In replays we want to show the flags of all players, so we build a list of whose buildings to show
		std::list<Team *> teamsToShow;

		if (!globalContainer->replaying)
		{
			// Only add the local team
			teamsToShow.push_back(teams[localTeam]);
		}
		else
		{
			// Add all teams
			for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
			{
				teamsToShow.push_back(teams[i]);
			}
		}

		// now cycle through all added teams
		for (std::list<Team *>::iterator teamsIt=teamsToShow.begin(); teamsIt!=teamsToShow.end(); ++teamsIt)
		{
			for (std::list<Building *>::iterator virtualIt=(*teamsIt)->virtualBuildings.begin();
				virtualIt!=(*teamsIt)->virtualBuildings.end(); ++virtualIt)
			{
				Building *building=*virtualIt;
				BuildingType *type=building->type;

				int team = building->owner->teamNumber;

				int imgid = type->gameSpriteImage;

				int x, y;
				const Sint32 dispX = buildingGuiState ? displayedPosX(*buildingGuiState, *building) : building->posX;
				const Sint32 dispY = buildingGuiState ? displayedPosY(*buildingGuiState, *building) : building->posY;
				map.mapCaseToDisplayable(dispX, dispY, &x, &y, viewportX, viewportY);

				// all flags are hued:
				Sprite *buildingSprite = type->gameSpritePtr;
				buildingSprite->setBaseColor(teams[team]->color);
				globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

				// flag circle:
				if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) || (building==view.selectedBuilding))
					globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

				if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0)
				{
					int decy=(type->height*32);
					int healDecx=(type->width-2)*16+1;

					// TODO : find better color for this
					if (type->hpMax)
					{
						float hpRatio=(float)building->hp/(float)type->hpMax;
						drawHealthBar(x+healDecx+6, y+decy-4, 16, 1+(int)(15.0f*hpRatio), hpRatio);
					}

					if (building->maxUnitInside>0)
						drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
					if (building->maxUnitWorking>0)
						drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 255, 255, 255);

					if ((type->canFeedUnit) || (type->unitProductionTime))
						drawBuildingResourceBar(x+1, y+1, type, type->maxRessource[CORN], building->ressources[CORN], 255, 255, 120);
				}
			}
		}
	}

	if (DEBUG_RENDER_GRADIENTS)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				for (int pi=0; pi<gameHeader.getNumberOfPlayers(); pi++)
					if (players[pi] && players[pi]->ai && players[pi]->ai->implementitionID==AI::CASTOR)
					{
						AICastor *ai=(AICastor *)players[pi]->ai->aiImplementation;
						//Uint8 *gradient=ai->wheatCareMap[1];
						Uint8 *gradient=ai->hydratationMap;
						//Uint8 *gradient=ai->enemyWarriorsMap;
						//Uint8 *gradient=map.forbiddenGradient[1][0];
						//Uint8 *gradient=map.ressourcesGradient[0][CORN][0];

						assert(gradient);
						size_t addr=((x+viewportX)&map.wMask)+map.w*((y+viewportY)&map.hMask);
						Uint8 value=gradient[addr];
						if (value)
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, value);

						/*Uint8 *gradient2=ai->wheatCareMap[1];
						assert(gradient2);
						Uint8 value2=gradient2[addr];
						if (value2)
							globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, value2);*/
						break;
					}
}
