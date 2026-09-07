#include "../src/GlobalContainer.h"
#include "../src/Game.h"
#include "../src/gui/GameGUI.h"
#include "../src/team/Team.h"
#include "../src/map/Map.h"
#include "../src/building/Building.h"
#include "../src/game/entities/BuildingType.h"
#include "../src/building/IntBuildingType.h"
#define protected public
#include "../src/unit/Unit.h"
#undef protected
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <algorithm>
#include <iostream>

GlobalContainer* globalContainer = nullptr;
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
        assert(b);return b;
    }
};
}
namespace {
struct TrappedFixture : Fixture {
    ::Building* inn;
    Unit* unit;
    explicit TrappedFixture(int resource=WOOD) {
        inn=building(20,20,0);
        unit=game.addUnit(20,19,0,WORKER,0,0,0,0); assert(unit);
        game.map.setGroundUnit(20,19,NOGUID);
        unit->posX=20; unit->posY=20;
        unit->attachedBuilding=inn; unit->setTargetBuilding(inn);
        inn->unitsInside.push_back(unit);
        unit->activity=Unit::ACT_UPGRADING;
        unit->displacement=Unit::DIS_EXITING_BUILDING;
        unit->movement=Unit::MOV_INSIDE;
        unit->destinationPurpose=FEED;
        unit->insideTimeout=0; unit->medical=Unit::MED_HUNGRY;
        unit->needToRecheckMedical=true;
        unit->hungry=10; unit->hungriness=2; unit->hp=2;
        for(int y=19;y<=20+inn->type->height;++y)for(int x=19;x<=20+inn->type->width;++x) {
            if(x>=20&&x<20+inn->type->width&&y>=20&&y<20+inn->type->height)continue;
            auto&r=game.map.getResource(x,y);r.type=resource;r.amount=1;
        }
        int x,y,dx,dy;assert(!inn->findGroundExit(&x,&y,&dx,&dy,false));
    }
};
void starvationAndElimination() {
    for(int resource:{WOOD,CORN}) {
        TrappedFixture f(resource);
        Unit* survivor=f.game.addUnit(40,40,1,WORKER,0,0,0,0);assert(survivor);
        const int id=Unit::GIDtoID(f.unit->gid);
        f.unit->handleMedical();assert(f.unit->hungry==8 && !f.unit->isDead);
        assert(f.unit->attachedBuilding==f.inn && !f.inn->unitsInside.empty());
        for(int i=0;i<10&&!f.unit->isDead;++i)f.unit->handleMedical();
        assert(f.unit->isDead && f.unit->attachedBuilding==nullptr);
        assert(f.inn->unitsInside.empty());
        assert(f.game.map.getGroundUnit(40,40)==survivor->gid);
        for(int i=0;i<2;++i)for(int t=0;t<3;++t)f.game.teams[t]->syncStep();
        assert(f.game.teams[0]->myUnits[id]==nullptr);
        assert(!f.game.teams[0]->isAlive);
        f.game.wonSyncStep();
        assert(f.game.teams[0]->hasLost && f.game.teams[1]->hasWon);
    }
}
void rescueBeforeStarvation() {
    TrappedFixture f;
    f.unit->handleMedical();assert(f.unit->hungry==8);
    f.game.map.getResource(20,19).clear();
    f.unit->handleMovementExitingBuilding();
    assert(!f.unit->isDead && f.unit->attachedBuilding==nullptr);
    assert(f.unit->movement==Unit::MOV_EXITING_BUILDING && f.inn->unitsInside.empty());
}
void activeServiceStillSuspendsHunger() {
    for(auto state:{Unit::DIS_ENTERING_BUILDING,Unit::DIS_INSIDE}) {
        TrappedFixture f;f.unit->displacement=state;
        for(int i=0;i<10;++i)f.unit->handleMedical();
        assert(f.unit->hungry==10 && f.unit->hp==2 && !f.unit->isDead);
    }
}
void starvationContinuesAfterSave() {
    TrappedFixture f;f.unit->handleMedical();
    const int id=Unit::GIDtoID(f.unit->gid);
    auto*backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);f.game.save(&output,false,"trapped unit fixture");
    std::string bytes(backend->getBuffer(),backend->getPosition());
    Game restored(NULL);GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);assert(restored.load(&input));
    Unit*u=restored.teams[0]->myUnits[id];assert(u && u->hungry==8);
    u->handleMedical();assert(u->hungry==6 && !u->isDead);
}
}
int main() {
    GlobalContainer container;globalContainer=&container;container.runNoX=true;container.settings.rememberUnit=false;
    container.buildingsTypes.init();IntBuildingType::init();
    starvationAndElimination();rescueBeforeStarvation();activeServiceStillSuspendsHunger();starvationContinuesAfterSave();
    std::cout<<"TrappedUnitLifecycleTest: wood/wheat starvation, elimination/winner, rescue, active-service protection and save/load PASS\n";
}
