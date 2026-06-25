// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <cmath>
#include <FormatableString.h>
#include <GAG.h>
#include "GameGUILoadSave.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "MapEditKeyActions.h"
#include "ScriptEditorScreen.h"
#include <sstream>
#include <StreamFilter.h>
#include <Stream.h>
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "GUIMessageBox.h"
#include "SDLCompat.h"

void MapEdit::performAction(const std::string& action, int relMouseX, int relMouseY)
{
//	std::cout<<action<<std::endl;
	if(action.find("&")!=std::string::npos)
	{
		int pos=action.find("&");
		performAction(action.substr(0, pos));
		performAction(action.substr(pos+1, action.size()-pos-1));
	}
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
	else if(action.substr(0, 29)=="set place building selection ")
	{
		performAction("unselect");
		std::string type=action.substr(29, action.size()-29);
		selectionName=type;
		selectionMode=PlaceBuilding;
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
		game.view.selectedUnit=NULL;
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
				if (game.teams[team]->startPosSet<3)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=3;
				}
			}
			else
			{
				if (game.teams[team]->startPosSet<2)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=2;
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
	else if(action=="select forbidden zone")
	{
		performAction("unselect");
		brushType = ForbiddenBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select clearing zone")
	{
		performAction("unselect");
		brushType = ClearAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select guard zone")
	{
		performAction("unselect");
		brushType = GuardAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
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
		lastPlacementX=-1;
		lastPlacementY=-1;
		firstPlacementX=-1;
		firstPlacementY=-1;
	}
	else if(action=="select grass")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Grass;
		selectionMode=PlaceTerrain;
		
		brush.defaultSelection();
		brush.setAddRemoveEnabledState(false);
	}
	else if(action=="select sand")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Sand;
		selectionMode=PlaceTerrain;
		
		brush.defaultSelection();
		brush.setAddRemoveEnabledState(false);
	}
	else if(action=="select water")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Water;
		selectionMode=PlaceTerrain;
		
		brush.defaultSelection();
		brush.setAddRemoveEnabledState(false);
	}
	else if(action=="select wheat")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Wheat;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select trees")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Trees;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select stone")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Stone;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select algae")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Algae;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select papyrus")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Papyrus;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select cherry tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::CherryTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select orange tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::OrangeTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
	}
	else if(action=="select prune tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::PruneTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
		brush.setAddRemoveEnabledState(true);
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
		lastPlacementX=-1;
		lastPlacementY=-1;
		firstPlacementX=-1;
		firstPlacementY=-1;
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
		lastPlacementX=-1;
		lastPlacementY=-1;
		firstPlacementX=-1;
		firstPlacementY=-1;
	}
	else if(action=="update script area number")
	{
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
		hasMapBeenModified = true;
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
		lastPlacementX=-1;
		lastPlacementY=-1;
		firstPlacementX=-1;
		firstPlacementY=-1;
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
		lastPlacementX=-1;
		lastPlacementY=-1;
		firstPlacementX=-1;
		firstPlacementY=-1;
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
	else if(action=="select worker")
	{
		performAction("unselect");
		placingUnit=Worker;
		selectionMode=PlaceUnit;
	}
	else if(action=="select warrior")
	{
		performAction("unselect");
		placingUnit=Warrior;
		selectionMode=PlaceUnit;
	}
	else if(action=="select explorer")
	{
		performAction("unselect");
		placingUnit=Explorer;
		selectionMode=PlaceUnit;
	}
	else if(action=="select unit level 1")
	{
		placingUnitLevel=0;
	}
	else if(action=="select unit level 2")
	{
		placingUnitLevel=1;
	}
	else if(action=="select unit level 3")
	{
		placingUnitLevel=2;
	}
	else if(action=="select unit level 4")
	{
		placingUnitLevel=3;
	}
	else if(action=="place unit")
	{
		int type=0;
		if(placingUnit==Worker)
			type=WORKER;
		else if(placingUnit==Warrior)
			type=WARRIOR;
		else if(placingUnit==Explorer)
			type=EXPLORER;
		int level=placingUnitLevel;

		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		if (unit)
		{
			if (game.teams[team]->startPosSet<1)
			{
				game.teams[team]->startPosX=viewportX;
				game.teams[team]->startPosY=viewportY;
				game.teams[team]->startPosSet=1;
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
		}
	}
	else if(action=="select map unit")
	{
		int x;
		int y;
		int gid=NOGUID;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		if(game.map.getAirUnit(x, y)!=NOGUID)
		{
			gid=game.map.getAirUnit(x, y);
		}
		else if(game.map.getGroundUnit(x, y)!=NOGUID)
		{
			gid=game.map.getGroundUnit(x, y);
		}
		if(gid!=NOGUID)
		{
			performAction("unselect");
			selectedUnitGID=gid;
			game.view.selectedUnit=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
			selectionMode=EditingUnit;
			panelMode=UnitEditor;
			unitInfoTitle->setUnit(game.view.selectedUnit);
			unitPicture->setUnit(game.view.selectedUnit);
			unitHPLabel->setValues(&game.view.selectedUnit->hp, &game.view.selectedUnit->performance[HP]);
			unitHPScrollBox ->setValues(&game.view.selectedUnit->hp, &game.view.selectedUnit->performance[HP]);
			unitWalkLevelLabel->setValues(&game.view.selectedUnit->level[WALK]);
			unitWalkLevelScrollBox->setValues(&game.view.selectedUnit->level[WALK]);
			unitSwimLevelLabel->setValues(&game.view.selectedUnit->level[SWIM]);
			unitSwimLevelScrollBox->setValues(&game.view.selectedUnit->level[SWIM]);
			unitHarvestLevelLabel->setValues(&game.view.selectedUnit->level[HARVEST]);
			unitHarvestLevelScrollBox->setValues(&game.view.selectedUnit->level[HARVEST]);
			unitBuildLevelLabel->setValues(&game.view.selectedUnit->level[BUILD]);
			unitBuildLevelScrollBox->setValues(&game.view.selectedUnit->level[BUILD]);
			unitAttackSpeedLevelLabel->setValues(&game.view.selectedUnit->level[ATTACK_SPEED]);
			unitAttackSpeedLevelScrollBox->setValues(&game.view.selectedUnit->level[ATTACK_SPEED]);
			unitAttackStrengthLevelLabel->setValues(&game.view.selectedUnit->level[ATTACK_STRENGTH]);
			unitAttackStrengthLevelScrollBox->setValues(&game.view.selectedUnit->level[ATTACK_STRENGTH]);
			unitMagicGroundAttackLevelLabel->setValues(&game.view.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			unitMagicGroundAttackLevelScrollBox->setValues(&game.view.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			enableOnlyGroup("unit editor");
			if(!game.view.selectedUnit->canLearn[WALK])
			{
				unitWalkLevelLabel->disable();
				unitWalkLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[SWIM])
			{
				unitSwimLevelLabel->disable();
				unitSwimLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[HARVEST])
			{
				unitHarvestLevelLabel->disable();
				unitHarvestLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[BUILD])
			{
				unitBuildLevelLabel->disable();
				unitBuildLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[ATTACK_SPEED])
			{
				unitAttackSpeedLevelLabel->disable();
				unitAttackSpeedLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[ATTACK_STRENGTH])
			{
				unitAttackStrengthLevelLabel->disable();
				unitAttackStrengthLevelScrollBox->disable();
			}
			if(!game.view.selectedUnit->canLearn[MAGIC_ATTACK_GROUND])
			{
				unitMagicGroundAttackLevelLabel->disable();
				unitMagicGroundAttackLevelScrollBox->disable();
			}
		}
	}
	else if(action=="update unit walk level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[WALK]);
		u->performance[WALK] = ut->performance[WALK];
		hasMapBeenModified = true;
	}
	else if(action=="update unit swim level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[SWIM]);
		u->performance[SWIM] = ut->performance[SWIM];
		hasMapBeenModified = true;
	}
	else if(action=="update unit harvest level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[HARVEST]);
		u->performance[HARVEST] = ut->performance[HARVEST];
		hasMapBeenModified = true;
	}
	else if(action=="update unit build level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[BUILD]);
		u->performance[BUILD] = ut->performance[BUILD];
		hasMapBeenModified = true;
	}
	else if(action=="update unit attack speed level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_SPEED]);
		u->performance[ATTACK_SPEED] = ut->performance[ATTACK_SPEED];
		hasMapBeenModified = true;
	}
	else if(action=="update unit attack strength level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_STRENGTH]);
		u->performance[ATTACK_STRENGTH] = ut->performance[ATTACK_STRENGTH];
		hasMapBeenModified = true;
	}
	else if(action=="update unit magic ground attack level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[MAGIC_ATTACK_GROUND]);
		u->performance[MAGIC_ATTACK_GROUND] = ut->performance[MAGIC_ATTACK_GROUND];
		hasMapBeenModified = true;
	}
	else if(action=="update unit")
	{
		hasMapBeenModified = true;
	}
	else if(action=="select map building")
	{
		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		int gid=NOGBID;
		for(int t=0; t<Team::MAX_COUNT; ++t)
		{
			if(game.teams[t] && gid==NOGBID)
			{
				for (std::list<Building *>::iterator virtualIt=game.teams[t]->virtualBuildings.begin();
						virtualIt!=game.teams[t]->virtualBuildings.end(); ++virtualIt)
				{
					{
						Building *b=*virtualIt;
						if ((b->posX==x) && (b->posY==y))
						{
							gid=b->gid;
							break;
						}
					}
				}
			}
		}
		if(gid==NOGBID && game.map.getBuilding(x, y)!=NOGUID)
		{
			gid=game.map.getBuilding(x, y);
		}
		if(gid!=NOGBID)
		{
			performAction("unselect");
			Building* b=game.teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
			selectionMode=EditingBuilding;
			panelMode=BuildingEditor;
			selectedBuildingGID=gid;
			enableOnlyGroup("building editor");
			buildingInfoTitle->setBuilding(b);
			buildingPicture->setBuilding(b);
			bool hpLabel=false;
			buildingHPLabel->setValues(&b->hp, &b->type->hpMax);
			buildingHPScrollBox->setValues(&b->hp, &b->type->hpMax);
			bool foodLabel=false;
			buildingFoodQuantityLabel->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			buildingFoodQuantityScrollBox->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			bool assignedLabel=false;
			buildingAssignedLabel->setValues(&b->maxUnitWorking);
			buildingAssignedScrollBox->setValues(&b->maxUnitWorking);
			bool workerRatioLabel=false;
			buildingWorkerRatioLabel->setValues(&b->ratio[WORKER]);
			buildingWorkerRatioScrollBox->setValues(&b->ratio[WORKER]);
			bool explorerRatioLabel=false;
			buildingExplorerRatioLabel->setValues(&b->ratio[EXPLORER]);
			buildingExplorerRatioScrollBox->setValues(&b->ratio[EXPLORER]);
			bool warriorRatioLabel=false;
			buildingWarriorRatioLabel->setValues(&b->ratio[WARRIOR]);
			buildingWarriorRatioScrollBox->setValues(&b->ratio[WARRIOR]);
			bool cherryLabel=false;
			buildingCherryLabel->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			buildingCherryScrollBox->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			bool orangeLabel=false;
			buildingOrangeLabel->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			buildingOrangeScrollBox->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			bool pruneLabel=false;
			buildingPruneLabel->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			buildingPruneScrollBox->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			bool stoneLabel=false;
			buildingStoneLabel->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			buildingStoneScrollBox->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			bool bulletsLabel=false;
			buildingBulletsLabel->setValues(&b->bullets, &b->type->maxBullets);
			buildingBulletsScrollBox->setValues(&b->bullets, &b->type->maxBullets);
			bool minimumLevel=false;
			buildingMinimumLevelLabel->setValues(&b->minLevelToFlag);
			buildingMinimumLevelScrollBox->setValues(&b->minLevelToFlag);
			bool radius=false;
			buildingRadiusLabel->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			buildingRadiusScrollBox->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			if(b->type->isBuildingSite)
			{
				hpLabel=true;
				assignedLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SWARM_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
				workerRatioLabel=true;
				explorerRatioLabel=true;
				warriorRatioLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::HEAL_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				stoneLabel=true;
				bulletsLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				assignedLabel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			else if(b->shortTypeNum==IntBuildingType::STONE_WALL)
			{
				hpLabel=true;
			}
			else if(b->shortTypeNum==IntBuildingType::MARKET_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				cherryLabel=true;
				orangeLabel=true;
				pruneLabel=true;
			}

			int ypos=252;
			if(!hpLabel)
			{
				buildingHPLabel->disable();
				buildingHPScrollBox->disable();
			}
			else
			{
				buildingHPLabel->area.y=ypos;
				buildingHPScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!foodLabel)
			{
				buildingFoodQuantityLabel->disable();
				buildingFoodQuantityScrollBox->disable();
			}
			else
			{
				buildingFoodQuantityLabel->area.y=ypos;
				buildingFoodQuantityScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!assignedLabel)
			{
				buildingAssignedLabel->disable();
				buildingAssignedScrollBox->disable();
			}
			else
			{
				buildingAssignedLabel->area.y=ypos;
				buildingAssignedScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!workerRatioLabel)
			{
				buildingWorkerRatioLabel->disable();
				buildingWorkerRatioScrollBox->disable();
			}
			else
			{
				buildingWorkerRatioLabel->area.y=ypos;
				buildingWorkerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!explorerRatioLabel)
			{
				buildingExplorerRatioLabel->disable();
				buildingExplorerRatioScrollBox->disable();
			}
			else
			{
				buildingExplorerRatioLabel->area.y=ypos;
				buildingExplorerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!warriorRatioLabel)
			{
				buildingWarriorRatioLabel->disable();
				buildingWarriorRatioScrollBox->disable();
			}
			else
			{
				buildingWarriorRatioLabel->area.y=ypos;
				buildingWarriorRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!cherryLabel)
			{
				buildingCherryLabel->disable();
				buildingCherryScrollBox->disable();
			}
			else
			{
				buildingCherryLabel->area.y=ypos;
				buildingCherryScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!orangeLabel)
			{
				buildingOrangeLabel->disable();
				buildingOrangeScrollBox->disable();
			}
			else
			{
				buildingOrangeLabel->area.y=ypos;
				buildingOrangeScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!pruneLabel)
			{
				buildingPruneLabel->disable();
				buildingPruneScrollBox->disable();
			}
			else
			{
				buildingPruneLabel->area.y=ypos;
				buildingPruneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!stoneLabel)
			{
				buildingStoneLabel->disable();
				buildingStoneScrollBox->disable();
			}
			else
			{
				buildingStoneLabel->area.y=ypos;
				buildingStoneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!bulletsLabel)
			{
				buildingBulletsLabel->disable();
				buildingBulletsScrollBox->disable();
			}
			else
			{
				buildingBulletsLabel->area.y=ypos;
				buildingBulletsScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!minimumLevel)
			{
				buildingMinimumLevelLabel->disable();
				buildingMinimumLevelScrollBox->disable();
			}
			else
			{
				buildingMinimumLevelLabel->area.y=ypos;
				buildingMinimumLevelScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!radius)
			{
				buildingRadiusLabel->disable();
				buildingRadiusScrollBox->disable();
			}
			else
			{
				buildingRadiusLabel->area.y=ypos;
				buildingRadiusScrollBox->area.y=ypos+16;
				ypos+=32;
			}
		}
	}
	else if(action=="update building")
	{
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
}



