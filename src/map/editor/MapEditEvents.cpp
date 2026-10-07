#include <GestureScroll.h>
#include "MapZoomControls.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "MapEditKeyActions.h"
#include "EditorDock.h"
#include <SDL3/SDL.h>

void MapEdit::processEvent(SDL_Event& event)
{
	updateCamera();
    if (GAGCore::scrollGesture(event) && !inputState.hasFocus()) return;
    inputState.observe(event);
    if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        suspendInput();
    }
    if (!inputState.hasFocus() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL)) return;

	if (event.type==SDL_EVENT_QUIT)
	{
		requestApplicationQuit();
	}
#	ifdef USE_OSX
	else if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_GUI))
	{
		requestApplicationQuit();
	}
#	endif
#	ifdef USE_WIN32
	else if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F4 && (event.key.mod & SDL_KMOD_ALT))
	{
		requestApplicationQuit();
	}
#	endif

	else if (hasDialog())
	{
		delegateMenu(event);
		return;
	}
	else if (dock && routeToDock(event))
	{
		return;
	}
    else if (auto sample = GAGCore::scrollGesture(event))
    {
        auto wheel = GAGCore::gestureWheelFallback(*sample);
        const double delta=wheel.wheel.y * (wheel.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        if (!scrollBuildingSelectors(delta)) zoomMap(delta, mouseX, mouseY);
    }
    else if(event.type==SDL_EVENT_MOUSE_WHEEL)
    {
        double delta=event.wheel.y;
#if SDL_VERSION_ATLEAST(2,0,18)
        delta=event.wheel.y;
#endif
        delta*=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;
        if (!scrollBuildingSelectors(delta)) zoomMap(delta,mouseX,mouseY);
    }
	else if(event.type==SDL_EVENT_MOUSE_MOTION)
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
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-dockWidth(), globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				performAction("zone drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingTerrain)
		{
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-dockWidth(), globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
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
		else if(isDraggingNoResourceGrowthArea)
		{
			performAction("no ressource growth area drag motion", relMouseX, relMouseY);
		}
	}
	else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP)
	{
		// Button events carry their own position; resync the cached motion
		// position to it before dispatching. A warped or synthetic click can
		// arrive without a preceding motion event, and every hit-test and
		// performAction handler below reads mouseX/mouseY — without the
		// resync they would act at the stale motion position instead of
		// where the click landed.
		mouseX=event.button.x;
		mouseY=event.button.y;
        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT && clickMapZoomControls(camera,mouseX,mouseY))
        {viewportX=camera.tileX();viewportY=camera.tileY();return;}
		handleMouseButtonEvent(event);
	}
	else if(event.type==SDL_EVENT_KEY_DOWN)
	{
		handleKeyPressed(event.key, true);
	}
	else if(event.type==SDL_EVENT_KEY_UP)
	{
		handleKeyPressed(event.key, false);
	}
}



void MapEdit::handleMouseButtonEvent(SDL_Event& event)
{
	if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT)
	{
		if((phone || dock || !findAction(event.button.x, event.button.y)) && camera.contains(mouseX,mouseY) && widgetRectangle(0, 16, globalContainer->gfx->getW()-dockWidth(), globalContainer->gfx->getH()).is_in(mouseX, mouseY))
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
			else if(selectionMode==ChangeNoResourceGrowthAreas)
				performAction("no ressource growth area drag start");
			else
			{
				performAction("select map unit");
				performAction("select map building");
				if (!phone)
				{
					isLeftScrollDragging = true;
					if (!isScrollDragging) performAction("scroll drag start");
				}
			}
		}
		else if(!dock && widgetRectangle(globalContainer->gfx->getW()-dockWidth()+RIGHT_MENU_OFFSET+14, 14, 100, 100).is_in(mouseX, mouseY))
			performAction("minimap drag start");
	}
	else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_RIGHT)
	{
		if(selectionMode==PlaceNothing || selectionMode==EditingUnit || selectionMode==EditingBuilding)
			performAction("change menu");
		if(selectionMode!=PlaceNothing)
			performAction("unselect");
	}
	else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_MIDDLE)
	{
		isMiddleScrollDragging = true;
		if (!isScrollDragging) performAction("scroll drag start");
	}
	else if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT)
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
		if(isDraggingNoResourceGrowthArea)
			performAction("no ressource growth area drag end");
		if(isLeftScrollDragging)
		{
			isLeftScrollDragging = false;
			if (!isMiddleScrollDragging) performAction("scroll drag stop");
		}
	}
	else if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_MIDDLE)
	{
		isMiddleScrollDragging = false;
		if(isScrollDragging && !isLeftScrollDragging) performAction("scroll drag stop");
	}
}



void MapEdit::handleKeyPressed(SDL_KeyboardEvent key, bool pressed)
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
		case MapEditKeyActions::FocusBrushSearch:
		{
			if (dock)
			{
				dock->focusSearch();
				// The key's own text arrives next; it must not land in the field.
				swallowSearchKeyText = true;
			}
		}
		break;
		case MapEditKeyActions::SwitchToResourcesView:
		{
			if (dock)
				dock->showTab(EditorDock::Tab::Resources);
			else
				performAction("switch to terrain view");
		}
		break;
	}
}



void MapEdit::suspendInput()
{
    if(phone) phone->cancel();
    inputState.clearHeld();
    xSpeed = ySpeed = 0;
    isDraggingMinimap = isScrollDragging = false;
    isLeftScrollDragging = isMiddleScrollDragging = false;
    isDraggingZone = isDraggingTerrain = isDraggingDelete = false;
    isDraggingArea = isDraggingNoResourceGrowthArea = false;
    // The parent will not receive pointer motion while its child is active.
    // Neutralize edge scrolling until a new motion event arrives.
    mouseX = globalContainer->gfx->getW() / 2;
    mouseY = globalContainer->gfx->getH() / 2;
}


// Pointer events over the dock, or while it owns a press, drag or scroll, go to
// the dock; so do keys and text while its search field is editing. Map strokes
// and drags that started on the map keep their events wherever the pointer goes.
bool MapEdit::routeToDock(SDL_Event& event)
{
	const bool mapCapture = isDraggingMinimap || isDraggingZone || isDraggingTerrain || isScrollDragging ||
		isDraggingDelete || isDraggingArea || isDraggingNoResourceGrowthArea;
	int x = 0, y = 0;
	bool pointer = true;
	switch (event.type)
	{
	case SDL_EVENT_MOUSE_MOTION: x = int(event.motion.x); y = int(event.motion.y); break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: x = int(event.button.x); y = int(event.button.y); break;
	case SDL_EVENT_MOUSE_WHEEL: x = int(event.wheel.mouse_x); y = int(event.wheel.mouse_y); break;
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_MOTION:
	case SDL_EVENT_FINGER_UP:
		x = int(event.tfinger.x * globalContainer->gfx->getW());
		y = int(event.tfinger.y * globalContainer->gfx->getH());
		break;
	default:
		pointer = GAGCore::scrollGesture(event).has_value();
		if (pointer)
		{
			const auto sample = GAGCore::scrollGesture(event);
			x = int(sample->x); y = int(sample->y);
		}
		break;
	}
	if (pointer)
	{
		if (mapCapture)
			return false;
		if (dock->interacting() || dock->contains(x, y))
		{
			if (event.type == SDL_EVENT_MOUSE_MOTION)
			{
				// The map's own pointer leaves for the dock: no brush preview there.
				mouseX = x;
				mouseY = y;
			}
			dock->eventLogical(event);
			return true;
		}
		// Hover elsewhere still updates the dock (tooltips end, the wheel target).
		if (event.type == SDL_EVENT_MOUSE_MOTION)
			dock->eventLogical(event);
		return false;
	}
	const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
		event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING;
	if (!keyboard)
		return false;
	if (event.type == SDL_EVENT_TEXT_INPUT && swallowSearchKeyText)
	{
		swallowSearchKeyText = false;
		const std::string text = event.text.text ? event.text.text : "";
		if (text == "/" || text == "f" || text == "F")
			return true;
	}
	if (event.type == SDL_EVENT_KEY_DOWN)
		swallowSearchKeyText = false;
	if (!dock->editingText())
		return false;
	dock->eventLogical(event);
	return true;
}
