#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "../src/team/Team.h"
#include "../src/map/Map.h"
#include "../src/map/MapInternal.h"
#include "../src/building/Building.h"
#include "BuildingType.h"
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
void fruitIsNeverAClearingTarget() {
    Fixture f;
    auto* flag=f.building(20,20,0,"clearingflag");
    flag->unitStayRange=6;
    // Fruit IDs 5..7 used to index beyond the five clearing switches into
    // alignment padding. Vary only that padding to reproduce different heap
    // histories (including a fresh load) without changing any game state.
    auto* bytes=reinterpret_cast<unsigned char*>(flag);
    const auto begin=reinterpret_cast<unsigned char*>(flag->clearingResources)-bytes+BASIC_COUNT;
    const auto end=reinterpret_cast<unsigned char*>(&flag->minLevelToFlag)-bytes;
    REQUIRE(end>=begin);
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
        const int slot=flag->routeSlot(swim, BuildingRoute::Clearing);
        f.game.map.buildingGradient(flag,swim,BuildingRoute::Clearing);
        for(unsigned char padding:{0,1}) {
            std::fill(bytes+begin,bytes+end,padding);
            for(int resource:{WOOD,WHEAT,PAPYRUS,STONE,ALGA,CHERRY,ORANGE,PRUNE,NO_RES_TYPE}) {
                auto& tile=f.game.map.getResource(21,20);
                tile.type=resource;tile.amount=resource==NO_RES_TYPE?0:1;
                for(bool enabled:{false,true}) {
                    if(resource<BASIC_COUNT)flag->clearingResources[resource]=enabled;
                    f.game.map.updateGlobalGradient(flag,swim,BuildingRoute::Clearing);
                    f.game.map.finishBuildingGradient(flag,swim,BuildingRoute::Clearing);
                    const bool expected=resource<BASIC_COUNT && enabled;
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
	TEST_CASE("basic switches; all fruit; empty tiles; padding independence and swimming variants")
	{
		glob2test::HeadlessGlobals globals;
	    fruitIsNeverAClearingTarget();
	}
}
