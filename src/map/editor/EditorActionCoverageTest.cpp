// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "MapEdit.h"
#include "Race.h"
#include "MapEditDialog.h"
#include "LoadSaveDialog.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <set>
#include "BuildingType.h"
#include "Unit.h"
#include <nlohmann/json.hpp>

namespace
{
void blank(MapEdit& editor)
{
    editor.game.map.setSize(5,5,GRASS);
    editor.game.map.setGame(&editor.game);
    editor.game.addTeam(); editor.game.teams[0]->race.loadDefault();
    for (int y=0; y<32; ++y) for (int x=0; x<32; ++x)
        editor.game.map.clearImmobileUnit(x,y);
    editor.viewportX=0; editor.viewportY=0;
    editor.updateCamera();
    editor.minimap.setGame(editor.game);
}
void cursor(MapEdit& editor,int x,int y)
{
    // MapEdit's action layer consumes the last event position in logical pixels.
    editor.mouseX=x*32+16; editor.mouseY=y*32+16;
}
}

TEST_SUITE("EditorActionCoverage")
{
    TEST_CASE("rerolling the terrain look changes the map seed and marks the map modified [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        editor.hasMapBeenModified=false;
        editor.game.map.setTerrainSeed(0);
        std::set<Uint32> seeds;
        for (int i=0; i<4; ++i)
        {
            editor.performAction("reroll terrain look");
            seeds.insert(editor.game.map.terrainSeed());
        }
        CHECK(seeds.size()>1);
        CHECK(editor.hasMapBeenModified);
    }
    TEST_CASE("custom catalog editor exposes resources mixed controls and long upgrade paths [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        auto snapshot=nlohmann::json::parse(editor.game.buildingsTypes.snapshotJson());
        auto& variants=snapshot["variants"];
        variants[3]["properties"]["zonable"]={1,1,1};
        variants[3]["properties"]["defaultUnitStayRange"]=5;
        variants[3]["properties"]["maxUnitStayRange"]=20;
        std::vector<int> capacity(MaterialSlotCount,0);
        std::fill_n(capacity.begin(),MaterialCount,30);
        variants[3]["properties"]["maxMaterial"]=capacity;
        variants[3]["semantics"]["assignmentLimit"]=40;
        variants[3]["semantics"]["production"]=variants[1]["semantics"]["production"];
        int previous=7;
        for (int depth=3; depth<5; ++depth)
        {
            auto site=variants[2], finished=variants[3];
            const int siteId=variants.size(), finishedId=siteId+1;
            const std::string family="fixture.refuge."+std::to_string(depth);
            site["id"]=siteId; site["key"]=family+".site";
            site["previous"]=variants[previous]["key"]; site["next"]=family+".finished";
            site["properties"]["type"]=family; site["properties"]["level"]=0; // Presentation tier is independent of path depth.
            site["semantics"]["placeable"]=false;
            finished["id"]=finishedId; finished["key"]=family+".finished";
            finished["previous"]=family+".site"; finished["next"]="";
            finished["properties"]["type"]=family; finished["properties"]["level"]=0;
            finished["semantics"]["placeable"]=false;
            variants[previous]["next"]=site["key"];
            variants.push_back(site); variants.push_back(finished); previous=finishedId;
        }
        const int overlayId=editor.game.buildingsTypes.getFinishedTypeNum("warflag");
        variants[overlayId]["properties"]["width"]=2;
        variants[overlayId]["properties"]["height"]=3;
        editor.game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        editor.game.configureBuildingCatalog(); editor.game.buildingsTypes.loadSprites();
        editor.rebuildBuildingSelectors();
        CHECK(editor.buildingLevelCount==5);
        editor.performAction("next building level page");
        CHECK(editor.buildingLevel==3);
        editor.building_view_level2->handleClick(8,8);
        CHECK(editor.buildingLevel==4);
        CHECK(editor.buildingSelectionType("inn.0.finished")==previous);
        editor.performAction("next building level page"); CHECK(editor.buildingLevel==0);
        auto* building=editor.game.addBuilding(4,4,3,0); REQUIRE(building);
        for (unsigned material=materialIndex(MaterialId::Gold);material<MaterialCount;++material)
            building->materials[material]=1; // Existing owned stock makes each configured row relevant.
        cursor(editor,4,4); editor.performAction("select map building");
        REQUIRE(editor.selectedBuildingGID==building->gid);
        CHECK(editor.buildingAssignedScrollBox->maximumValue()==40);
        for (int resource=0; resource<MaterialCount; ++resource)
        {
            CHECK(editor.buildingResourceControls[resource]->maximumValue()==30);
            editor.buildingResourceControls[resource]->setValue(resource+1);
            CHECK(building->materials[resource]==resource+1);
        }
        editor.buildingEditFirstRow=100; editor.layoutBuildingEditRows();
        CHECK(editor.buildingWorkerLevelScrollBox->enabled);
        CHECK(editor.buildingBombingScrollBox->enabled);
        editor.buildingWorkerLevelScrollBox->setValue(2);
        editor.buildingMinimumLevelScrollBox->setValue(1);
        editor.buildingBombingScrollBox->setValue(1);
        CHECK(building->minWorkerLevelToFlag==2);
        CHECK(building->minLevelToFlag==1);
        CHECK(building->explorersRequireBombing);
        editor.draw(SDL_GetTicks());
        globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/editor-composite.bmp");
        globals->gfx->nextFrame();
        auto* overlay=editor.game.addBuilding(31,31,overlayId,0); REQUIRE(overlay);
        cursor(editor,0,1); editor.performAction("select map building");
        CHECK(editor.selectedBuildingGID==overlay->gid);
    }

    TEST_CASE("editor material rows require configured capacity and natural or owned presence [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        const auto gold=materialIndex(MaterialId::Gold),metal=materialIndex(MaterialId::Metal);
        const int inn=editor.game.buildingsTypes.getFinishedTypeNum("inn");
        auto catalog=nlohmann::json::parse(editor.game.buildingsTypes.snapshotJson());
        catalog["variants"][inn]["properties"]["maxMaterial"][gold]=30;
        catalog["variants"][inn]["properties"]["maxMaterial"][metal]=0;
        editor.game.buildingsTypes.loadSnapshotJson(catalog.dump());
        editor.game.configureBuildingCatalog();editor.game.buildingsTypes.loadSprites();
        auto* selected=editor.game.addBuilding(4,4,inn,0);REQUIRE(selected);
        auto* supplier=editor.game.addBuilding(12,12,inn,0);REQUIRE(supplier);
        auto* worker=editor.game.addUnit(20,20,0,WORKER,0,0,0,0);REQUIRE(worker);
        auto& team=*editor.game.teams[0];
        team.teamMaterials[metal]=1; // Presence cannot expose an unconfigured slot.
        auto shown=[&](unsigned material) {
            return std::any_of(editor.buildingEditRows.begin(),editor.buildingEditRows.end(),
                [&](const auto& row){return row.second==editor.buildingResourceControls[material];});
        };
        auto select=[&] {cursor(editor,4,4);editor.performAction("select map building");};
        const auto deposit=*editor.game.map.resourceRegistry().find("gold-ore");
        for (int source=0;source<5;++source)
        {
            CAPTURE(source);
            select();CHECK_FALSE(shown(gold));CHECK_FALSE(shown(metal));
            if(source==0)editor.game.map.setResource(24,24,deposit,0);
            if(source==1)supplier->materials[gold]=1;
            if(source==2)worker->carriedMaterial=gold;
            if(source==3)team.teamMaterials[gold]=1;
            if(source==4)team.reservedTeamMaterials[gold]=1;
            select();CHECK(shown(gold));CHECK_FALSE(shown(metal));
            editor.game.map.replaceResource(24,24,Resource{});
            supplier->materials[gold]=0;worker->carriedMaterial=-1;
            team.teamMaterials[gold]=team.reservedTeamMaterials[gold]=0;
            select();CHECK_FALSE(shown(gold));CHECK_FALSE(shown(metal));
        }
    }

    TEST_CASE("editor selects and saves the sixteenth team [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        for (int team=1; team<Team::MAX_COUNT; ++team)
            editor.performAction("add team");
        REQUIRE(editor.game.teamsCount()==16);
        editor.performAction("add team");
        CHECK(editor.game.teamsCount()==16);
        REQUIRE(editor.team_view_tcs->area.width==TeamColorSelector::WIDTH);
        REQUIRE(editor.team_view_tcs->area.height==TeamColorSelector::HEIGHT);
        const int last=Team::MAX_COUNT-1;
        editor.performAction("select active team",
            (last%TeamColorSelector::COLUMNS)*TeamColorSelector::SWATCH_SIZE+1,
            (last/TeamColorSelector::COLUMNS)*TeamColorSelector::SWATCH_SIZE+1);
        REQUIRE(editor.team==last);
        editor.performAction("switch to teams view");
        editor.draw(SDL_GetTicks());
        globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/editor-sixteen.bmp");
        globals->gfx->nextFrame();
        cursor(editor,4,4);
        editor.performAction("select worker");
        editor.performAction("place unit");
        REQUIRE(editor.game.map.getGroundUnit(4,4)!=NOGUID);
        CHECK(Unit::GIDtoTeam(editor.game.map.getGroundUnit(4,4))==last);
        glob2test::TempDir scratch;
        const auto filename=(scratch.path/"sixteen.map").string();
        REQUIRE(editor.save(filename,"sixteen editor teams"));
        MapEdit restored;
        REQUIRE(restored.load(filename));
        CHECK(restored.game.teamsCount()==16);
        CHECK(Unit::GIDtoTeam(restored.game.map.getGroundUnit(4,4))==last);
    }

    TEST_CASE("unit placement selection and stat edits use the action dispatcher [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        for (const auto& entry : {std::pair{"select worker",WORKER},
                                  std::pair{"select explorer",EXPLORER},
                                  std::pair{"select warrior",WARRIOR}})
        {
            INFO(entry.first);
            const int x=3+entry.second*3, y=4;
            cursor(editor,x,y);
            editor.performAction(entry.first);
            editor.performAction("select unit level 3");
            CHECK(editor.placingUnitLevel==2);
            editor.hasMapBeenModified=false;
            editor.performAction("place unit");
            const auto gid=entry.second==EXPLORER ? editor.game.map.getAirUnit(x,y) : editor.game.map.getGroundUnit(x,y);
            REQUIRE(gid!=NOGUID);
            auto* unit=editor.game.teams[0]->myUnits[Unit::GIDtoID(gid)];
            REQUIRE(unit!=nullptr);
            CHECK(unit->typeNum==entry.second);
            CHECK(editor.hasMapBeenModified);
            editor.performAction("select map unit");
            REQUIRE(editor.view.selectedUnit==unit);
            for (const auto& update : {std::pair{"update unit walk level",WALK},
                                      std::pair{"update unit swim level",SWIM},
                                      std::pair{"update unit attack speed level",ATTACK_SPEED},
                                      std::pair{"update unit attack strength level",ATTACK_STRENGTH},
                                      std::pair{"update unit magic ground attack level",MAGIC_ATTACK_GROUND}})
            {
                unit->level[update.second]=1;
                editor.hasMapBeenModified=false;
                editor.performAction(update.first);
                CHECK(unit->performance[update.second]==unit->race->getUnitType(unit->typeNum,1)->performance[update.second]);
                CHECK(editor.hasMapBeenModified);
            }
            if (entry.second==WORKER)
            {
                unit->level[BUILD]=1;
                editor.performAction("update unit build level");
                CHECK(unit->level[HARVEST]==1);
                CHECK(unit->performance[HARVEST]==unit->race->getUnitType(WORKER,1)->performance[HARVEST]);
            }
            editor.performAction("unselect");
            CHECK(editor.view.selectedUnit==nullptr);
        }
        CHECK_FALSE(editor.performUnitAction("unknown action",0,0));
    }

    TEST_CASE("building selection exposes matching editor controls and save retains edits [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        int index=0;
        for (const char* name : {"swarm","inn","hospital","racetrack","swimmingpool",
                                 "barracks","school","defencetower","market","warflag","explorationflag","clearingflag","stonewall"})
        {
            INFO(std::string(name));
            const int type=globals->buildingsTypes.getTypeNum(name,0,false);
            REQUIRE(type>=0);
            const int x=2+(index%4)*6, y=2+(index/4)*6; ++index;
            auto* building=editor.game.addBuilding(x,y,type,0);
            REQUIRE(building!=nullptr);
            if (!building->type->isVirtual)
                editor.game.map.setBuilding(x,y,building->type->width,building->type->height,building->gid);
            cursor(editor,x,y);
            editor.performAction("select map building");
            CHECK(editor.selectedBuildingGID==building->gid);
            CHECK(editor.selectionMode==MapEdit::EditingBuilding);
            building->hp=building->type->hpMax/2;
            editor.hasMapBeenModified=false;
            editor.performAction("update building");
            CHECK(editor.hasMapBeenModified);
            editor.performAction("unselect");
        }
        glob2test::TempDir scratch;
        const auto filename=(scratch.path/"edited.map").string();
        REQUIRE(editor.save(filename,"coverage editor map"));
        CHECK_FALSE(editor.hasMapBeenModified);
        MapEdit restored;
        REQUIRE(restored.load(filename));
        for (int id=0; id<Building::MAX_COUNT; ++id)
        {
            const auto* expected=editor.game.teams[0]->myBuildings[id];
            const auto* actual=restored.game.teams[0]->myBuildings[id];
            REQUIRE(bool(actual)==bool(expected));
            if (actual) { CHECK(actual->typeNum==expected->typeNum); CHECK(actual->hp==expected->hp); }
        }
        CHECK_FALSE(restored.performBuildingAction("unknown action",0,0));
    }
    TEST_CASE("zone brush addition removal and seam wrapping preserve other team paint and saved masks [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor); editor.performAction("add team"); editor.team=0;
        for (const auto& entry : {std::pair{"select forbidden zone",&Tile::forbidden},
                                  std::pair{"select guard zone",&Tile::guardArea},
                                  std::pair{"select clearing zone",&Tile::clearArea}})
        {
            INFO(entry.first);
            editor.performAction(entry.first); editor.brush.setFigure(1); editor.brush.mode=BrushTool::MODE_ADD;
            editor.game.map.setAreaMask(editor.game.map.coordToIndex(0,31),entry.second,2);
            cursor(editor,31,31); editor.performAction("zone drag start"); editor.performAction("zone drag end");
            CHECK((editor.game.map.getTile(31,31).*entry.second & 1)!=0);
            CHECK(editor.game.map.getTile(0,31).*entry.second==3);
            CHECK((editor.game.map.getTile(31,0).*entry.second & 1)!=0);
            CHECK(editor.game.map.getTile(0,0).*entry.second==0);
            CHECK(editor.hasMapBeenModified);
            editor.brush.mode=BrushTool::MODE_DEL;
            editor.performAction("zone drag start"); editor.performAction("zone drag end");
            CHECK(editor.game.map.getTile(31,31).*entry.second==0);
            CHECK(editor.game.map.getTile(0,31).*entry.second==2);
        }
        editor.performAction("select guard zone"); editor.brush.mode=BrushTool::MODE_ADD;
        cursor(editor,6,6); editor.performAction("zone drag start"); editor.performAction("zone drag end");
        glob2test::TempDir scratch; const auto filename=(scratch.path/"zones.map").string();
        REQUIRE(editor.save(filename,"zone brushes")); MapEdit restored; REQUIRE(restored.load(filename));
        CHECK(restored.game.map.getTile(6,6).guardArea==editor.game.map.getTile(6,6).guardArea);
        CHECK(restored.game.map.getTile(0,31).guardArea==2);
    }

    TEST_CASE("farm brush is offered only with the farm-areas experiment and refuses ground that cannot grow [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        {
            MapEdit editor; blank(editor);
            CHECK(editor.farmingZone==nullptr);
            editor.performAction("select farm zone");
            CHECK(editor.brushType!=MapEdit::FarmAreaBrush);
        }
        globalContainer->settings.experiments.set(ExperimentId::FarmAreas);
        MapEdit editor; blank(editor); editor.performAction("add team"); editor.team=0;
        REQUIRE(editor.farmingZone!=nullptr);
        // Water down the left edge, so the grass beside it can grow wheat.
        for (int y=0; y<32; ++y) for (int x=0; x<8; ++x) editor.game.map.setUMatPos(x,y,WATER,1);
        editor.game.map.setResourcesGrow(20,12, 0);
        editor.performAction("select farm zone"); editor.brush.setFigure(1); editor.brush.mode=BrushTool::MODE_ADD;
        REQUIRE(editor.brushType==MapEdit::FarmAreaBrush);
        cursor(editor,12,12); editor.performAction("zone drag start"); editor.performAction("zone drag end");
        CHECK((editor.game.map.getTile(12,12).farmArea & 1)!=0);
        CHECK(editor.game.map.isFarmAreaInDisplayedView(12,12));
        cursor(editor,20,12); editor.performAction("zone drag start"); editor.performAction("zone drag end");
        CHECK(editor.game.map.getTile(20,12).farmArea==0);
        CHECK((editor.game.map.getTile(19,12).farmArea & 1)!=0);
        glob2test::TempDir scratch; const auto filename=(scratch.path/"farm.map").string();
        REQUIRE(editor.save(filename,"farm brush")); MapEdit restored; REQUIRE(restored.load(filename));
        CHECK(restored.game.map.getTile(12,12).farmArea==editor.game.map.getTile(12,12).farmArea);
        CHECK(restored.game.map.isFarmAreaInDisplayedView(12,12));
        editor.brush.mode=BrushTool::MODE_DEL;
        cursor(editor,12,12); editor.performAction("zone drag start"); editor.performAction("zone drag end");
        CHECK(editor.game.map.getTile(12,12).farmArea==0);
        // The flag view with its four zone buttons and the painted farm beside the water.
        editor.performAction("switch to flag view");
        editor.performAction("select farm zone");
        editor.draw(SDL_GetTicks());
        globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory()+"/editor-farm-brush.bmp");
        globals->gfx->nextFrame();
    }

    TEST_CASE("resource brushes add erase and retain painted wheat through editor save-load [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        for (const auto& entry : {std::pair{"select wheat",WHEAT},std::pair{"select trees",WOOD},
                                  std::pair{"select stone",STONE}})
        {
            INFO(entry.first);
            editor.performAction(entry.first); editor.brush.setFigure(0); editor.brush.mode=BrushTool::MODE_ADD;
            cursor(editor,6,6); editor.performAction("terrain drag start"); editor.performAction("terrain drag end");
            CHECK(editor.game.map.getResource(6,6).type==entry.second);
            editor.brush.mode=BrushTool::MODE_DEL;
            editor.performAction("terrain drag start"); editor.performAction("terrain drag end");
            CHECK(editor.game.map.getResource(6,6).type==NO_RES_TYPE);
        }
        editor.performAction("select wheat"); editor.brush.mode=BrushTool::MODE_ADD;
        cursor(editor,8,8); editor.performAction("terrain drag start"); editor.performAction("terrain drag end");
        glob2test::TempDir scratch; const auto filename=(scratch.path/"resources.map").string();
        REQUIRE(editor.save(filename,"resource brushes")); MapEdit restored; REQUIRE(restored.load(filename));
        CHECK(restored.game.map.getResource(8,8).type==WHEAT);
    }

    TEST_CASE("base terrain brush changes water sand and grass and saves the final terrain [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit editor; blank(editor);
        for(const auto& entry:{std::pair{"select water",WATER},std::pair{"select sand",SAND},std::pair{"select grass",GRASS}}) {
            INFO(entry.first); editor.performAction(entry.first); editor.brush.setFigure(6);
            cursor(editor,8,8); editor.performAction("terrain drag start"); editor.performAction("terrain drag end");
            CHECK(editor.game.map.getTerrainType(8,8)==entry.second); CHECK(editor.hasMapBeenModified);
        }
        glob2test::TempDir scratch; const auto filename=(scratch.path/"terrain.map").string();
        REQUIRE(editor.save(filename,"terrain brush")); MapEdit restored; REQUIRE(restored.load(filename));
        CHECK(restored.game.map.getTerrainType(8,8)==GRASS);
    }

    TEST_CASE("terrain experiments independently gate whole-cell brushes [display]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.width=1024,.height=768,
            .screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        MapEdit disabled; blank(disabled);
        CHECK(disabled.additionalTerrainSelectors.empty());
        disabled.performAction("select grass");
        disabled.performAction("select ice");
        CHECK(disabled.terrainType==TerrainSelector::Grass);
        disabled.performAction("select road");
        CHECK(disabled.terrainType==TerrainSelector::Grass);
        globals->settings.experiments.set(ExperimentId::IceTerrain,true);
        MapEdit editor;blank(editor);
        REQUIRE(editor.additionalTerrainSelectors.size()==1);
        editor.performAction("select ice");editor.brush.setFigure(0);
        cursor(editor,8,8);editor.performAction("terrain drag start");editor.performAction("terrain drag end");
        CHECK(editor.game.map.terrainTypeAt(8,8)==ICE);
        CHECK(editor.game.map.terrainTypeAt(7,8)==GRASS);
        editor.performAction("select road");
        CHECK(editor.terrainType==TerrainSelector::Ice);
        globals->settings.experiments.set(ExperimentId::TrailTerrain,true);
        editor.performAction("select road");
        cursor(editor,8,8);editor.performAction("terrain drag start");editor.performAction("terrain drag end");
        CHECK(editor.game.map.terrainTypeAt(8,8)==TRAIL);
        CHECK(editor.game.map.terrainTypeAt(7,8)==GRASS);
        glob2test::TempDir scratch;const auto filename=(scratch.path/"whole-cell-terrain.map").string();
        REQUIRE(editor.save(filename,"whole-cell terrain"));
        globals->settings.experiments.clear();
        MapEdit restored;REQUIRE(restored.load(filename));
        CHECK(restored.game.map.terrainTypeAt(8,8)==TRAIL);
    }

    TEST_CASE("registered terrain stamps batch invalidation and reject invalid selector IDs [display]")
    {
        glob2test::HeadlessGlobals globals({.display=true,.width=1024,.height=768,
            .screenFlags=GAGCore::GraphicContext::PORTABLEGPU});
        globals->settings.experiments.set(ExperimentId::IceTerrain,true);
        globals->settings.experiments.set(ExperimentId::TrailTerrain,true);
        MapEdit editor;blank(editor);
        REQUIRE(editor.additionalTerrainSelectors.size()==2);
        editor.performAction("select road");editor.brush.setFigure(6);
        const auto generation=editor.game.map.topologyGeneration;
        cursor(editor,8,8);editor.performAction("terrain drag start");editor.performAction("terrain drag end");
        CHECK(editor.game.map.topologyGeneration==generation+1);
        CHECK(editor.game.map.terrainTypeAt(8,8)==TRAIL);
        CHECK(editor.game.map.terrainTypeAt(7,8)==TRAIL);
        // Catalogue brushes are gated by their group's experiment, not listed in the side panel.
        editor.performAction("select boulders");
        CHECK(editor.terrainType==TerrainSelector::Trail);
        globals->settings.experiments.set(ExperimentId::ObstacleTerrain,true);
        editor.performAction("select boulders");
        CHECK(editor.terrainType==TerrainSelector::selectorFor(BOULDERS));
        editor.performAction("select hedge");
        CHECK(editor.terrainType==TerrainSelector::selectorFor(HEDGE));
        cursor(editor,20,20);editor.performAction("terrain drag start");editor.performAction("terrain drag end");
        CHECK(editor.game.map.terrainTypeAt(20,20)==HEDGE);
        CHECK(editor.game.map.requiredTerrainExperiments().has(ExperimentId::ObstacleTerrain));
        globals->settings.experiments.set(ExperimentId::ObstacleTerrain,false);
        editor.performAction("select road");
        for (auto invalid : {static_cast<TerrainSelector::TerrainType>(-1),
                static_cast<TerrainSelector::TerrainType>(TerrainSelector::RegisteredBegin+TERRAIN_COUNT),
                TerrainSelector::selectorFor(GRASS_SAND_SHORE),TerrainSelector::NoTerrain}) {
            editor.beginTerrainPlacement(invalid,MapEdit::TerrainPlacementMode::BaseTerrain);
            CHECK(editor.terrainType==TerrainSelector::Trail);
        }
        editor.beginTerrainPlacement(TerrainSelector::Trail,MapEdit::TerrainPlacementMode::Resource);
        CHECK(editor.terrainType==TerrainSelector::Trail);
        CHECK_FALSE(editor.brush.addRemoveEnabled);
        editor.beginTerrainPlacement(static_cast<TerrainSelector::TerrainType>(TerrainSelector::RegisteredBegin+GRASS),
            MapEdit::TerrainPlacementMode::BaseTerrain);
        CHECK(editor.terrainType==TerrainSelector::Grass);
        editor.performAction("select wheat");
        CHECK(editor.terrainType==TerrainSelector::Wheat);
        CHECK(editor.brush.addRemoveEnabled);
        editor.beginTerrainPlacement(TerrainSelector::Wheat,MapEdit::TerrainPlacementMode::BaseTerrain);
        CHECK(editor.terrainType==TerrainSelector::Wheat);
    }

	TEST_CASE("custom terrain imports palettes and saved maps work on desktop and phone "
			  "[display][artifacts]")
	{
		const char *previous = SDL_getenv_unsafe("GLOB2_MOBILE_UI");
		const std::string saved = previous ? previous : "";
		struct Restore
		{
			bool set;
			std::string value;
			~Restore()
			{
				if (set)
					glob2test::setEnv("GLOB2_MOBILE_UI", value.c_str());
				else
					glob2test::unsetEnv("GLOB2_MOBILE_UI");
			}
		} restore{previous != nullptr, saved};
		for (bool phone : {false, true})
		{
			glob2test::setEnv("GLOB2_MOBILE_UI", phone ? "1" : "0");
			glob2test::HeadlessGlobals globals(
				{.display = true,
				 .width = phone ? 800 : 1024,
				 .height = phone ? 480 : 768,
				 .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
			MapEdit editor;
			blank(editor);
			CHECK(editor.usesPhone() == phone);
			using Json = nlohmann::json;
			Json definitions = Json::array();
			for (unsigned i = 0; i < 40; ++i)
				definitions.push_back({{"key", "example:t" + std::to_string(i)},
									   {"name", "Terrain " + std::to_string(i)},
									   {"base", "grass"},
									   {"properties", {{"groundSpeedQ8", 192}}},
									   {"appearance", "sand"}});
			glob2test::TempDir scratch;
			const auto directory = scratch.path / "terrain";
			std::filesystem::create_directories(directory);
			GAGCore::Toolkit::getFileManager()->addDir(scratch.path.string());
			const auto file = directory / "runtime-review.json";
			glob2test::writeFile(file, "invalid JSON");
			// This picker must not advertise compressed files to the raw JSON loader.
			glob2test::writeFile(directory / "compressed-only.json.gz", "not offered");
			editor.performAction("import terrain definitions");
			REQUIRE(editor.loadSaveScreen);
			const auto files = editor.loadSaveScreen->filePresentation().files;
			CHECK(std::find(files.begin(), files.end(), "compressed-only") == files.end());
			const auto selected = std::find(files.begin(), files.end(), "runtime-review");
			REQUIRE(selected != files.end());
			editor.loadSaveScreen->selectPresentedFile(int(selected - files.begin()));
			const auto original = editor.game.map.frozenTerrainRegistry();
			editor.hasMapBeenModified = false;
			editor.loadSaveScreen->confirmPresentedFile();
			SDL_Event poll{};
			poll.type = SDL_EVENT_USER;
			editor.delegateMenu(poll);
			REQUIRE(editor.loadSaveScreen);
			CHECK(editor.loadSaveScreen->filePresentation().failed);
			CHECK_FALSE(editor.loadSaveScreen->filePresentation().status.empty());
			CHECK_FALSE(editor.loadSaveScreen->finished());
			CHECK(editor.game.map.frozenTerrainRegistry() == original);
			CHECK_FALSE(editor.hasMapBeenModified);

			// Retry the selected file through the same dialog after correcting it.
			glob2test::writeFile(file,
								 Json{{"schemaVersion", 1}, {"terrains", definitions}}.dump());
			editor.loadSaveScreen->confirmPresentedFile();
			editor.delegateMenu(poll);
			CHECK_FALSE(editor.loadSaveScreen);
			CHECK(editor.game.map.terrainRegistry().size() == 40 + TERRAIN_COUNT);
			CHECK(editor.hasMapBeenModified);
			REQUIRE(editor.terrainPalette);
			editor.draw(SDL_GetTicks());
			globals->gfx->printScreen(
				glob2test::artifactDirFromWorkingDirectory() +
				(phone ? "/terrain-palette-phone.bmp" : "/terrain-palette-desktop.bmp"));
			globals->gfx->nextFrame();

			// Select a type beyond the first viewport through the real scroll host.
			const std::string key = "terrain/example:t9";
			auto &host = editor.terrainPalette->host();
			host.scrollIntoView(key);
			host.layoutIfNeeded();
			const auto bounds = host.bounds(key);
			REQUIRE(bounds.w > 0);
			REQUIRE(bounds.h > 0);
			host.tapAt({bounds.x + bounds.w / 2, bounds.y + bounds.h / 2});
			CHECK(editor.terrainPalette->finished());
			editor.delegateMenu(poll);
			CHECK_FALSE(editor.terrainPalette);
			const auto type = *editor.game.map.terrainRegistry().find("example:t9");
			CHECK(editor.terrainType == TerrainSelector::selectorFor(type));
			cursor(editor, 8, 8);
			editor.performAction("terrain drag start");
			editor.performAction("terrain drag end");
			REQUIRE(editor.game.map.terrainTypeAt(8, 8) == type);

			editor.performAction("open terrain palette");
			SDL_Event escape{};
			escape.type = SDL_EVENT_KEY_DOWN;
			escape.key.key = SDLK_ESCAPE;
			editor.processEvent(escape);
			CHECK_FALSE(editor.terrainPalette);
			const auto map = (scratch.path / "custom.map").string();
			REQUIRE(editor.save(map, "Custom terrain"));
			std::filesystem::remove(file);
			MapEdit loaded;
			REQUIRE(loaded.load(map));
			CHECK(loaded.game.map.terrainTypeAt(8, 8) == type);
			CHECK(loaded.game.map.terrainProperties(type).groundSpeedQ8 == 192);
			loaded.draw(SDL_GetTicks());
			globals->gfx->printScreen(
				glob2test::artifactDirFromWorkingDirectory() +
				(phone ? "/terrain-map-phone.bmp" : "/terrain-map-desktop.bmp"));
			globals->gfx->nextFrame();
		}
	}
}
