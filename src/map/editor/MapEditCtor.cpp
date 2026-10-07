// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <GAG.h>
#include "ExperimentalFeatures.h"
#include "TerrainExperiments.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include <InterfacePresentation.h>
#include "ScriptEditorScreen.h"
#include "Utilities.h"
#include <SDL3/SDL.h>


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
	Sprite::requestHighResolution(globalContainer->settings.highResolutionArtwork);
    const bool usePhone=GAGCore::phonePresentationRequested();
    if(usePhone && globalContainer->gfx->hasPortableRenderer()) phone=std::make_unique<PhoneEditor>(*this);
	doQuit=false;
	doFullQuit=false;
	doQuitAfterLoadSave=false;

	// default value;
	viewportX=0;
	viewportY=0;
	xSpeed=0;
	ySpeed=0;
	// screen center, not (0,0) -- see handleMapScroll()
	mouseX=globalContainer->gfx->getW()/2;
	mouseY=globalContainer->gfx->getH()/2;
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
	rebuildBuildingSelectors();
	building_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + RIGHT_MENU_OFFSET+decX, globalContainer->gfx->getH()-42-TeamColorSelector::HEIGHT, TeamColorSelector::WIDTH, TeamColorSelector::HEIGHT ), "building view", "building view team selector", "select active team");
	building_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 1", "switch to building level 1", 1, buildingLevel, true);
	building_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 2", "switch to building level 2", 2, buildingLevel, true);
	building_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 3", "switch to building level 3", 3, buildingLevel, true);
	addWidget(building_view_tcs);
	addWidget(building_view_level1);
	addWidget(building_view_level2);
	addWidget(building_view_level3);
	buildingLevelNextPage=new TextLabel(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX,globalContainer->gfx->getH()-36,32,32),"building view","building level page","next building level page","",true,">");
	addWidget(buildingLevelNextPage);
	// Farm areas are an experiment: the editor offers the brush only to a player
	// who has it switched on, since only their games will read the mask.
	const bool farmZone = globalContainer->settings.experiments.has(ExperimentId::FarmAreas);
	const int zoneCount = farmZone ? 4 : 3;
	const int panelLeft = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
	forbiddenZone = new ZoneSelector(*this, widgetRectangle(panelLeft+mapEditZoneButtonX(0, zoneCount), 216, 32, 32), "flag view", "forbidden zone", "select forbidden zone", ZoneSelector::ForbiddenZone);
	guardZone = new ZoneSelector(*this, widgetRectangle(panelLeft+mapEditZoneButtonX(1, zoneCount), 216, 32, 32), "flag view", "guard zone", "select guard zone", ZoneSelector::GuardingZone);
	clearingZone = new ZoneSelector(*this, widgetRectangle(panelLeft+mapEditZoneButtonX(2, zoneCount), 216, 32, 32), "flag view", "clearing zone", "select clearing zone", ZoneSelector::ClearingZone);
	if (farmZone)
		farmingZone = new ZoneSelector(*this, widgetRectangle(panelLeft+mapEditZoneButtonX(3, zoneCount), 216, 32, 32), "flag view", "farming zone", "select farm zone", ZoneSelector::FarmingZone);
	deleteButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 216+40, 112, 16), "flag view", "delete button", "select delete objects", "[delete]");
	zoneBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 216+65, BrushTool::WIDTH, BrushTool::HEIGHT), "flag view", "zone brush selector", "handle zone click", brush);
	worker = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 385, 38, 38), "flag view", "worker selector", "select worker", WORKER);
	explorer = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+48+decX, 385, 38, 38), "flag view", "explorer selector", "select explorer", EXPLORER);
	warrior = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+88+decX, 385, 38, 38), "flag view", "warrior selector", "select warrior", WARRIOR);
	flag_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + RIGHT_MENU_OFFSET+decX, globalContainer->gfx->getH()-42-TeamColorSelector::HEIGHT, TeamColorSelector::WIDTH, TeamColorSelector::HEIGHT ), "flag view", "flag view team selector", "select active team");
	flag_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 1", "select unit level 1", 1, placingUnitLevel);
	flag_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 2", "select unit level 2", 2, placingUnitLevel);
	flag_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+64+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 3", 3, placingUnitLevel);
	flag_view_level4 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+96+decX, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 4", 4, placingUnitLevel);
	addWidget(forbiddenZone);
	addWidget(guardZone);
	addWidget(clearingZone);
	if (farmingZone)
		addWidget(farmingZone);
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
    // One side-panel brush per enabled catalogue group that has exactly one member
    // (ice, or trail while path-terrain is off). Groups with several members are
    // chosen in the Terrain palette dialog, which lists every enabled type.
    for (unsigned g = 0; g < TERRAIN_GROUP_COUNT; ++g)
    {
        const auto group = TerrainGroup(g);
        if (!terrainGroupDefinition(group).paletteVisible || group == TerrainGroup::Water || group == TerrainGroup::Sand || group == TerrainGroup::Grass) continue;
        std::vector<::TerrainType> enabled;
        for (unsigned id = 0; id < TERRAIN_COUNT; ++id)
        {
            const auto type = static_cast<::TerrainType>(id);
            if (terrainGroup(type) != group || !game.map.terrainPresentation(type).editorSelectable) continue;
            const auto requirement = terrainExperiment(type);
            if (requirement && !globalContainer->settings.experiments.has(*requirement)) continue;
            enabled.push_back(type);
        }
        if (enabled.size() != 1) continue;
        const auto &presentation = game.map.terrainPresentation(enabled.front());
        const int slot = int(additionalTerrainSelectors.size());
        auto* selector = new TerrainSelector(*this,
            widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+32*(slot%4)+decX,
                286+38*(slot/4),32,32), "terrain view",
            std::string(presentation.name)+" selector", std::string("select ")+presentation.name,
            TerrainSelector::selectorFor(enabled.front()));
        additionalTerrainSelectors.push_back(selector);
        addWidget(selector);
    }
    const int terrainExtraRow = 38*((int(additionalTerrainSelectors.size())+3)/4);
	noResourceGrowthButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 294+terrainExtraRow, 112, 16), "terrain view", "no ressources growth button", "select no ressources growth", "[no ressources growth areas]");
	areasButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + 8+decX, 320+terrainExtraRow, 112, 16), "terrain view", "script areas button", "select change areas", "[Script Areas]");
	areaNumber = new NumberCycler(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 336+terrainExtraRow, 8, 16), "terrain view", "script area number selector", "update script area number", 9);
	areaNameLabel = new TextLabel(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+24+decX, 336+terrainExtraRow, 104, 16), "terrain view", "script area name label", "open area name", "", false, Toolkit::getStringTable()->getString("[Unnamed Area]"));
	terrainBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 362+terrainExtraRow, BrushTool::WIDTH, BrushTool::HEIGHT), "terrain view", "terrain brush selector", "handle terrain click", brush);
	showFertilityOverlay = new Checkbox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 466+terrainExtraRow, 128, 16), "terrain view", "fertility checkbox", "compute fertility", "[Fertility Map]", isFertilityOn);
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
	addWidget(noResourceGrowthButton);
	addWidget(areasButton);
	addWidget(areaNumber);
	addWidget(areaNameLabel);
	addWidget(terrainBrushSelector);
	addWidget(showFertilityOverlay);

	increaseTeams = new PlusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, 408, 32, 32), "teams view", "increase teams", "add team");
	decreaseTeams = new MinusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+40+decX, 408, 32, 32), "teams view", "decrease teams", "remove team");
	team_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + RIGHT_MENU_OFFSET+decX, 168, TeamColorSelector::WIDTH, TeamColorSelector::HEIGHT ), "teams view", "team view team selector", "select active team");
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
	unitBuildLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 348, 128, 16), "unit editor", "unit editor build level label", "", "[Build]", NULL, 3);
	unitBuildLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 364, 112, 16), "unit editor", "unit editor build level scroll box", "update unit build level", NULL, 3);
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
	static const char* resourceLabels[MaterialCount]={"[Wood]","[Food]","[Paper]","[Stone]","[Algae]","[Cherries]","[Oranges]","[Prunes]","[Gold]","[Metal]","[Glass]","[Fabric]"};
	for (int resource=0; resource<MaterialCount; ++resource)
	{
		const auto name="building resource "+std::to_string(resource);
		buildingResourceLabels[resource]=new FractionValueText(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,252,128,16),"building editor",name+" label","",resourceLabels[resource],nullptr,static_cast<Sint32*>(nullptr));
		buildingResourceControls[resource]=new ValueScrollBox(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,268,128,16),"building editor",name+" value","update building",nullptr,static_cast<Sint32*>(nullptr));
		addWidget(buildingResourceLabels[resource]); addWidget(buildingResourceControls[resource]);
	}
	buildingAssignedLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor assigned label", "", "[assigned]", NULL, 20);
	buildingAssignedScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor assigned scroll box", "", NULL, 20);
	buildingWorkerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor worker ratio label", "", "[Worker Ratio]", NULL, 16);
	buildingWorkerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor worker ratio scroll box", "", NULL, 20);
	buildingExplorerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor explorer ratio label", "", "[Explorer Ratio]", NULL, 16);
	buildingExplorerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor explorer ratio scroll box", "", NULL, 20);
	buildingWarriorRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor warrior ratio label", "", "[Warrior Ratio]", NULL, 16);
	buildingWarriorRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor warrior ratio scroll box", "", NULL, 20);
	buildingBulletsLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor bullets label", "", "[Bullets]", NULL, static_cast<Sint32*>(NULL));
	buildingBulletsScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor bullets scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	buildingMinimumLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor minimum level to flag label", "", "[Minimum Level To Flag]", NULL, 3);
	buildingMinimumLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor minimum level to flag scroll box", "update building", NULL, 3);
	buildingWorkerLevelLabel=new FractionValueText(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,252,128,16),"building editor","worker qualification label","","[Worker]",nullptr,NB_UNIT_LEVELS-1);
	buildingWorkerLevelScrollBox=new ValueScrollBox(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,268,128,16),"building editor","worker qualification value","update building",nullptr,NB_UNIT_LEVELS-1);
	buildingBombingLabel=new FractionValueText(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,252,128,16),"building editor","bombing requirement label","","[ground attack]",&buildingBombingRequirement,1);
	buildingBombingScrollBox=new ValueScrollBox(*this,widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX,268,128,16),"building editor","bombing requirement value","update building",&buildingBombingRequirement,1);
	addWidget(buildingWorkerLevelLabel); addWidget(buildingWorkerLevelScrollBox);
	addWidget(buildingBombingLabel); addWidget(buildingBombingScrollBox);
	buildingRadiusLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 252, 128, 16), "building editor", "building editor range label", "", "[range]", NULL, static_cast<Sint32*>(NULL));
	buildingRadiusScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+decX, 268, 128, 16), "building editor", "building editor range scroll box", "update building", NULL, static_cast<Sint32*>(NULL));
	addWidget(buildingInfoTitle);
	addWidget(buildingPicture);
	addWidget(buildingHPLabel);
	addWidget(buildingHPScrollBox);
	addWidget(buildingAssignedLabel);
	addWidget(buildingAssignedScrollBox);
	addWidget(buildingWorkerRatioLabel);
	addWidget(buildingWorkerRatioScrollBox);
	addWidget(buildingExplorerRatioLabel);
	addWidget(buildingExplorerRatioScrollBox);
	addWidget(buildingWarriorRatioLabel);
	addWidget(buildingWarriorRatioScrollBox);
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
	isLeftScrollDragging=false;
	isMiddleScrollDragging=false;
	isDraggingArea=false;
	isDraggingNoResourceGrowthArea=false;

	lastPlacementX=-1;
	lastPlacementY=-1;

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

	isShowingAreaName=false;
	
	isFertilityOn=false;
}



MapEdit::~MapEdit()
{
	Sprite::requestHighResolution(false);
	// The toolkit owns this shared cache entry; other staging editors may use it.
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		delete *i;
	}
}

void MapEdit::updateCamera()
{
    if (camera.tileX()!=viewportX) camera.originX=viewportX*32.0+camera.fractionX();
    if (camera.tileY()!=viewportY) camera.originY=viewportY*32.0+camera.fractionY();
    camera.resize(globalContainer->gfx->getW()-menuWidth(),globalContainer->gfx->getH(),game.map.getW()*32.0,game.map.getH()*32.0);
    if(!globalContainer->gfx->canDrawStretchedSprite()){camera.zoom=1;camera.offsetX=camera.offsetY=0;}
    viewportX=camera.tileX();viewportY=camera.tileY();
    game.map.displayViewportW=std::ceil(camera.visibleW()+camera.fractionX());
    game.map.displayViewportH=std::ceil(camera.visibleH()+camera.fractionY());
    view.mouseX=mapMouseX(mouseX);view.mouseY=mapMouseY(mouseY);
}
bool MapEdit::zoomMap(double steps,int x,int y)
{
    updateCamera();
    if (!globalContainer->gfx->canDrawStretchedSprite() || y<16 || !camera.contains(x,y)) return false;
    camera.wheel(steps,x,y);
    if(!globalContainer->gfx->canDrawStretchedSprite()){camera.zoom=1;camera.offsetX=camera.offsetY=0;}
    viewportX=camera.tileX();viewportY=camera.tileY();
    game.map.displayViewportW=std::ceil(camera.visibleW()+camera.fractionX());
    game.map.displayViewportH=std::ceil(camera.visibleH()+camera.fractionY());
    view.mouseX=mapMouseX(mouseX);view.mouseY=mapMouseY(mouseY);
    return true;
}
