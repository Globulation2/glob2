// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "MapEdit.h"
#include "Race.h"
#include <filesystem>

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
            editor.game.map.getTile(0,31).*entry.second=2;
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
        editor.game.map.getTile(20,12).canResourcesGrow=0;
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

}
