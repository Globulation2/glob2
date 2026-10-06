#include "EngineFixtures.h"
#include <nlohmann/json.hpp>
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "../src/team/Team.h"
#include "../src/map/Map.h"
#include "../src/map/MapInternal.h"
#include "../src/building/Building.h"
#include "BuildingType.h"
#include "Unit.h"
#include "../src/building/IntBuildingType.h"
#include <algorithm>

namespace
{
namespace {
struct Fixture {
    GameGUI gui;
    Game& game=gui.game;
    Fixture() {
        game.map.setSize(6,6,GRASS);game.map.setGame(&game);
        for(int t=0;t<3;++t) {
            game.addTeam();game.teams[t]->race.loadDefault();
            game.teams[t]->playersMask=1u<<t;
        }
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)game.map.clearImmobileUnit(x,y);
    }
    Building* building(int x,int y,int team,const char* type="inn") {
        auto*b=game.addBuilding(x,y,globalContainer->buildingsTypes.getTypeNum(type,0,false),team);
        REQUIRE(b);return b;
    }
};
}
namespace {
void clearingUsesMaterialSwitchesAndResourceProperties() {
    Fixture f;
    auto* flag=f.building(20,20,0,"clearingflag");
    flag->unitStayRange=6;
    // Vary actual alignment padding after the fixed material switches. This
    // reproduces different heap histories without corrupting active switches,
    // including the newly-supported high material bits.
    auto* bytes=reinterpret_cast<unsigned char*>(flag);
    const auto begin=reinterpret_cast<unsigned char*>(flag->clearingMaterials)-bytes+MaterialCount;
    const auto end=reinterpret_cast<unsigned char*>(&flag->minLevelToFlag)-bytes;
    REQUIRE(end>=begin);
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
        const int slot=flag->routeSlot(swim, BuildingRoute::Clearing);
        f.game.map.buildingGradient(flag,swim,BuildingRoute::Clearing);
        for(unsigned char padding:{0,1}) {
            std::fill(bytes+begin,bytes+end,padding);
            for(unsigned resource=0;resource<=f.game.map.resourceRegistry().size();++resource) {
                const auto type=resource==f.game.map.resourceRegistry().size() ? NO_RES_TYPE : resource;
                auto tile=f.game.map.getResource(21,20);
                tile.type=type;tile.amount=type==NO_RES_TYPE?0:1;
                f.game.map.replaceResource(21,20,tile);
                for(unsigned material=0;material<MaterialCount;++material) for(bool enabled:{false,true}) {
                    std::fill_n(flag->clearingMaterials,MaterialCount,false);
                    flag->clearingMaterials[material]=enabled;
                    f.game.map.updateGlobalGradient(flag,swim,BuildingRoute::Clearing);
                    f.game.map.finishBuildingGradient(flag,swim,BuildingRoute::Clearing);
                    const bool expected=type!=NO_RES_TYPE && enabled && f.game.map.resourcePropertiesByIndex(type).clearable
                        && (f.game.map.resourcePropertiesByIndex(type).materialMask & materialBit(static_cast<MaterialId>(material)));
                    REQUIRE((flag->globalGradient[slot][21+20*64]==GRADIENT_AT_GOAL)==expected);
                }
            }
        }
    }
}
}
}

TEST_SUITE("ClearingFlagGradient")
{
	TEST_CASE("material switches; declarative clearing; empty tiles; padding independence and swimming variants")
	{
		glob2test::HeadlessGlobals globals;
	    clearingUsesMaterialSwitchesAndResourceProperties();
	}
}

TEST_SUITE("ClearingFlagGradient")
{
TEST_CASE("passable clearing sources respect forbidden paint buildings and immobile units")
{
    glob2test::HeadlessGlobals globals;
    Fixture fixture;
    auto& map=fixture.game.map;
    auto* flag=fixture.building(20,20,0,"clearingflag");
    auto* obstacle=fixture.building(30,30,0);
    flag->unitStayRange=6;
    std::fill_n(flag->clearingMaterials,MaterialCount,false);
    flag->clearingMaterials[materialIndex(MaterialId::Fabric)]=true;
    using Json=nlohmann::json;
    const auto cottonId=*map.resourceRegistry().find("cotton");
    auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(cottonId)];
    definition["key"]="test-passable-fabric";
    definition["properties"]["blocksGround"]=false;
    definition["properties"]["clearable"]=true;
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    const auto id=*map.resourceRegistry().find("test-passable-fabric");
    REQUIRE_FALSE(map.resourceRegistry().properties(id).blocksGround);
    REQUIRE(map.resourceRegistry().properties(id).clearable);
    const auto index=map.coordToIndex(21,20);
    map.replaceResource(index,Resource{static_cast<Uint16>(resourceIndex(id)),0,1,0});
    for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
    {
        const auto verify=[&](Uint16 expected) {
            map.buildingGradient(flag,swim,BuildingRoute::Clearing);
            map.updateGlobalGradient(flag,swim,BuildingRoute::Clearing);
            map.finishBuildingGradient(flag,swim,BuildingRoute::Clearing);
            CHECK(flag->globalGradient[flag->routeSlot(swim,BuildingRoute::Clearing)][index]==expected);
        };
        verify(GRADIENT_AT_GOAL);
        map.addForbidden(21,20,0);
        verify(GRADIENT_FORBIDDEN);
        map.removeForbidden(21,20,0);
        map.setBuilding(21,20,1,1,obstacle->gid);
        verify(GRADIENT_FORBIDDEN);
        map.setBuilding(21,20,1,1,NOGBID);
        map.markImmobileUnit(21,20,0);
        verify(GRADIENT_FORBIDDEN);
        map.clearImmobileUnit(21,20);
        verify(GRADIENT_AT_GOAL);
    }
}
}

TEST_CASE("empty persistent deposits remain clearing flag goals and worker targets" * doctest::test_suite("ClearingFlagGradient"))
{
    glob2test::HeadlessGlobals globals;
    for (const bool blocksGround : {false,true})
    {
        CAPTURE(blocksGround);
        Fixture f;
        auto& map=f.game.map;
        auto* flag=f.building(20,20,0,"clearingflag");
        flag->unitStayRange=6;
        std::fill_n(flag->clearingMaterials,MaterialCount,false);
        using Json=nlohmann::json;
        auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][0];
        definition["key"]="empty-clearable-gold";
        definition["properties"]["primaryMaterial"]="gold";
        definition["properties"]["blocksGround"]=blocksGround;
        definition["properties"]["persistsWhenEmpty"]=true;
        definition["properties"]["clearable"]=true;
        definition["yields"]={{"gold",{{"capacity",2},{"initial",1},{"consumption","one"}}}};
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
        const auto id=*map.resourceRegistry().find("empty-clearable-gold");
        for (const int y : {21,22})
        {
            map.setResource(21,y,id,0);
            map.setMaterialAmount(map.coordToIndex(21,y),MaterialId::Gold,0);
        }
        const auto index=map.coordToIndex(21,21);
        REQUIRE(map.getResource(index).type!=NO_RES_TYPE);
        REQUIRE(map.materialMaskAt(index)==0);
        CHECK_FALSE(map.isClearableResourceForMaterials(21,21,flag->clearingMaterials));
        flag->clearingMaterials[materialIndex(MaterialId::Gold)]=true;
        REQUIRE(map.isClearableResourceForMaterials(21,21,flag->clearingMaterials));
        for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
        {
            map.buildingGradient(flag,swim,BuildingRoute::Clearing);
            map.updateGlobalGradient(flag,swim,BuildingRoute::Clearing);
            map.finishBuildingGradient(flag,swim,BuildingRoute::Clearing);
            CHECK(flag->globalGradient[flag->routeSlot(swim,BuildingRoute::Clearing)][index]==GRADIENT_AT_GOAL);
        }
        auto* worker=f.game.addUnit(20,21,0,WORKER,0,0,0,0);
        REQUIRE(worker);
        worker->attachedBuilding=flag;
        worker->movement=Unit::MOV_RANDOM_GROUND;
        worker->handleMovementClearingResources();
        REQUIRE(worker->movement==Unit::MOV_HARVESTING);
        CHECK(worker->dx==1);
        CHECK(worker->dy==0);
        worker->handleMovementClearingResources();
        CHECK(map.getResource(index).type==NO_RES_TYPE);
        CHECK(f.game.teams[0]->stats.measurements.cleared[materialIndex(MaterialId::Gold)]==1);
    }
}
