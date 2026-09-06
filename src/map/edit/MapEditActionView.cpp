// SPDX-License-Identifier: GPL-3.0-or-later

#include <GAG.h>
#include "GameGUILoadSave.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "SDLCompat.h"

bool MapEdit::performViewAction(const std::string& action, int relMouseX, int relMouseY)
{
	if(action=="scroll drag start")
	{
		isScrollDragging=true;
	}
	else if(action=="scroll drag motion")
	{
		viewportX+=relMouseX;
		viewportY+=relMouseY;
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
		noRessourceGrowthButton->setUnselected();
		isDraggingZone=false;
		isDraggingTerrain=false;
		isDraggingDelete=false;
		isDraggingArea=false;
		isDraggingNoRessourceGrowthArea=false;
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
		menuScreen=new MapEditMenuScreen;
		showingMenuScreen=true;
	}
	else if(action=="close menu screen")
	{
		delete menuScreen;
		menuScreen=NULL;
		showingMenuScreen=false;
	}
	else if(action=="open load screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=new LoadSaveScreen("maps", "map", true, Toolkit::getStringTable()->getString("[load game]"), game.mapHeader.getMapName().c_str(), glob2FilenameToName, glob2NameToFilename);
		showingLoad=true;
	}
	else if(action=="close load screen")
	{
		delete loadSaveScreen;
		showingLoad=false;
		loadSaveScreen=NULL;
	}
	else if(action=="open save screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=new LoadSaveScreen("maps", "map", false, Toolkit::getStringTable()->getString("[save game]"), game.mapHeader.getMapName().c_str(), glob2FilenameToName, glob2NameToFilename);
		showingSave=true;
	}
	else if(action=="close save screen")
	{
		delete loadSaveScreen;
		showingSave=false;
		loadSaveScreen=NULL;
	}
	else if(action=="open scenario editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		scriptEditor=new ScriptEditorScreen(&game);
		showingScriptEditor=true;
		hasMapBeenModified=true;
	}
	else if(action=="close scenario editor")
	{
		delete scriptEditor;
		showingScriptEditor=false;
		scriptEditor=NULL;
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

		teamsEditor=new TeamsEditor(&game);
		showingTeamsEditor=true;
		hasMapBeenModified=true;
	}
	else if(action=="close teams editor")
	{
		delete teamsEditor;
		showingTeamsEditor=false;
		teamsEditor=NULL;
	}
	else if(action=="open area name")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		areaName=new AskForTextInput("[Change Area Name]", game.map.getAreaName(areaNumber->getIndex()));
		isShowingAreaName=true;
	}
	else if(action=="close area name")
	{
		game.map.setAreaName(areaNumber->getIndex(), areaName->getText());
		performAction("update script area number");
		delete areaName;
		isShowingAreaName=false;
		areaName=NULL;
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
			FertilityCalculatorDialog dialog(globalContainer->gfx, game.map);
			dialog.runModal();
			overlay.forceRecompute();
			overlay.compute(game, OverlayArea::Fertility, team);
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
