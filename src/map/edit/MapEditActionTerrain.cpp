// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "SDLCompat.h"

void MapEdit::beginZonePlacement(BrushType type)
{
	performAction("unselect");
	brushType = type;
	selectionMode=PlaceZone;
	if (brush.getType() == BrushTool::MODE_NONE)
		brush.defaultSelection();
	brush.setAddRemoveEnabledState(true);
}

void MapEdit::beginTerrainPlacement(TerrainSelector::TerrainType type, TerrainPlacementMode mode)
{
	performAction("unselect");
	terrainType=type;
	selectionMode=PlaceTerrain;
	const bool isResource = mode == TerrainPlacementMode::Resource;
	if (!isResource || brush.getType() == BrushTool::MODE_NONE)
		brush.defaultSelection();
	brush.setAddRemoveEnabledState(isResource);
}

void MapEdit::resetPlacementTracking()
{
	lastPlacementX=-1;
	lastPlacementY=-1;
	firstPlacement.reset();
}

bool MapEdit::performTerrainAction(const std::string& action, int relMouseX, int relMouseY)
{
	if(action.substr(0, 29)=="set place building selection ")
	{
		performAction("unselect");
		std::string type=action.substr(29, action.size()-29);
		selectionName=type;
		selectionMode=PlaceBuilding;
	}
	else if(action=="place building")
	{
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, buildingLevel, false);
		if(!isUpgradable(IntBuildingType::shortNumberFromType(selectionName)))
			typeNum = globalContainer->buildingsTypes.getTypeNum(selectionName, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		int tempX, tempY, x, y;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, bt, &x, &y, team, false))
		{
			if(bt->maxUnitWorking)
				game.addBuilding(x, y, typeNum, team, 1, 0);
			else
				game.addBuilding(x, y, typeNum, team, 0, 0);
			if (selectionName=="swarm")
			{
				if (game.teams[team]->startPosSet<Team::START_POS_FROM_SWARM)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=Team::START_POS_FROM_SWARM;
				}
			}
			else
			{
				if (game.teams[team]->startPosSet<Team::START_POS_FROM_BUILDING)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=Team::START_POS_FROM_BUILDING;
				}
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
		}
	}
	else if(action=="switch to building level 1")
	{
		buildingLevel=0;
	}
	else if(action=="switch to building level 2")
	{
		buildingLevel=1;
	}
	else if(action=="switch to building level 3")
	{
		buildingLevel=2;
	}
	else if(action=="select forbidden zone")
	{
		beginZonePlacement(ForbiddenBrush);
	}
	else if(action=="select clearing zone")
	{
		beginZonePlacement(ClearAreaBrush);
	}
	else if(action=="select guard zone")
	{
		beginZonePlacement(GuardAreaBrush);
	}
	else if(action=="handle zone click")
	{
		if(brushType==NoBrush)
		{
			performAction("unselect");
			performAction("select forbidden zone");
		}
		brush.handleClick(relMouseX, relMouseY);
	}
	else if(action=="zone drag start")
	{
		isDraggingZone=true;
		handleBrushClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="zone drag motion")
	{
		handleBrushClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="zone drag end")
	{
		isDraggingZone=false;
		resetPlacementTracking();
	}
	else if(action=="select grass")
	{
		beginTerrainPlacement(TerrainSelector::Grass, TerrainPlacementMode::BaseTerrain);
	}
	else if(action=="select sand")
	{
		beginTerrainPlacement(TerrainSelector::Sand, TerrainPlacementMode::BaseTerrain);
	}
	else if(action=="select water")
	{
		beginTerrainPlacement(TerrainSelector::Water, TerrainPlacementMode::BaseTerrain);
	}
	else if(action=="select wheat")
	{
		beginTerrainPlacement(TerrainSelector::Wheat, TerrainPlacementMode::Resource);
	}
	else if(action=="select trees")
	{
		beginTerrainPlacement(TerrainSelector::Trees, TerrainPlacementMode::Resource);
	}
	else if(action=="select stone")
	{
		beginTerrainPlacement(TerrainSelector::Stone, TerrainPlacementMode::Resource);
	}
	else if(action=="select algae")
	{
		beginTerrainPlacement(TerrainSelector::Algae, TerrainPlacementMode::Resource);
	}
	else if(action=="select papyrus")
	{
		beginTerrainPlacement(TerrainSelector::Papyrus, TerrainPlacementMode::Resource);
	}
	else if(action=="select cherry tree")
	{
		beginTerrainPlacement(TerrainSelector::CherryTree, TerrainPlacementMode::Resource);
	}
	else if(action=="select orange tree")
	{
		beginTerrainPlacement(TerrainSelector::OrangeTree, TerrainPlacementMode::Resource);
	}
	else if(action=="select prune tree")
	{
		beginTerrainPlacement(TerrainSelector::PruneTree, TerrainPlacementMode::Resource);
	}
	else if(action=="select delete objects")
	{
		performAction("unselect");
		selectionMode=RemoveObject;
		deleteButton->setSelected();

		brush.defaultSelection();
		brush.setAddRemoveEnabledState(false);
	}
	else if(action=="select no ressources growth")
	{
		performAction("unselect");
		selectionMode=ChangeNoRessourceGrowthAreas;
		noRessourceGrowthButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="handle terrain click")
	{
		if(terrainType==TerrainSelector::NoTerrain && selectionMode!=RemoveObject && selectionMode!=ChangeAreas && selectionMode!=ChangeNoRessourceGrowthAreas)
			performAction("select grass");
		brush.handleClick(relMouseX, relMouseY);
	}
	else if(action=="terrain drag start")
	{
		isDraggingTerrain=true;
		handleTerrainClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="terrain drag motion")
	{
		handleTerrainClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="terrain drag end")
	{
		isDraggingTerrain=false;
		resetPlacementTracking();
	}
	else if(action=="delete drag start")
	{
		isDraggingDelete=true;
		handleDeleteClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="delete drag motion")
	{
		handleDeleteClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="delete drag end")
	{
		isDraggingDelete=false;
		resetPlacementTracking();
	}
	else if(action=="select change areas")
	{
		performAction("unselect");
		selectionMode=ChangeAreas;
		areasButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="area drag start")
	{
		isDraggingArea=true;
		handleAreaClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="area drag motion")
	{
		handleAreaClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="area drag end")
	{
		isDraggingArea=false;
		resetPlacementTracking();
	}
	else if(action=="no ressource growth area drag start")
	{
		isDraggingNoRessourceGrowthArea=true;
		handleNoRessourceGrowthClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="no ressource growth area drag motion")
	{
		handleNoRessourceGrowthClick(mouseX, mouseY);
		hasMapBeenModified = true;
	}
	else if(action=="no ressource growth area drag end")
	{
		isDraggingNoRessourceGrowthArea=false;
		resetPlacementTracking();
	}
	else if(action=="add team")
	{
		if(game.mapHeader.getNumberOfTeams() < 12)
		{
			game.addTeam();
			regenerateGameHeader();
		}
		hasMapBeenModified = true;
	}
	else if(action=="remove team")
	{
		if(game.mapHeader.getNumberOfTeams() > 1)
		{
			if(team==game.mapHeader.getNumberOfTeams()-1)
				team-=1;
			game.removeTeam();
			regenerateGameHeader();
		}
		hasMapBeenModified = true;
	}
	else if(action=="select active team")
	{
		int n=relMouseX/16 + (relMouseY/16)*6;
		if(game.teams[n])
		{
			team=n;
			game.map.computeDisplayedForbidden(team);
			game.map.computeDisplayedClearArea(team);
			game.map.computeDisplayedGuardArea(team);
		}
	}
	else
		return false;
	return true;
}
