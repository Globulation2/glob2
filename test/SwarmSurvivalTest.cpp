#define main originalCombatRegressionMain
#include "MaximaCombatIntegrationTest.cpp"
#undef main

namespace {
struct SwarmFixture : combat_regressions::Fixture {
    ::Building* swarm;
    SwarmFixture() {
        swarm=building(20,20,0,"swarm");
        game.teams[0]->createLists();
        swarm->resources[CORN]=swarm->type->resourceForOneUnit;
        swarm->ratio[0]=swarm->ratio[1]=swarm->ratio[2]=0;
        swarm->productionTimeout=150;
        assert(game.addUnit(40,40,1,WORKER,0,0,0,0));
    }
    void enclose(int resource) {
        for(int y=19;y<=20+swarm->type->height;++y)
            for(int x=19;x<=20+swarm->type->width;++x) {
                if(x>=20&&x<20+swarm->type->width&&y>=20&&y<20+swarm->type->height)continue;
                auto&r=game.map.getResource(x,y);r.type=resource;r.amount=1;
            }
    }
};
void enclosedSwarmCannotPreventLoss() {
    for(int resource:{WOOD,CORN}) {
        SwarmFixture f;f.enclose(resource);
        f.swarm->resources[CORN]*=2;
        f.swarm->locked[0]=f.swarm->locked[1]=false; // Stale accessible cache.
        f.game.teams[0]->syncStep();
        assert(!f.game.teams[0]->isAlive);
        f.game.teams[2]->syncStep();f.game.wonSyncStep();
        assert(f.game.teams[0]->hasLost && f.game.teams[1]->hasWon);
    }
}
void recoveryAndTemporaryBlockers() {
    SwarmFixture f;f.enclose(WOOD);
    f.game.map.getResource(20,19).clear();
    f.swarm->locked[0]=f.swarm->locked[1]=true; // Stale inaccessible cache.
    // An enemy may temporarily occupy the only exit; it can move away.
    auto*u=f.game.addUnit(20,19,1,WORKER,0,0,0,0);assert(u);
    int x,y,dx,dy;assert(!f.swarm->findGroundExit(&x,&y,&dx,&dy,false));
    f.game.teams[0]->syncStep();assert(f.game.teams[0]->isAlive);
    f.game.map.setGroundUnit(20,19,NOGUID);u->posX=42;u->posY=40;
    f.game.map.setGroundUnit(42,40,u->gid);
    f.swarm->ratio[WORKER]=1;f.swarm->productionTimeout=0;
    f.game.teams[0]->syncStep();assert(f.game.teams[0]->isAlive);
    assert(f.swarm->resources[CORN]==0); // Exactly one birth's food suffices.
    bool born=false;for(int i=0;i<Unit::MAX_COUNT;++i)if(f.game.teams[0]->myUnits[i])born=true;
    assert(born);
}
void livingWorkerStillKeepsTeamAlive() {
    SwarmFixture f;f.enclose(CORN);
    assert(f.game.addUnit(10,10,0,WORKER,0,0,0,0));
    f.game.teams[0]->syncStep();assert(f.game.teams[0]->isAlive);
}
}
int main() {
    GlobalContainer container;globalContainer=&container;container.runNoX=true;
    container.buildingsTypes.init();IntBuildingType::init();
    enclosedSwarmCannotPreventLoss();recoveryAndTemporaryBlockers();livingWorkerStillKeepsTeamAlive();
    std::cout<<"SwarmSurvivalTest: blocked exits, stale caches, temporary occupancy, exact birth food and surviving worker PASS\n";
}
