// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "AIRules.h"
#include "AICastor.h"
#include "AICabino.h"
#include "AINicowar.h"
#include "AIMaxima.h"
#include "ai/cortex/CortexPolicy.h"
#include "ai/cortex/CortexQuery.h"
#include "GameRuleOverrides.h"
#include "Order.h"
#include "OrderValidation.h"
#include "Player.h"
#include "script/ScriptObservations.h"
#include "script/ScriptOrders.h"
#include <BinaryStream.h>
#include <StreamBackend.h>

namespace {
constexpr AI::ImplementationID controllers[]={AI::NUMBI,AI::CASTOR,AI::WARRUSH,
    AI::ECONO,AI::NICOWAR,AI::CORTEX,AI::CABINO,AI::MAXIMA};
void populate(glob2test::HeadlessGame& w)
{
    for(int team=0;team<2;++team)
    {
        const int x=team*32;
        auto* swarm=w.addBuilding("swarm",x+3,3,0,team);
        auto* inn=w.addBuilding("inn",x+10,3,0,team);
        for(auto* b:{swarm,inn})
        { b->resources[WHEAT]=b->type->maxResource[WHEAT]; b->update(); }
        for(int i=0;i<32;++i) w.addUnit(WORKER,x+2+i%20,12+i/20,team);
        for(int i=0;i<10;++i) w.addUnit(WARRIOR,x+3+i,15,team);
        w.game.teams[team]->startPosX=x+3; w.game.teams[team]->startPosY=3;
        w.game.teams[team]->startPosSet=Team::START_POS_FROM_UNIT;
        for(int y=20;y<28;++y) for(int xx=x+3;xx<x+29;++xx)
            w.game.map.setResource(xx,y,WHEAT,8);
        for(int xx=x+3;xx<x+29;++xx) {
            w.game.map.setResource(xx,30,WOOD,8);
            w.game.map.setResource(xx,31,STONE,8);
        }
        w.game.teams[team]->stats.step(w.game.teams[team]);
    }
    w.game.map.setMapDiscovered();
}
void checkOrder(Game& g, Order& o)
{
    if(g.gameHeader.isUnitUpgradesDisabled() && o.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto& c=static_cast<const OrderConstruction&>(o);
        auto* b=g.teams[0]->myBuildings[Building::GIDtoID(c.gid)];
        REQUIRE(b); CHECK(b->hp < b->getEffectiveMaxHp());
    }
    if(o.getOrderType()==ORDER_CREATE)
    {
        const auto& c=static_cast<const OrderCreate&>(o);
        const auto* type=globalContainer->buildingsTypes.get(c.typeNum);
        CHECK(AIRules::usefulBuilding(g.gameHeader,type->shortTypeNum));
    }
    if(g.gameHeader.isPeacefulModeEnabled() && o.getOrderType()==ORDER_MODIFY_SWARM)
        CHECK(static_cast<const OrderModifySwarm&>(o).ratio[WARRIOR]==0);
}
std::vector<Uint32> state(Game& g)
{
    std::vector<Uint32> s,b,u; g.checkSum(&s,&b,&u,true);
    s.erase(s.begin()); // loader records the save format
    s.insert(s.end(),b.begin(),b.end()); s.insert(s.end(),u.begin(),u.end());return s;
}
std::string tick(Game& g)
{
    auto o=g.players[0]->ai->getOrder(false);checkOrder(g,*o);
    std::string wire(1,char(o->getOrderType()));
    if(o->getDataLength()) wire.append(reinterpret_cast<const char*>(o->getData()),o->getDataLength());
    o->sender=0;g.executeOrder(o,0);g.syncStep(0);return wire;
}
}
TEST_SUITE("AIRules")
{
TEST_CASE("native controllers exclude disabled work and continue after reload")
{
    glob2test::HeadlessGlobals globals;
    for(auto id:controllers) for(int variant=0;variant<3;++variant)
    {
        CAPTURE(id);CAPTURE(variant);
        glob2test::HeadlessGame w(glob2test::GameOptions{.wDec=6,.hDec=6,.teams=2,
            .discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
        auto& g=w.game;
        g.gameHeader.setUnitUpgradesDisabled(true);
        g.gameHeader.setHungerDisabled(variant>0);
        g.gameHeader.setPeacefulModeEnabled(variant==2);
        g.gameHeader.setResourceGrowthDisabled(variant==2);
        populate(w);
        // Existing service buildings and starting levels must not create training investments.
        w.addBuilding("school",3,35,1);w.addBuilding("racetrack",10,35);
        w.addBuilding("hospital",17,35);w.addUnit(WARRIOR,20,12,0,2);
        if(variant==2) {
            for(auto* b:g.teams[0]->myBuildings) if(b) {b->resources[WHEAT]=0;b->update();}
            for(int y=20;y<28;++y) for(int x=3;x<29;++x) g.map.getResource(x,y).amount=0;
        }
        g.teams[0]->stats.step(g.teams[0]);g.players[0]->makeItAI(id);g.setWaitingOnMask(0);
        int decisions=0;
        for(int i=0;i<4096;++i) decisions+=static_cast<unsigned char>(tick(g)[0])!=ORDER_NULL;
        CHECK(decisions>0);
        CHECK(g.teams[0]->isAlive);
        CHECK(g.teams[0]->stats.measurements.trainingVisits[WORKER]==0);
        CHECK(g.teams[0]->stats.measurements.trainingVisits[WARRIOR]==0);
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(backend);g.save(&out,false,"AI custom rules");out.flush();
        const auto bytes=backend->takeContents();
        const auto before=state(g);
        std::vector<std::string> orders;std::vector<std::vector<Uint32>> traces;
        for(int i=0;i<64;++i){orders.push_back(tick(g));traces.push_back(state(g));}
        GameGUI restored(false);
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        REQUIRE(restored.game.load(&in));restored.game.setWaitingOnMask(0);
        CHECK(state(restored.game)==before);
        for(int i=0;i<64;++i){CHECK(tick(restored.game)==orders[i]);CHECK(state(restored.game)==traces[i]);}
    }
}
TEST_CASE("engine prevents training and upgrades while retaining repairs and starting levels")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& g=w.game;g.gameHeader.setUnitUpgradesDisabled(true);
    auto* school=w.addBuilding("school",4,4);
    auto* worker=w.addUnit(WORKER,10,10);
    auto* veteran=w.addUnit(WARRIOR,12,10,0,2);
    auto* inn=w.addBuilding("inn",16,16);
    CHECK(w.team->findBestUpgrade(worker)==nullptr);
    CHECK(veteran->level[ATTACK_STRENGTH]==2);
    CHECK(!inn->isHardSpaceForBuildingSite(Building::UPGRADE));
    OrderConstruction upgrade(inn->gid,2,2);
    CHECK(OrderValidation::validate(g,0,upgrade).verdict==OrderValidation::Verdict::Rejected);
    inn->launchConstruction(2,2);CHECK(inn->constructionResultState==Building::NO_CONSTRUCTION);
    Script::Observations obs(g,0);
    auto descriptor=Script::Value::object().set("type","construction").set("building",obs.building(*inn))
        .set("workers",2).set("futureWorkers",2);
    CHECK_THROWS(Script::order(g,0,descriptor));
    inn->hp-=10;
    CHECK(OrderValidation::validate(g,0,upgrade).verdict==OrderValidation::Verdict::Accepted);
    CHECK_NOTHROW(Script::order(g,0,descriptor));
    inn->launchConstruction(2,2);CHECK(inn->constructionResultState==Building::REPAIR);
    // A restored en-route trainee must be unsubscribed without affecting heal/feed visits.
    worker->activity=Unit::ACT_UPGRADING;worker->displacement=Unit::DIS_GOING_TO_BUILDING;
    worker->destinationPurpose=BUILD;worker->attachedBuilding=school;worker->setTargetBuilding(school);
    school->subscribeUnitForInside(worker);worker->handleActivity();
    CHECK(worker->activity!=Unit::ACT_UPGRADING);CHECK(worker->attachedBuilding==nullptr);
    worker->activity=Unit::ACT_UPGRADING;worker->displacement=Unit::DIS_INSIDE;
    worker->destinationPurpose=BUILD;worker->attachedBuilding=school;
    worker->insideTimeout=-100;worker->level[BUILD]=0;
    g.map.setGroundUnit(worker->posX,worker->posY,NOGUID);
    worker->posX=school->posX;worker->posY=school->posY;
    worker->setTargetBuilding(school);school->unitsInside.push_back(worker);
    worker->handleDisplacement();
    CHECK(worker->displacement==Unit::DIS_EXITING_BUILDING);CHECK(worker->level[BUILD]==0);
    CHECK(w.team->stats.measurements.trainingVisits[WORKER]==0);
    // Leave cleanup to the ordinary exiting path; the unit remains a valid subscriber.
    g.gameHeader.setHungerDisabled(true);worker->hungry=0;
    CHECK(!worker->isUnitHungry());
}
TEST_CASE("rule parser and script observations use effective match values")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.header=true});
    auto& h=w.game.gameHeader;
    applyGameRule(h,"noUpgrades=1");applyGameRule(h,"scarcity=3");
    applyGameRule(h,"suddenDeathTick=500");
    CHECK_THROWS_AS(applyGameRule(h,"scarcity=4"),std::invalid_argument);
    CHECK_THROWS_AS(applyGameRule(h,"missing=1"),std::invalid_argument);
    CHECK_THROWS_AS(applyGameRule(h,"noHunger="),std::invalid_argument);
    Script::Observations obs(w.game,0);auto r=obs.query("rules",{},{});
    CHECK(r.get("noUpgrades").number==1);CHECK(r.get("scarcity").number==3);
    CHECK(r.get("suddenDeathTick").number==500);
    r.set("noUpgrades",0);CHECK(h.isUnitUpgradesDisabled());
}
TEST_CASE("JavaScript profiles can inspect rules and issue useful orders")
{
    glob2test::HeadlessGlobals globals;
    for(unsigned profile:{1u,2u})
    {
        CAPTURE(profile);
        glob2test::HeadlessGame w(glob2test::GameOptions{.discovered=true,.loadDefaultRace=true,.header=true});
        w.game.gameHeader.setUnitUpgradesDisabled(true);
        w.addBuilding("swarm",4,4);
        const std::string source=profile==1
            ? "export function step(c,s){const r=c.game.rules();if(!r.noUpgrades)throw Error('rules');return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"
            : "export const metadata={apiVersion:2,name:'Rule-aware smoke'}; export function step(c){const r=c.game.rules();if(!r.noUpgrades)throw Error('rules');c.game.buildings({team:c.myTeam})[0].workers=3;}";
        w.game.gameHeader.setAIConfig(0,Script::config(source,profile));
        w.game.players[0]->makeItAI(AI::JAVASCRIPT);
        auto order=w.game.players[0]->ai->getOrder(false);
        CHECK(order->getOrderType()==ORDER_MODIFY_BUILDING);
    }
}
TEST_CASE("no growth farms harvest their finite seed rather than waiting forever")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.header=true});
    w.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas);
    w.game.map.setResource(5,5,WHEAT,1);w.game.map.addFarmArea(5,5,0);
    CHECK(!w.game.map.takeHarvest(4,5,1,0,WHEAT,w.team->me));
    w.game.gameHeader.setResourceGrowthDisabled(true);
    CHECK(w.game.map.takeHarvest(4,5,1,0,WHEAT,w.team->me));
    CHECK(w.game.map.getResource(5,5).amount==0);
}
TEST_CASE("Cortex excludes unavailable technology from scoring and feeding prerequisites")
{
    auto obs=Cortex::makeEmptyObservation();obs.valid=1;obs.upgradesDisabled=true;
    obs.hungerDisabled=true;obs.combatDisabled=true;obs.totalUnit=40;obs.workers=30;obs.freeWorkers=12;
    obs.starvingUnits=40;obs.needFood=40;
    Cortex::CortexPolicy policy;
    auto facts=policy.computeFacts(obs);
    CHECK(!facts.starving);CHECK(!facts.hungry);CHECK(!facts.combatPhase);CHECK(facts.growWarrior==0);
    CHECK(policy.scoreSchool(obs,facts).score==0);
    CHECK(policy.scoreFeedCapacity(obs,facts).score==0);
    CHECK(policy.scoreSchoolUpgrade(obs,facts).score==0);
}
}
