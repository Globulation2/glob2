// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "MapEditKeyActions.h"
#include "SDLCompat.h"

void MapEdit::processEvent(SDL_Event& event)
{
	if (event.type==SDL_QUIT)
	{
		doFullQuit=true;
	}
#	ifdef USE_OSX
	else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q && SDL_GetModState() & KMOD_GUI)
	{
		doFullQuit=true;
	}
#	endif
#	ifdef USE_WIN32
	else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F4 && SDL_GetModState() & KMOD_ALT)
	{
		doFullQuit=true;
	}
#	endif
	
	else if(showingMenuScreen || showingLoad || showingSave || showingScriptEditor || showingTeamsEditor || isShowingAreaName)
	{
		delegateMenu(event);
		return;
	}
	else if(event.type==SDL_MOUSEMOTION)
	{
		mouseX=event.motion.x;
		mouseY=event.motion.y;
		relMouseX=event.motion.xrel;
		relMouseY=event.motion.yrel;
		updateCoordinatesLabel();
		if(isDraggingMinimap)
		{
			performAction("minimap drag motion", relMouseX, relMouseY);
			performAction("scroll horizontal stop", relMouseX, relMouseY);
			performAction("scroll vertical stop", relMouseX, relMouseY);
		}
		else if(isDraggingZone)
		{
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				performAction("zone drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingTerrain)
		{
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				performAction("terrain drag motion", relMouseX, relMouseY);
		}
		else if(isScrollDragging)
		{
			performAction("scroll drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingDelete)
		{
			performAction("delete drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingArea)
		{
			performAction("area drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingNoRessourceGrowthArea)
		{
			performAction("no ressource growth area drag motion", relMouseX, relMouseY);
		}
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP)
	{
		// Button events carry their own position; resync the cached motion
		// position to it before dispatching. A warped or synthetic click can
		// arrive without a preceding motion event, and every hit-test and
		// performAction handler below reads mouseX/mouseY — without the
		// resync they would act at the stale motion position instead of
		// where the click landed.
		mouseX=event.button.x;
		mouseY=event.button.y;
		handleMouseButtonEvent(event);
	}
	else if(event.type==SDL_KEYDOWN)
	{
		handleKeyPressed(event.key.keysym, true);
	}
	else if(event.type==SDL_KEYUP)
	{
		handleKeyPressed(event.key.keysym, false);
	}
}



void MapEdit::handleMouseButtonEvent(SDL_Event& event)
{
	if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)
	{
		if(!findAction(event.button.x, event.button.y) && widgetRectangle(0, 16, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH()).is_in(mouseX, mouseY))
		{
			//The button wasn't clicked in any registered area
			if(selectionMode==PlaceBuilding)
				performAction("place building");
			else if(selectionMode==PlaceZone)
				performAction("zone drag start");
			else if(selectionMode==PlaceTerrain)
				performAction("terrain drag start");
			else if(selectionMode==PlaceUnit)
				performAction("place unit");
			else if(selectionMode==RemoveObject)
				performAction("delete drag start");
			else if(selectionMode==ChangeAreas)
				performAction("area drag start");
			else if(selectionMode==ChangeNoRessourceGrowthAreas)
				performAction("no ressource growth area drag start");
			else
			{
				performAction("select map unit");
				performAction("select map building");
			}
		}
		else if(widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET+14, 14, 100, 100).is_in(mouseX, mouseY))
			performAction("minimap drag start");
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_RIGHT)
	{
		if(selectionMode==PlaceNothing || selectionMode==EditingUnit || selectionMode==EditingBuilding)
			performAction("change menu");
		if(selectionMode!=PlaceNothing)
			performAction("unselect");
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_MIDDLE)
	{
		performAction("scroll drag start");
	}
	else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)
	{
		if(isDraggingMinimap)
			performAction("minimap drag stop");
		if(isDraggingZone)
			performAction("zone drag end");
		if(isDraggingTerrain)
			performAction("terrain drag end");
		if(isDraggingDelete)
			performAction("delete drag end");
		if(isDraggingArea)
			performAction("area drag end");
		if(isDraggingNoRessourceGrowthArea)
			performAction("no ressource growth area drag end");
	}
	else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_MIDDLE)
	{
		if(isScrollDragging)
			performAction("scroll drag stop");
	}
}



void MapEdit::handleKeyPressed(SDL_Keysym key, bool pressed)
{
	Uint32 action_t = keyboardManager.getAction(KeyPress(key, pressed));
	switch(action_t)
	{
		case MapEditKeyActions::DoNothing:
		break;
		case MapEditKeyActions::SwitchToBuildingView:
		{
			performAction("switch to building view");
		}
		break;
		case MapEditKeyActions::SwitchToFlagView:
		{
			performAction("switch to flag view");
		}
		break;
		case MapEditKeyActions::SwitchToTerrainView:
		{
			performAction("switch to terrain view");
		}
		break;
		case MapEditKeyActions::SwitchToTeamsView:
		{
			performAction("switch to teams view");
		}
		break;
		case MapEditKeyActions::OpenSaveScreen:
		{
			performAction("open save screen");
		}
		break;
		case MapEditKeyActions::OpenLoadScreen:
		{
			performAction("open load screen");
		}
		break;
		case MapEditKeyActions::SelectSwarm:
		{
			performAction("unselect&switch to building view&set place building selection swarm");
		}
		break;
		case MapEditKeyActions::SelectInn:
		{
			performAction("unselect&switch to building view&set place building selection inn");
		}
		break;
		case MapEditKeyActions::SelectHospital:
		{
			performAction("unselect&switch to building view&set place building selection hospital");
		}
		break;
		case MapEditKeyActions::SelectRacetrack:
		{
			performAction("unselect&switch to building view&set place building selection racetrack");
		}
		break;
		case MapEditKeyActions::SelectSwimmingpool:
		{
			performAction("unselect&switch to building view&set place building selection swimmingpool");
		}
		break;
		case MapEditKeyActions::SelectSchool:
		{
			performAction("unselect&switch to building view&set place building selection school");
		}
		break;
		case MapEditKeyActions::SelectBarracks:
		{
			performAction("unselect&switch to building view&set place building selection barracks");
		}
		break;
		case MapEditKeyActions::SelectTower:
		{
			performAction("unselect&switch to building view&set place building selection defencetower");
		}
		break;
		case MapEditKeyActions::SelectStonewall:
		{
			performAction("unselect&switch to building view&set place building selection stonewall");
		}
		break;
		case MapEditKeyActions::SelectMarket:
		{
			performAction("unselect&switch to building view&set place building selection market");
		}
		break;
		case MapEditKeyActions::SelectExplorationFlag:
		{
			performAction("unselect&switch to flag view&set place building selection explorationflag");
		}
		break;
		case MapEditKeyActions::SelectWarFlag:
		{
			performAction("unselect&switch to flag view&set place building selection warflag");
		}
		break;
		case MapEditKeyActions::SelectClearingFlag:
		{
			performAction("unselect&switch to flag view&set place building selection clearingflag");
		}
		break;
		case MapEditKeyActions::ToggleMenuScreen:
		{
			if (showingMenuScreen==false)
				performAction("open menu screen");
			else if (showingMenuScreen==true)
				performAction("close menu screen");
		}
		break;
		case MapEditKeyActions::SelectDeleteTool:
		{
			performAction("switch to flag view&select delete objects");
		}
		break;
	}
}


