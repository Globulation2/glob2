// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "AIRules.h"
#include "AIRuleOrders.h"
#include "AIStateSerialization.h"
#include "Marshaling.h"
#include "ai/cortex/AICortex.h"
#include "AICastor.h"
#include "AICabino.h"
#include "AINicowar.h"
#include "AIMaxima.h"
#include "AIMaximaFoodSupply.h"
#include "ai/cortex/CortexPolicy.h"
#include "ai/cortex/CortexQuery.h"
#include "GameRuleOverrides.h"
#include "Order.h"
#include "RessourceType.h"
#include "ReplayReader.h"
#include "Version.h"
#include <FileManager.h>
#include <cstdlib>
#include "OrderValidation.h"
#include "Player.h"
#include "scripting/javascript/ScriptObservations.h"
#include "scripting/javascript/ScriptOrders.h"
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
            {w.game.map.setResource(xx,y,WHEAT,0);w.game.map.getResource(xx,y).amount=globalContainer->resourcesTypes.get(WHEAT)->sizesCount;}
        for(int xx=x+3;xx<x+29;++xx) {
            w.game.map.setResource(xx,30,WOOD,0);w.game.map.getResource(xx,30).amount=globalContainer->resourcesTypes.get(WOOD)->sizesCount;
            w.game.map.setResource(xx,31,STONE,0);w.game.map.getResource(xx,31).amount=globalContainer->resourcesTypes.get(STONE)->sizesCount;
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
        CHECK(AIRules::usefulBuilding(g,c.typeNum));
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
TEST_CASE("disabled training preserves independent services of mixed providers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto& game=world.game;
    game.gameHeader.setUnitUpgradesDisabled(true);
    const int school=game.buildingsTypes.getTypeNum("school",0,false);
    const int site=game.buildingsTypes.getTypeNum("school",0,true);
    REQUIRE(school>=0); REQUIRE(site>=0);
    CHECK_FALSE(AIRules::usefulBuilding(game,site));
    CHECK_FALSE(AIRules::usefulWithoutTraining(game,school));
    auto& type=*game.buildingsTypes.get(school);
    type.semantics.healing.enabled=true;
    type.semantics.healing.unitMask=1u<<WORKER;
    game.configureBuildingCatalog();
    CHECK(AIRules::trainingBuilding(type));
    CHECK(AIRules::usefulWithoutTraining(game,school));
    CHECK(AIRules::usefulBuilding(game,site));
    OrderCreate create(0,4,4,site,1,1);
    CHECK(AIRules::permittedQueuedOrder(game,create));
}
TEST_CASE("construction staffing uses separate site and completed capacities")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto& game=world.game;
    const int site=game.buildingsTypes.getTypeNum("hospital",0,true);
    const auto order=AIRules::createOrder(game,0,2,2,site,100,100);
    const auto& create=static_cast<const OrderCreate&>(*order);
    CHECK(create.unitWorking==game.buildingsTypes.get(site)->semantics.assignmentLimit);
    CHECK(create.unitWorkingFuture==0);
    auto* building=world.addBuilding("hospital",2,2);
    const auto upgrade=AIRules::constructionOrder(game,*building,100,100);
    const auto& construction=static_cast<const OrderConstruction&>(*upgrade);
    CHECK(construction.unitWorking==game.buildingsTypes.get(building->type->nextLevel)->semantics.assignmentLimit);
    CHECK(construction.unitWorkingFuture==0);
    // A shared repair site still restores this hospital, not the inn its
    // ordinary forward link names. Completed staffing must remain zero.
    building->type->prevLevel=game.buildingsTypes.getTypeNum("inn",0,true);
    building->hp=building->getEffectiveMaxHp()-1;
    const auto repair=AIRules::constructionOrder(game,*building,7,7);
    CHECK(static_cast<const OrderConstruction&>(*repair).unitWorking==7);
    CHECK(static_cast<const OrderConstruction&>(*repair).unitWorkingFuture==0);
    OrderConstruction imported(building->gid,7,7);
    AIStateSerialization::normalizeLegacyOrderStaffing(game,imported,135);
    CHECK(imported.unitWorking==7);CHECK(imported.unitWorkingFuture==0);
}
TEST_CASE("legacy queued staffing imports explicit 135 wire without weakening modern validation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;
    const int site=game.buildingsTypes.getTypeNum("hospital",0,true);
    std::array<Uint8,29> wire{}; wire[0]=ORDER_CREATE;
    addSint32(wire.data()+1,0,0);addSint32(wire.data()+1,20,4);addSint32(wire.data()+1,20,8);
    addSint32(wire.data()+1,site,12);addSint32(wire.data()+1,7,16);addSint32(wire.data()+1,1,20);
    addSint32(wire.data()+1,ORDER_CREATE_NO_FLAG_RADIUS,24);
    auto legacy=Order::getOrder(wire.data(),wire.size(),135);REQUIRE(legacy);
    CHECK(OrderValidation::validate(game,0,*legacy).verdict==OrderValidation::Verdict::Rejected);
    legacy->sender=17;
    AIStateSerialization::normalizeLegacyOrderStaffing(game,*legacy,135);
    const auto& created=static_cast<const OrderCreate&>(*legacy);
    CHECK(created.typeNum==site);CHECK(created.unitWorking==7);CHECK(created.unitWorkingFuture==0);CHECK(legacy->sender==17);
    CHECK(OrderValidation::validate(game,0,*legacy).verdict==OrderValidation::Verdict::Accepted);
    auto modern=Order::getOrder(wire.data(),wire.size(),FILE_FORMAT_VERSION_BUILDING_CATALOG);REQUIRE(modern);
    AIStateSerialization::normalizeLegacyOrderStaffing(game,*modern,FILE_FORMAT_VERSION_BUILDING_CATALOG);
    CHECK(static_cast<const OrderCreate&>(*modern).unitWorkingFuture==1);
    CHECK(OrderValidation::validate(game,0,*modern).verdict==OrderValidation::Verdict::Rejected);
    addSint32(wire.data()+1,-1,16);
    auto invalid=Order::getOrder(wire.data(),wire.size(),135);REQUIRE(invalid);
    AIStateSerialization::normalizeLegacyOrderStaffing(game,*invalid,135);
    CHECK(static_cast<const OrderCreate&>(*invalid).unitWorking==-1);
    CHECK(OrderValidation::validate(game,0,*invalid).verdict==OrderValidation::Verdict::Rejected);
    addSint32(wire.data()+1,MAX_BUILDING_WORKER_REQUEST+1,16);
    auto oversized=Order::getOrder(wire.data(),wire.size(),135);REQUIRE(oversized);
    AIStateSerialization::normalizeLegacyOrderStaffing(game,*oversized,135);
    CHECK(static_cast<const OrderCreate&>(*oversized).unitWorking==MAX_BUILDING_WORKER_REQUEST+1);
    CHECK(static_cast<const OrderCreate&>(*oversized).unitWorkingFuture==1);
    CHECK(OrderValidation::validate(game,0,*oversized).verdict==OrderValidation::Verdict::Rejected);

    auto* hospital=world.addBuilding("hospital",2,2);
    auto* worker=world.addUnit(WORKER,10,10);worker->constructionLevel=1;
    std::array<Uint8,11> upgradeWire{};upgradeWire[0]=ORDER_CONSTRUCTION;
    addUint16(upgradeWire.data()+1,hospital->gid,0);
    addUint32(upgradeWire.data()+1,7,2);addUint32(upgradeWire.data()+1,1,6);
    auto upgrade=Order::getOrder(upgradeWire.data(),upgradeWire.size(),135);REQUIRE(upgrade);
    AIStateSerialization::normalizeLegacyOrderStaffing(game,*upgrade,135);
    CHECK(static_cast<const OrderConstruction&>(*upgrade).gid==hospital->gid);
    CHECK(static_cast<const OrderConstruction&>(*upgrade).unitWorking==7);
    CHECK(static_cast<const OrderConstruction&>(*upgrade).unitWorkingFuture==0);
    CHECK(OrderValidation::validate(game,0,*upgrade).verdict==OrderValidation::Verdict::Accepted);
}
TEST_CASE("Cabino reservations match unit class and qualification instead of building level")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true});
    Player player;player.setTeam(world.game.teams[0]);
    Cabino::AICabino ai(&player);
    struct Reservations:Cabino::DistributedUnitManager {
        using Cabino::DistributedUnitManager::DistributedUnitManager;
        using Cabino::DistributedUnitManager::getNeededUnits;
        bool perform(unsigned)override{return false;}
        unsigned numberOfTicks()const override{return 1;}
    } reservations(ai);
    auto* building=world.addBuilding("inn",2,2);
    building->maxUnitWorking=7;
    REQUIRE(reservations.request("construction",WORKER,BUILD,2,4,building->gid));
    CHECK(reservations.getNeededUnits(WORKER,BUILD,0,false)==0);
    CHECK(reservations.getNeededUnits(WARRIOR,BUILD,1,false)==0);
    CHECK(reservations.getNeededUnits(WORKER,BUILD,1,false)==4);
    auto* worker=world.addUnit(WORKER,10,10);worker->constructionLevel=1;
    auto* unqualified=world.addUnit(WORKER,11,10);unqualified->constructionLevel=0;
    auto* warrior=world.addUnit(WARRIOR,12,10);warrior->constructionLevel=1;
    building->unitsWorking={worker,unqualified,warrior};
    CHECK(reservations.getNeededUnits(WORKER,BUILD,1,false)==3);
    CHECK(reservations.getNeededUnits(WORKER,BUILD,2,true)==3);
    building->unitsWorking.clear();
}
TEST_CASE("retained tournament replay contains no unavailable orders")
{
    const char* path=std::getenv("GLOB2_RULE_REPLAY");
    if(!path) return; // Optional retained evidence; ordinary fixtures run below.
    glob2test::HeadlessGlobals globals;
    GameGUI gui(false);auto& g=gui.game;
    auto* stream=new GAGCore::BinaryInputStream(globalContainer->fileManager->openInflatingInputStreamBackend(path));
    REQUIRE(gui.load(stream));
    glob2test::BoundGameRandom random(g);
    ReplayReader reader;REQUIRE(reader.loadReplay(stream,false));
    g.setWaitingOnMask(0);
    while(!reader.isFinished())
    {
        while(reader.hasMoreOrdersThisStep())
        {
            auto order=reader.retrieveOrder();
            CAPTURE(g.stepCounter);CAPTURE(order->sender);CAPTURE(order->getOrderType());
            // Repair orders can outlive their damage while queued. The live
            // selection audit covers construction; replay checks the other gates.
            if(order->getOrderType()!=ORDER_CONSTRUCTION)
                CHECK(AIRules::permittedQueuedOrder(g,*order));
            g.executeOrder(order,0);
        }
        g.syncStep(0);reader.advanceStep();
    }
    for(int team=0;team<g.mapHeader.getNumberOfTeams();++team)
        if(g.gameHeader.isUnitUpgradesDisabled()) {
            CHECK(g.teams[team]->stats.measurements.trainingVisits[WORKER]==0);
            CHECK(g.teams[team]->stats.measurements.trainingVisits[WARRIOR]==0);
        }
}
TEST_CASE("native controllers exclude disabled work and continue after reload [slow]")
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
        auto* school=w.addBuilding("school",3,35,1);school->maxUnitWorking=5;w.addBuilding("racetrack",10,35);
        w.addBuilding("hospital",17,35);w.addUnit(WARRIOR,25,12,0,2);
        if(variant==2) {
            for(int slot=0;slot<Building::MAX_COUNT;++slot) if(auto* b=g.teams[0]->myBuildings[slot]) {b->resources[WHEAT]=0;b->update();}
            for(int y=20;y<28;++y) for(int x=3;x<29;++x) g.map.setNoResource(x,y,0);
        }
        g.teams[0]->stats.step(g.teams[0]);g.players[0]->makeItAI(id);g.setWaitingOnMask(0);
        int decisions=0;
        for(int i=0;i<4096;++i) decisions+=static_cast<unsigned char>(tick(g)[0])!=ORDER_NULL;
        CHECK(decisions>0);
        CHECK(g.teams[0]->isAlive);
        CHECK(school->maxUnitWorking==0);
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
        in.seekFromStart(0);
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
TEST_CASE("restored controller queues discard unavailable work and release prerequisites")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& g=w.game;g.gameHeader.setUnitUpgradesDisabled(true);
    g.gameHeader.setHungerDisabled(true);g.gameHeader.setPeacefulModeEnabled(true);
    auto* inn=w.addBuilding("inn",4,4);
    OrderConstruction upgrade(inn->gid,2,2);
    CHECK(!AIRules::permittedQueuedOrder(g,upgrade));
    inn->hp-=1;CHECK(AIRules::permittedQueuedOrder(g,upgrade));inn->hp+=1;
    const int school=globalContainer->buildingsTypes.getTypeNum("school",0,true);
    OrderCreate training(0,20,20,school,2,2);
    CHECK(!AIRules::permittedQueuedOrder(g,training));
    AIMaximaRuntime::Context maxima(g.players[0]);
    auto& register_=maxima.get_building_register();register_.initiate();
    const auto id=register_.found().begin()->first;
    AIMaximaRuntime::Management::UpgradeRepair savedUpgrade(id);
    savedUpgrade.modify(maxima);
    CHECK(!register_.is_building_upgrading(id));
    CHECK(maxima.orders.empty());
    AICortex cortex(g.players[0]);
    cortex.orderQueue.push(std::make_shared<OrderConstruction>(inn->gid,2,2));
    CHECK(cortex.getOrder()->getOrderType()==ORDER_NULL);CHECK(cortex.orderQueue.empty());
    NewNicowar nicowar;
    AISharedRuntime::Runtime runtime(nullptr,g.players[0]);
    nicowar.placement_queue.push_back(NewNicowar::RegularSchool);
    nicowar.construction_queue.push_back(NewNicowar::RegularInn);
    nicowar.buildings_under_construction_per_type[NewNicowar::RegularInn]=1;
    nicowar.order_buildings(runtime);
    CHECK(nicowar.placement_queue.empty());CHECK(nicowar.construction_queue.empty());
    CHECK(nicowar.buildings_under_construction_per_type[NewNicowar::RegularInn]==0);
}
TEST_CASE("Cabino migrates legacy warrior reservations only when training is disabled")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.wDec=6,.hDec=6,.teams=2,
        .loadDefaultRace=true,.header=true});
    auto* flag=w.addBuilding("warflag",4,4);
    Cabino::AICabino old(w.game.players[0]);
    auto* manager=static_cast<Cabino::DistributedUnitManager*>(old.getUnitModule());
    auto& record=manager->module_records["legacy"];
    record.requested[WARRIOR][ATTACK_STRENGTH][2]=7;
    record.reservedUnits[WARRIOR][ATTACK_STRENGTH][2]=5;
    record.usingUnits[WARRIOR][ATTACK_STRENGTH][2]=3;
    manager->buildings[flag->gid]={"legacy",unsigned(flag->posX),unsigned(flag->posY),
        unsigned(flag->type->shortTypeNum),unsigned(flag->type->level),ATTACK_STRENGTH,WARRIOR,2,3};
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);manager->save(&out);out.flush();
    const auto bytes=backend->takeContents();
    for(bool disabled:{false,true})
    {
        w.game.gameHeader.setUnitUpgradesDisabled(disabled);
        Cabino::AICabino restored(w.game.players[0]);
        auto* loaded=static_cast<Cabino::DistributedUnitManager*>(restored.getUnitModule());
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        in.seekFromStart(0);
        REQUIRE(loaded->load(&in,w.game.players[0],VERSION_MINOR));
        const unsigned bucket=disabled?0:2;
        auto& migrated=loaded->module_records["legacy"];
        CHECK(migrated.requested[WARRIOR][ATTACK_STRENGTH][bucket]==7);
        CHECK(migrated.reservedUnits[WARRIOR][ATTACK_STRENGTH][bucket]==5);
        CHECK(migrated.usingUnits[WARRIOR][ATTACK_STRENGTH][bucket]==3);
        CHECK(migrated.reservedUnits[WARRIOR][ATTACK_STRENGTH][disabled?2:0]==0);
        CHECK(loaded->buildings[flag->gid].minimum_level==bucket);
        // Releasing both saved claims must subtract from their migrated bucket,
        // rather than wrapping an unsigned zero and starving later army requests.
        loaded->unreserve("legacy",WARRIOR,ATTACK_STRENGTH,3,5);
        REQUIRE(loaded->request("legacy",WARRIOR,ATTACK_STRENGTH,3,0,flag->gid));
        CHECK(migrated.reservedUnits[WARRIOR][ATTACK_STRENGTH][bucket]==0);
        CHECK(migrated.usingUnits[WARRIOR][ATTACK_STRENGTH][bucket]==0);
        CHECK(loaded->buildings.count(flag->gid)==0);
    }
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
            ? "export function step(c,s){const r=c.game.rules();if(!r.noUpgrades)throw Error('rules');try{r.noUpgrades=0;}catch(e){}if(r.noUpgrades!==1)throw Error('mutable rules');return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"
            : "export function metadata(){return {apiVersion:2,name:'Rule-aware smoke'};} export function step(c){const r=c.game.rules();if(!r.noUpgrades)throw Error('rules');try{r.noUpgrades=0;}catch(e){}if(r.noUpgrades!==1)throw Error('mutable rules');c.game.buildings({team:c.myTeam})[0].workers=3;}";
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
    w.game.map.setResource(5,5,WHEAT,0);w.game.map.getResource(5,5).amount=1;w.game.map.addFarmArea(5,5,0);
    CHECK(!w.game.map.takeHarvest(4,5,1,0,WHEAT,w.team->me));
    w.game.gameHeader.setResourceGrowthDisabled(true);
    CHECK(w.game.map.takeHarvest(4,5,1,0,WHEAT,w.team->me));
    CHECK(w.game.map.getResource(5,5).amount==0);
}
TEST_CASE("Maxima removes disabled reserves while retaining finite production supply")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame w(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& g=w.game;
    w.addBuilding("school",4,4);w.addBuilding("swarm",12,4);w.addBuilding("hospital",20,4);
    for(int i=0;i<40;++i)w.addUnit(WORKER,2+i%20,15+i/20);
    AIMaxima::Maxima maxima(g.players[0]);
    auto enabled=maxima.observe_labour(maxima.context);
    CHECK(enabled.trainingSlots>0);
    g.gameHeader.setUnitUpgradesDisabled(true);
    auto disabled=maxima.observe_labour(maxima.context);
    CHECK(disabled.trainingSlots==0);CHECK(disabled.trainable==0);
    CHECK(AIMaxima::Labour::plan(disabled,maxima.labour_policy(),4).trainingReserve==0);
    CHECK(disabled.hospitals==1);CHECK(disabled.swarms==1);
    CHECK(AIMaxima::effectiveWheatRegrowth(&g.map,800)==800);
    g.gameHeader.setResourceScarcityLevel(3);CHECK(AIMaxima::effectiveWheatRegrowth(&g.map,800)==100);
    g.gameHeader.setResourceGrowthDisabled(true);CHECK(AIMaxima::effectiveWheatRegrowth(&g.map,800)==0);
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
