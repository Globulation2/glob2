// SPDX-License-Identifier: GPL-3.0-or-later

#include <GAG.h>
#include "gui/LoadSaveDialog.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "ScriptEditorScreen.h"
#include "Utilities.h"
#include <SDL3/SDL.h>

bool MapEdit::performViewAction(const std::string& action, float relMouseX, float relMouseY)
{
	if(action=="scroll drag start")
	{
		isScrollDragging=true;
	}
	else if(action=="scroll drag motion")
	{
		const int direction = isLeftScrollDragging ? -1 : 1;
		camera.originX+=direction*relMouseX/camera.zoom;
		camera.originY+=direction*relMouseY/camera.zoom;
		camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();
	}
	else if(action=="scroll drag stop")
	{
		isScrollDragging=false;
	}
	else if(action=="switch to building view")
	{
		panelMode=AddBuildings;
		enableOnlyGroup("building view");
		performAction("unselect");
	}
	else if(action=="switch to flag view")
	{
		panelMode=AddFlagsAndZones;
		enableOnlyGroup("flag view");
		performAction("unselect");
	}
	else if(action=="switch to terrain view")
	{
		panelMode=Terrain;
		enableOnlyGroup("terrain view");
		performAction("unselect");
	}
	else if(action=="switch to teams view")
	{
		panelMode=Teams;
		enableOnlyGroup("teams view");
		performAction("unselect");
	}
	else if(action=="unselect")
	{
		selectionName="";
		brush.unselect();
		selectionMode=PlaceNothing;
		brushType=NoBrush;
		terrainType=TerrainSelector::NoTerrain;
		placingUnit=NoUnit;
		selectedUnitGID=NOGUID;
		view.selectedUnit=NULL;
		deleteButton->setUnselected();
		areasButton->setUnselected();
		noResourceGrowthButton->setUnselected();
		isDraggingZone=false;
		isDraggingTerrain=false;
		isDraggingDelete=false;
		isDraggingArea=false;
		isDraggingNoResourceGrowthArea=false;
		if(panelMode==UnitEditor)
			performAction("switch to building view");
	}
	else if(action=="change menu")
	{
		if(panelMode==AddBuildings)
			performAction("switch to flag view");
		else if(panelMode==AddFlagsAndZones)
			performAction("switch to terrain view");
		else if(panelMode==Terrain)
			performAction("switch to teams view");
		else if(panelMode==Teams)
			performAction("switch to building view");
		else
			performAction("switch to building view");
	}
	else if(action=="minimap drag start")
	{
		isDraggingMinimap=true;
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH-RIGHT_MENU_OFFSET, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag motion")
	{
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH-RIGHT_MENU_OFFSET, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag stop")
	{
		isDraggingMinimap=false;
	}
	else if(action=="open menu screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		menuScreen=std::make_unique<MapEditMenuScreen>();
		attachDialog(*menuScreen);
		showingMenuScreen=true;
	}
	else if(action=="close menu screen")
	{
		menuScreen.reset();
		showingMenuScreen=false;
	}
	else if(action=="open load screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=std::make_unique<LoadSaveDialog>("maps", "map", true, Toolkit::getStringTable()->getString("[load map]"), game.mapHeader.getMapName().c_str(), glob2FilenameToName, glob2NameToFilename, Glob2UI::Surface::Editor);
		attachDialog(*loadSaveScreen);
		showingLoad=true;
	}
	else if(action=="close load screen")
	{
		loadSaveScreen.reset();
		showingLoad=false;
	}
	else if(action=="open save screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=std::make_unique<LoadSaveDialog>("maps", "map", false, Toolkit::getStringTable()->getString("[save map]"), game.mapHeader.getMapName().c_str(), glob2FilenameToName, glob2NameToFilename, Glob2UI::Surface::Editor);
		attachDialog(*loadSaveScreen);
		showingSave=true;
	}
	else if(action=="close save screen")
	{
		loadSaveScreen.reset();
		showingSave=false;
	}
	else if(action=="open scenario editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		scriptEditor=std::make_unique<ScriptEditorScreen>(&game);
		attachDialog(*scriptEditor);
		showingScriptEditor=true;
		hasMapBeenModified=true;
	}
	else if(action=="close scenario editor")
	{
		scriptEditor.reset();
		showingScriptEditor=false;
	}
	else if(action=="open teams editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");

		for (int i=0; i<game.mapHeader.getNumberOfTeams(); ++i)
		{
			game.mapHeader.getBaseTeam(i)=*game.teams[i];
		}

		teamsEditor=std::make_unique<TeamsEditor>(&game);
		attachDialog(*teamsEditor);
		showingTeamsEditor=true;
		hasMapBeenModified=true;
	}
	else if(action=="close teams editor")
	{
		teamsEditor.reset();
		showingTeamsEditor=false;
	}
	else if(action=="open area name")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		areaName=std::make_unique<AskForTextInput>("[Change Area Name]", game.map.getAreaName(areaNumber->getIndex()));
		attachDialog(*areaName);
		isShowingAreaName=true;
	}
	else if(action=="close area name")
	{
		game.map.setAreaName(areaNumber->getIndex(), areaName->getText());
		performAction("update script area number");
		areaName.reset();
		isShowingAreaName=false;
	}
	else if(action=="update script area number")
	{
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
		hasMapBeenModified = true;
	}
	else if(action=="compute fertility")
	{
		//Only compute when its x'ed in, not otherwise
		if(isFertilityOn)
		{
			fertilityRequested = true;
		}
	}
	else if(action=="quit editor")
	{
		doQuit=true;
	}
	else
		return false;
	return true;
}
