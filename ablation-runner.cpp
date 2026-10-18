#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#define DOCTEST_CONFIG_IMPLEMENT
#include "EngineFixtures.h"
#include "UnitCatalog.h"
#include "gradient/GradientRuntime.h"
#include "Version.h"
#include <sstream>
#include <iostream>
TEST_CASE("ablation integration diagnostic") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true,.seed=4921});
    auto& game=world.game;
    auto catalog=UnitCatalog::loadFile(glob2test::fixture("unit-catalog/combinations.json").string());
    game.gameHeader.setUnitCatalog(catalog);game.configureBuildingCatalog();
    for(int t=0;t<game.teamsCount();++t)game.teams[t]->race.setCatalog(catalog);
    game.gameHeader.setHungerDisabled(true);
    for(int i=0;i<8;++i){auto* u=world.addUnit(*game.unitCatalog().find("ablation-"+std::to_string(i)),5+3*(i%4),5+3*(i/4));REQUIRE(u);u->receiveCarriedMaterial(WOOD,{});u->receiveCarriedMaterial(STONE,{});}
    for(int tick=0;tick<96;++tick){
        game.step(0);
        std::vector<Uint32> g,b,u;game.checkSum(&g,&b,&u,true);if(!g.empty())g.erase(g.begin());Uint32 hash=2166136261u;
        for(const auto* v:{&g,&b,&u})for(auto n:*v)hash=(hash^n)*16777619u;
        std::cout<<"TRACE "<<tick<<" "<<hash<<"\n";
        std::cout<<"GAME "<<tick;for(auto n:g)std::cout<<" "<<n;std::cout<<"\n";
        for(const auto& q:game.map.gradientRuntime->fieldRequests)std::cout<<"QUEUE "<<tick<<" "<<int(q.request.kind)<<" "<<int(q.request.team)<<" "<<int(q.request.material)<<" "<<int(q.request.swim)<<" "<<game.map.gradientDestination(q.slot)<<"\n";
        game.map.gradientRuntime->pipeline.visitLogical([&](auto* slot,unsigned remaining,bool superseded){std::cout<<"PENDING "<<tick<<" "<<game.map.gradientDestination(slot)<<" "<<remaining<<" "<<superseded<<"\n";});
        for(auto* unit:world.team->liveUnits){std::vector<Uint32> fields;unit->checkSum(&fields);std::cout<<"UNIT "<<tick<<" "<<unit->gid;for(auto n:fields)std::cout<<" "<<n;std::cout<<"\n";}
    }
}
int main(int argc,char**argv){SDL_SetMainReady();doctest::Context context(argc,argv);return context.run();}
