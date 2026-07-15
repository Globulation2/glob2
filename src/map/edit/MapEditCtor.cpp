// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <GAG.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Utilities.h"
#include "SDLCompat.h"


MapEdit::MapEdit()
  : game(NULL, this), keyboardManager(MapEditShortcuts), 
    minimap(globalContainer->runNoX,
            RIGHT_MENU_WIDTH, // menu width
            globalContainer->gfx->getW(), // game width
            20, // x offset
            5, // y offset
            128, // width
            128, // height
            Minimap::HideFOW)
{
	doQuit=false;
	doFullQuit=false;
	doQuitAfterLoadSave=false;

	// default value;
	viewportX=0;
	viewportY=0;
	xSpeed=0;
	ySpeed=0;
	mouseX=0;
	mouseY=0;
	relMouseX=0;
	relMouseY=0;
	wasMinimapRendered=false;

	// load menu
	menu=Toolkit::getSprite("data/gui/editor");

	// editor facilities
	hasMapBeenModified=false;
	team=0;

	selectionMode=PlaceNothing;

	int decX = RIGHT_MENU_OFFSET;

	panelMode=AddBuildings;
	buildingView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 136, 32, 32), "any", "building view icon", "switch to building view", 0, AddBuildings);
	flagsView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, 136, 32, 32), "any", "flag view icon", "switch to flag view", 28, AddFlagsAndZones);
	terrainView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, 136, 32, 32), "any", "terrain view icon", "switch to terrain view", 31, Terrain);
	teamsView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX, 136, 32, 32), "any", "teams view icon", "switch to teams view", 33, Teams);
	menuIcon = new MenuIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-32+decX, 0, 32, 32), "any", "menu icon", "open menu screen");
	mapCoordinatesLabel = new TextLabel(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+decX, globalContainer->gfx->getH()-95, 75, 10), "any", "map coordinates label", "do nothing", "", false, "0 0");
	addWidget(buildingView);
	addWidget(flagsView);
	addWidget(terrainView);
	addWidget(teamsView);
	addWidget(menuIcon);
	addWidget(mapCoordinatesLabel);
	swarm = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+12+decX, 128+32+6, 40, 40), "building view", "swarm", "set place building selection swarm", "swarm", true);
	inn = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+12+decX, 128+32+6, 40, 40), "building view", "inn", "set place building selection inn", "inn", true);
	hospital = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+12+decX, 128+32+46*1+6, 40, 40), "building view", "hospital", "set place building selection hospital", "hospital", true);
	racetrack = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+12+decX, 128+32+46*1+6, 40, 40), "building view", "racetrack", "set place building selection racetrack", "racetrack", true);
	swimmingpool = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+12+decX, 128+32+46*2+6, 40, 40), "building view", "swimmingpool", "set place building selection swimmingpool", "swimmingpool", true);
	barracks = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+12+decX, 128+32+46*2+6, 40, 40), "building view", "barracks", "set place building selection barracks", "barracks", true);
	school = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+12+decX, 128+32+46*3+6, 40, 40), "building view", "school", "set place building selection school", "school", true);
	defencetower = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+12+decX, 128+32+46*3+6, 40, 40), "building view", "defencetower", "set place building selection defencetower", "defencetower", true);
	stonewall = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+12+decX, 128+32+46*4+6, 40, 40), "building view", "stonewall", "set place building selection stonewall", "stonewall", true);
	market = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+12+decX, 128+32+46*4+6, 40, 40), "building view", "market", "set place building selection market", "market", true);
	building_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 16+decX, globalContainer->gfx->getH()-74, 96, 32 ), "building view", "building view team selector", "select active team");
	building_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 1", "switch to building level 1", 1, buildingLevel);
	building_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 2", "switch to building level 2", 2, buildingLevel);
	building_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 3", "switch to building level 3", 3, buildingLevel);
	addWidget(swarm);
	addWidget(inn);
	addWidget(hospital);
	addWidget(racetrack);
	addWidget(swimmingpool);
	addWidget(barracks);
	addWidget(school);
	addWidget(defencetower);
	addWidget(stonewall);
	addWidget(market);
	addWidget(building_view_tcs);
	addWidget(building_view_level1);
	addWidget(building_view_level2);
	addWidget(building_view_level3);
	explorationflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+5+decX, 128+32+7, 32, 32), "flag view", "explorationflag", "set place building selection explorationflag", "explorationflag", false);
	warflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+5+42+decX, 128+32+7, 32, 32), "flag view", "warflag", "set place building selection warflag", "warflag", false);
	clearingflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+5+84+decX, 128+32+7, 32, 32), "flag view", "clearingflag", "set place building selection clearingflag", "clearingflag", false);
	forbiddenZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 216, 32, 32), "flag view", "forbidden zone", "select forbidden zone", ZoneSelector::ForbiddenZone);
	guardZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+40+decX, 216, 32, 32), "flag view", "guard zone", "select guard zone", ZoneSelector::GuardingZone);
	clearingZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+80+decX, 216, 32, 32), "flag view", "clearing zone", "select clearing zone", ZoneSelector::ClearingZone);
	deleteButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 216+40, 112, 16), "flag view", "delete button", "select delete objects", "[delete]");
	zoneBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 216+65, BrushTool::WIDTH, BrushTool::HEIGHT), "flag view", "zone brush selector", "handle zone click", brush);
	worker = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 385, 38, 38), "flag view", "worker selector", "select worker", WORKER);
	explorer = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+48+decX, 385, 38, 38), "flag view", "explorer selector", "select explorer", EXPLORER);
	warrior = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+88+decX, 385, 38, 38), "flag view", "warrior selector", "select warrior", WARRIOR);
	flag_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 16+decX, globalContainer->gfx->getH()-74, 96, 32 ), "flag view", "flag view team selector", "select active team");
	flag_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 1", "select unit level 1", 1, placingUnitLevel);
	flag_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 2", "select unit level 2", 2, placingUnitLevel);
	flag_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 3", 3, placingUnitLevel);
	flag_view_level4 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 4", 4, placingUnitLevel);
	addWidget(warflag);
	addWidget(explorationflag);
	addWidget(clearingflag);
	addWidget(forbiddenZone);
	addWidget(guardZone);
	addWidget(clearingZone);
	addWidget(deleteButton);
	addWidget(zoneBrushSelector);
	addWidget(worker);
	addWidget(warrior);
	addWidget(explorer);
	addWidget(flag_view_tcs);
	addWidget(flag_view_level1);
	addWidget(flag_view_level2);
	addWidget(flag_view_level3);
	addWidget(flag_view_level4);

	grass = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 172, 32, 32), "terrain view", "grass selector", "select grass", TerrainSelector::Grass);
	sand = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, 172, 32, 32), "terrain view", "sand selector", "select sand", TerrainSelector::Sand);
	water = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, 172, 32, 32), "terrain view", "water selector", "select water", TerrainSelector::Water);
	wheat = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX, 172, 32, 32), "terrain view", "wheat selector", "select wheat", TerrainSelector::Wheat);
	trees = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 210, 32, 32), "terrain view", "trees selector", "select trees", TerrainSelector::Trees);
	stone = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, 210, 32, 32), "terrain view", "stone selector", "select stone", TerrainSelector::Stone);
	algae = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, 210, 32, 32), "terrain view", "algae selector", "select algae", TerrainSelector::Algae);
	papyrus = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX, 210, 32, 32), "terrain view", "papyrus selector", "select papyrus", TerrainSelector::Papyrus);
	orange = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 248, 32, 32), "terrain view", "orange selector", "select orange tree", TerrainSelector::OrangeTree);
	cherry = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, 248, 32, 32), "terrain view", "cherry selector", "select cherry tree", TerrainSelector::CherryTree);
	prune = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, 248, 32, 32), "terrain view", "prune selector", "select prune tree", TerrainSelector::PruneTree);
	noRessourceGrowthButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 294, 112, 16), "terrain view", "no ressources growth button", "select no ressources growth", "[no ressources growth areas]");
	areasButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 320, 112, 16), "terrain view", "script areas button", "select change areas", "[Script Areas]");
	areaNumber = new NumberCycler(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 336, 8, 16), "terrain view", "script area number selector", "update script area number", 9);
	areaNameLabel = new TextLabel(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+24+decX, 336, 104, 16), "terrain view", "script area name label", "open area name", "", false, Toolkit::getStringTable()->getString("[Unnamed Area]"));
	terrainBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 362, BrushTool::WIDTH, BrushTool::HEIGHT), "terrain view", "terrain brush selector", "handle terrain click", brush);
	showFertilityOverlay = new Checkbox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 466, 128, 16), "terrain view", "fertility checkbox", "compute fertility", "[Fertility Map]", isFertilityOn);
	addWidget(grass);
	addWidget(sand);
	addWidget(water);
	addWidget(wheat);
	addWidget(trees);
	addWidget(stone);
	addWidget(algae);
	addWidget(papyrus);
	addWidget(orange);
	addWidget(cherry);
	addWidget(prune);
	addWidget(noRessourceGrowthButton);
	addWidget(areasButton);
	addWidget(areaNumber);
	addWidget(areaNameLabel);
	addWidget(terrainBrushSelector);
	addWidget(showFertilityOverlay);

	increaseTeams = new PlusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 408, 32, 32), "teams view", "increase teams", "add team");
	decreaseTeams = new MinusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+40+decX, 408, 32, 32), "teams view", "decrease teams", "remove team");
	team_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 16+decX, 168, 96, 32 ), "teams view", "team view team selector", "select active team");
	addWidget(increaseTeams);
	addWidget(decreaseTeams);
	addWidget(team_view_tcs);

	unitInfoTitle = new UnitInfoTitle(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 173, 128, 16), "unit editor", "unit editor title", "", NULL);
	unitPicture = new UnitPicture(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+decX, 203, 40, 40), "unit editor", "unit editor picture", "", NULL);
	unitHPLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "unit editor", "unit editor hp label", "update unit", "[hp]", NULL, static_cast<Sint32*>(NULL));
	unitHPScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 112, 16), "unit editor", "unit editor hp scroll box", "", NULL, static_cast<Sint32*>(NULL));
	unitWalkLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 284, 128, 16), "unit editor", "unit editor walk level label", "", "[Walk]", NULL, 3);
	unitWalkLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 300, 112, 16), "unit editor", "unit editor walk level scroll box", "update unit walk level", NULL, 3);
	unitSwimLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 316, 128, 16), "unit editor", "unit editor swim level label", "", "[Swim]", NULL, 3);
	unitSwimLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 332, 112, 16), "unit editor", "unit editor swim level scroll box", "update unit swim level", NULL, 3);
	unitHarvestLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 348, 128, 16), "unit editor", "unit editor harvest level label", "", "[Harvest]", NULL, 3);
	unitHarvestLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 364, 112, 16), "unit editor", "unit editor harvest level scroll box", "update unit harvest level", NULL, 3);
	unitBuildLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 380, 128, 16), "unit editor", "unit editor build level label", "", "[Build]", NULL, 3);
	unitBuildLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 396, 112, 16), "unit editor", "unit editor build level scroll box", "update unit build level", NULL, 3);
	unitAttackSpeedLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 348, 128, 16), "unit editor", "unit editor attack speed level label", "", "[At. speed]", NULL, 3);
	unitAttackSpeedLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 364, 112, 16), "unit editor", "unit editor attack speed level scroll box", "update unit attack speed level", NULL, 3);
	unitAttackStrengthLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 380, 128, 16), "unit editor", "unit editor attack strength level label", "", "[At. strength]", NULL, 3);
	unitAttackStrengthLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 396, 112, 16), "unit editor", "unit editor attack strength level scroll box", "update unit attack strength level", NULL, 3);
	unitMagicGroundAttackLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 284, 128, 16), "unit editor", "unit editor ground attack level label", "", "[Magic At. Ground]", NULL, 3);
	unitMagicGroundAttackLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 300, 112, 16), "unit editor", "unit editor magic ground attack level scroll box", "update unit magic ground attack level", NULL, 3);
	addWidget(unitInfoTitle);
	addWidget(unitPicture);
	addWidget(unitHPLabel);
	addWidget(unitHPScrollBox);
	addWidget(unitWalkLevelLabel);
	addWidget(unitWalkLevelScrollBox);
	addWidget(unitSwimLevelLabel);
	addWidget(unitSwimLevelScrollBox);
	addWidget(unitHarvestLevelLabel);
	addWidget(unitHarvestLevelScrollBox);
	addWidget(unitBuildLevelLabel);
	addWidget(unitBuildLevelScrollBox);
	addWidget(unitAttackSpeedLevelLabel);
	addWidget(unitAttackSpeedLevelScrollBox);
	addWidget(unitAttackStrengthLevelLabel);
	addWidget(unitAttackStrengthLevelScrollBox);
	addWidget(unitMagicGroundAttackLevelLabel);
	addWidget(unitMagicGroundAttackLevelScrollBox);

	buildingInfoTitle = new BuildingInfoTitle(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+decX, 173, 128, 16), "building editor", "building editor info title", "", NULL);
	buildingPicture = new BuildingPicture(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+2+decX, 203, 56, 46), "building editor", "building editor picture", "", NULL);
	buildingHPLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor hp label", "", "[hp]", NULL, static_cast<Sint32*>(NULL));
	buildingHPScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor hp scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingFoodQuantityLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor food label", "", "[Wheat]", NULL, static_cast<Sint32*>(NULL));
	buildingFoodQuantityScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor food scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingAssignedLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor assigned label", "", "[assigned]", NULL, 20);
	buildingAssignedScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor assigned scroll box", "", NULL, 20);
	buildingWorkerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor worker ratio label", "", "[Worker Ratio]", NULL, 16);
	buildingWorkerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor worker ratio scroll box", "", NULL, 20);
	buildingExplorerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor explorer ratio label", "", "[Explorer Ratio]", NULL, 16);
	buildingExplorerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor explorer ratio scroll box", "", NULL, 20);
	buildingWarriorRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor warrior ratio label", "", "[Warrior Ratio]", NULL, 16);
	buildingWarriorRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor warrior ratio scroll box", "", NULL, 20);
	buildingCherryLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor cherry label", "", "[Cherry]", NULL, static_cast<Sint32*>(NULL));
	buildingCherryScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor cherry scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingOrangeLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor orange label", "", "[Orange]", NULL, static_cast<Sint32*>(NULL));
	buildingOrangeScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor orange scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingPruneLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor prune label", "", "[Prune]", NULL, static_cast<Sint32*>(NULL));
	buildingPruneScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor prune scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingStoneLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor stone label", "", "[Stone]", NULL, static_cast<Sint32*>(NULL));
	buildingStoneScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor stone scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingBulletsLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor bullets label", "", "[Bullets]", NULL, static_cast<Sint32*>(NULL));
	buildingBulletsScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor bullets scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingMinimumLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor minimum level to flag label", "", "[Minimum Level To Flag]", NULL, 3);
	buildingMinimumLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor minimum level to flag scroll box", "update building", NULL, 3);
	buildingRadiusLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor range label", "", "[range]", NULL, static_cast<Sint32*>(NULL));
	buildingRadiusScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor range scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	addWidget(buildingInfoTitle);
	addWidget(buildingPicture);
	addWidget(buildingHPLabel);
	addWidget(buildingHPScrollBox);
	addWidget(buildingFoodQuantityLabel);
	addWidget(buildingFoodQuantityScrollBox);
	addWidget(buildingAssignedLabel);
	addWidget(buildingAssignedScrollBox);
	addWidget(buildingWorkerRatioLabel);
	addWidget(buildingWorkerRatioScrollBox);
	addWidget(buildingExplorerRatioLabel);
	addWidget(buildingExplorerRatioScrollBox);
	addWidget(buildingWarriorRatioLabel);
	addWidget(buildingWarriorRatioScrollBox);
	addWidget(buildingCherryLabel);
	addWidget(buildingCherryScrollBox);
	addWidget(buildingOrangeLabel);
	addWidget(buildingOrangeScrollBox);
	addWidget(buildingPruneLabel);
	addWidget(buildingPruneScrollBox);
	addWidget(buildingStoneLabel);
	addWidget(buildingStoneScrollBox);
	addWidget(buildingBulletsLabel);
	addWidget(buildingBulletsScrollBox);
	addWidget(buildingMinimumLevelLabel);
	addWidget(buildingMinimumLevelScrollBox);
	addWidget(buildingRadiusLabel);
	addWidget(buildingRadiusScrollBox);

	selectionName="";
	buildingLevel=0;
	brushType = NoBrush;
	enableOnlyGroup("building view");

	isDraggingMinimap=false;
	isDraggingZone=false;
	isDraggingTerrain=false;
	isDraggingDelete=false;
	isScrollDragging=false;
	isDraggingArea=false;
	isDraggingNoRessourceGrowthArea=false;

	lastPlacementX=-1;
	lastPlacementY=-1;

	menuScreen = NULL;
	scriptEditor=NULL;
	teamsEditor=NULL;
	showingMenuScreen=false;
	showingLoad=false;
	showingSave=false;
	showingScriptEditor=false;
	showingTeamsEditor=false;

	terrainType=TerrainSelector::NoTerrain;

	teamViewSelectorKeys.push_back("[human]");
	teamViewSelectorKeys.push_back("[ai]");


	placingUnit=NoUnit;
	placingUnitLevel=0;

	selectedUnitGID=NOGUID;
	selectedBuildingGID=NOGBID;

	areaName=NULL;
	isShowingAreaName=false;
	
	isFertilityOn=false;
}



MapEdit::~MapEdit()
{
	Toolkit::releaseSprite("data/gui/editor");
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		delete *i;
	}
}
