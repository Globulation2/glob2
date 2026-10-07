// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ai/cortex/AICortex.h"
#include "ai/cortex/CortexObservation.h"
#include "ai/cortex/CortexBuildings.h"
#include "ai/model/BuildingProjection.h"
#include "Order.h"
#include "CortexPlacement.h"
#include "CortexFoodSources.h"
#include <algorithm>
#include "CortexPolicy.h"
#include <nlohmann/json.hpp>
#include "Player.h"
#include "ai/engine/AIDecision.h"
#include "Version.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>

namespace
{
// Private action helpers are exercised within the same scoped observation
// boundary as production decisions. Returned handles resolve back to fixtures
// only after the decision-local adapter has been released.
class ObservedCortex : public AICortex
{
    Player* owner;
    template<class Function> decltype(auto) decision(Function&& function)
    {
        const auto captured=AIEngine::AIWorldView::capture(*owner->game, AIEngine::AIWorldView::captureCatalog(*owner->game));
        intents.clear();applyQueuedIntent(*captured);
        observedWorld=captured.get();observedTeam=&captured->teams[owner->teamNumber];observedPlayer=owner->number;
        struct Reset { ObservedCortex& ai; ~Reset(){ai.observedWorld=nullptr;ai.observedTeam=nullptr;ai.intents.clear();} } reset{*this};
        return function();
    }
    template<class Function> Building* target(Function&& function)
    {
        const auto ref=decision([&] { auto* building=function();return building ? building->identity : BuildingRef{}; });
        return owner->game->resolveBuilding(ref);
    }
public:
    explicit ObservedCortex(Player* player) : AICortex(player),owner(player) {}
    std::shared_ptr<Order> slowPollForTest() { return decision([&]{return AICortex::decide();}); }
    void queueForTest(std::shared_ptr<Order> order) { decision([&]{enqueueOrder(std::move(order));}); }
    Building* findFlagByGid(Uint16 gid) { return target([&]{return AICortex::findFlagByGid(gid);}); }
    Building* findUpgradeTarget(int type) { return target([&]{return AICortex::findUpgradeTarget(type);}); }
    Building* rediscoverFlag(Uint16& gid,int x,int y) { return target([&]{return AICortex::rediscoverFlag(gid,x,y);}); }
    template<class... Arguments> void translateAction(Arguments&&... arguments)
    { decision([&]{AICortex::translateAction(std::forward<Arguments>(arguments)...);}); }
    template<class... Arguments> void ensureFlagAt(Arguments&&... arguments)
    { decision([&]{AICortex::ensureFlagAt(std::forward<Arguments>(arguments)...);}); }
    void clearOneFlag(Uint16& gid) { decision([&]{AICortex::clearOneFlag(gid);}); }
    void clearAllOffenseFlags() { decision([&]{AICortex::clearAllOffenseFlags();}); }
    bool computeRallyPoint(int& x,int& y) { return decision([&]{return AICortex::computeRallyPoint(x,y);}); }
    void reconcileStaleDefenseFlag(const Cortex::CortexObservation& observation)
    { decision([&]{AICortex::reconcileStaleDefenseFlag(observation);}); }
    int countArrivedAtFlag(Building* flag)
    {
        return decision([&]{return AICortex::countArrivedAtFlag(flag ? observedWorld->buildingAtSlot(flag->gid) : nullptr);});
    }
};
void refreshStats(Team& team)
{
    const auto* previous=team.stats.getLatestStat();
    // A completed sample follows the smoothing window; one step only updates
    // the intermediate counters. Wait for the real snapshot to rotate.
    for(int sample=0;sample<128 && team.stats.getLatestStat()==previous;++sample)team.stats.step(&team);
    REQUIRE(team.stats.getLatestStat()!=previous);
}
void drain(AICortex& ai,Game& game)
{
    while (!ai.orderQueue.empty())
    {
        auto order=ai.orderQueue.front(); ai.orderQueue.pop();
        order->sender=0; game.executeOrder(order,0);
    }
}
}
TEST_SUITE("CortexActionCoverage")
{
    TEST_CASE("idle polls preserve slow path timer and random state")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        const auto view=AIEngine::AIWorldView::capture(fixture.game,
            AIEngine::AIWorldView::captureCatalog(fixture.game));
        const std::vector<AIEngine::ExecutionReceipt> receipts;
        AIEngine::DecisionContext context{*view,0,0,receipts};
        for(int initial:{-25,-24,-2,0,1,23,25,26,48}) {
            ObservedCortex fast(fixture.game.players[0]),slow(fixture.game.players[0]);
            fast.timer=slow.timer=initial;
            const auto randomBefore=syncRandEngine();
            const auto fastOrder=fast.getOrder(context);
            const auto randomAfter=syncRandEngine();
            const auto slowOrder=slow.slowPollForTest();
            CAPTURE(initial);
            CHECK(fastOrder->getOrderType()==ORDER_NULL);
            CHECK(slowOrder->getOrderType()==fastOrder->getOrderType());
            CHECK(fast.timer==slow.timer);
            CHECK(randomBefore==randomAfter);
            CHECK(randomAfter==syncRandEngine());
            CHECK(fast.observedWorld==nullptr);
            CHECK(fast.queryScratch.retainedVectorBytes()==0);
            CHECK(fast.bufferedDiagnostics.empty());
        }
    }
    TEST_CASE("wheat discovery keeps first path depths and local fog boundaries")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.terrain=WATER,.clearImmobile=true,.header=true});
        auto& map=world.game.map;
        std::fill(map.fogOfWar,map.fogOfWar+32*32,world.team->me);
        for(int x=2;x<=14;++x)map.setTerrain(x,8,GRASS);
        // A longer land-only detour reaches the far side of the third food
        // cell later; it must not replace the first path's greater food depth.
        for(int x=8;x<=11;++x)map.setTerrain(x,7,GRASS);
        for(int x:{6,7,10,12}) {
            map.setResourceByIndex(x,8,WHEAT,1);
            REQUIRE(map.isMaterialTakeableSlot(x,8, WHEAT));
        }
        auto index=[&](int x,int y){return static_cast<int>(map.coordToIndex(x,y));};
        const int seed=index(2,8); // Its land exit ring ends at x=5.
        auto scan=[&](const std::vector<int>& seeds,int right,bool ignoreFog) {
            return Cortex::scanFoodSourcesForbidden(map,world.team->me,0,seeds,
                0,0,right,31,0,ignoreFog,true);
        };
        const auto first=scan({seed},31,false);
        CHECK(first.fieldTileCount==4);
        CHECK(first.componentCount==3);
        CHECK(first.depthOf[index(6,8)]==1);
        CHECK(first.depthOf[index(7,8)]==2);
        CHECK(first.depthOf[index(10,8)]==3);
        CHECK(first.depthOf[index(12,8)]==4);
        CHECK(first.classOf[index(7,8)]==Cortex::WC_CHECKER_OPEN);
        for(int x:{6,10,12})CHECK(first.classOf[index(x,8)]==Cortex::WC_FORBIDDEN);
        CHECK((first.desired==std::vector<int>{index(6,8),index(10,8),index(12,8)}));
        CHECK(first.add==first.desired);
        CHECK(first.del.empty());
        const auto repeated=scan({seed,seed,seed},31,false);
        CHECK(repeated.depthOf==first.depthOf);
        CHECK(repeated.classOf==first.classOf);
        CHECK(repeated.desired==first.desired);
        CHECK(repeated.add==first.add);CHECK(repeated.del==first.del);
        CHECK(repeated.fieldTileCount==first.fieldTileCount);
        CHECK(repeated.componentCount==first.componentCount);
        CHECK(repeated.forbiddenCount==first.forbiddenCount);
        CHECK(repeated.addCount==first.addCount);CHECK(repeated.delCount==first.delCount);
        const auto clipped=scan({seed},9,false);
        CHECK(clipped.fieldTileCount==2);
        CHECK(clipped.componentCount==1);
        CHECK(clipped.depthOf[index(10,8)]==-1);
        CHECK((clipped.desired==std::vector<int>{index(6,8)}));
        map.fogOfWar[index(12,8)]=0;
        const auto fogged=scan({seed},31,false);
        CHECK(fogged.fieldTileCount==3);
        CHECK(fogged.depthOf[index(12,8)]==-1);
        CHECK((fogged.desired==std::vector<int>{index(6,8),index(10,8)}));
        const auto revealed=scan({seed},31,true);
        CHECK(revealed.depthOf==first.depthOf);
        CHECK(revealed.desired==first.desired);
        // This scan's territory does not wrap, even when it touches the seam.
        for(int x=0;x<=3;++x)map.setTerrain(x,20,GRASS);
        map.setTerrain(31,20,GRASS);
        map.setResourceByIndex(31,20,WHEAT,1);
        REQUIRE(map.isMaterialTakeableSlot(31,20, WHEAT));
        const auto edge=scan({index(0,20)},31,true);
        CHECK(edge.depthOf[index(31,20)]==-1);
        CHECK(edge.desired.empty());
    }

    TEST_CASE("placement reuses observation qualification without persisting stale worker levels")
    {
        using namespace Cortex;
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int hospital=world.game.buildingsTypes.getPlaceableTypeNum("hospital");
        REQUIRE(hospital>=0);
        snapshot["variants"][hospital]["semantics"]["requiredWorkerLevel"]=1;
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        world.game.configureBuildingCatalog();
        REQUIRE(world.addBuilding("swarm",4,4));
        auto* worker=world.addUnit(WORKER,12,12);
        REQUIRE(worker);
        refreshStats(*world.team);
        auto checkCandidate=[](const BuildCandidate& actual,const BuildCandidate& expected) {
            CHECK(actual.valid==expected.valid);
            CHECK(actual.x==expected.x); CHECK(actual.y==expected.y);
            CHECK(actual.score==expected.score); CHECK(actual.foodSourceDistance==expected.foodSourceDistance);
        };
        for(int qualification=0;qualification<=1;++qualification) {
            CAPTURE(qualification);
            worker->constructionLevel=qualification;
            REQUIRE(world.team->maxBuildLevel()==qualification);
            BuildCandidate ordinary[2][CORTEX_BUILD_CANDIDATES];
            BuildCandidate forward[2];
            int ordinaryCount[2],forwardCount[2];
            std::string randomEnd[2];
            for(int supplied=0;supplied<2;++supplied) {
                MersenneTwister random(713);
                SyncRandScope scope(random);
                const int current=supplied ? world.team->maxBuildLevel() : -1;
                const auto view=AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game));
                Cortex::QueryScratch scratch;Cortex::PlanningIntent intents;
                const auto* observedTeam=&view->teams[world.team->teamNumber];
                ordinaryCount[supplied]=placeCandidates(view.get(),observedTeam,scratch,intents,CORTEX_BUILD_HEAL,0,ordinary[supplied],-1,current);
                forwardCount[supplied]=placeForwardCandidate(view.get(),observedTeam,scratch,intents,CORTEX_BUILD_HEAL,16,16,0,31,forward[supplied],current);
                randomEnd[supplied]=getSyncRandState();
            }
            CHECK(ordinaryCount[0]==ordinaryCount[1]);
            CHECK(forwardCount[0]==forwardCount[1]);
            CHECK(randomEnd[0]==randomEnd[1]);
            for(int slot=0;slot<CORTEX_BUILD_CANDIDATES;++slot)
                checkCandidate(ordinary[0][slot],ordinary[1][slot]);
            checkCandidate(forward[0],forward[1]);
            CHECK((ordinaryCount[0]>0)==(qualification==1));
            CHECK((forwardCount[0]>0)==(qualification==1));
            MersenneTwister random(713);
            SyncRandScope scope(random);
            const auto observation=observe(world.game.players[0],0,NOGBID);
            CHECK(observation.maxBuildLevel==qualification);
            CHECK((observation.buildCandidates[CORTEX_BUILD_HEAL][0].valid!=0)==(qualification==1));
        }
    }

    TEST_CASE("Cortex observations stay isolated from later simulation changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* swarm=fixture.addBuilding("swarm",4,4);
        auto* worker=fixture.addUnit(WORKER,12,12);
        REQUIRE(swarm); REQUIRE(worker);
        refreshStats(*fixture.team);
        const auto view=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
        Cortex::QueryScratch scratch;Cortex::PlanningIntent intents;
        MersenneTwister random(713); SyncRandScope scope(random);
        const auto state=getSyncRandState();
        const auto first=Cortex::observeWorld(view.get(),&view->teams[0],scratch,intents,nullptr,0,NOGBID);
        swarm->maxUnitWorking=99; worker->constructionLevel=3; worker->posX=23;
        fixture.game.stepCounter+=8;
        setSyncRandState(state);
        const auto second=Cortex::observeWorld(view.get(),&view->teams[0],scratch,intents,nullptr,0,NOGBID);
        CHECK(first.tick==second.tick);
        CHECK(first.maxBuildLevel==second.maxBuildLevel);
        CHECK(first.trackedSwarms[0].maxUnitWorking==second.trackedSwarms[0].maxUnitWorking);
        CHECK(first.buildCandidates[Cortex::CORTEX_BUILD_FOOD][0].x==second.buildCandidates[Cortex::CORTEX_BUILD_FOOD][0].x);
    }

    TEST_CASE("bounded model channels preserve stock identity and ignore custom family metadata")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        for(size_t i=0;i<world.game.buildingsTypes.size();++i) {
            const auto& type=*world.game.buildingsTypes.get(i);
            if(!type.runtimeAvailable)continue;
            CAPTURE(type.key);
            CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,type)==type.shortTypeNum);
        }
        const int id=world.game.buildingsTypes.getFinishedTypeNum("stonewall");
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        snapshot["variants"][id]["properties"]["shortTypeNum"]=0;
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,*world.game.buildingsTypes.get(id))==ModelBuildingProjection::PassiveGround);
        snapshot["variants"][id]["semantics"]["production"]["recipes"]={{"worker",{{"enabled",true},{"duration",20},{"cost",nlohmann::json::object()}}}};
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        CHECK(ModelBuildingProjection::channel(world.game.buildingsTypes,*world.game.buildingsTypes.get(id))==ModelBuildingProjection::Production);
    }

    TEST_CASE("stock consumers are not exchange providers and hybrid attractors survive retirement")
    {
        glob2test::HeadlessGlobals globals;
        for(int purpose=0;purpose<3;++purpose) {
            CAPTURE(purpose);
            glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
            const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
            auto& spec=snapshot["variants"][id];
            spec["properties"]["zonable"]={0,0,1};spec["properties"]["maxUnitStayRange"]=8;
            spec["semantics"]["feeding"]["enabled"]=purpose==0;
            spec["semantics"]["market"]["fetchesStock"]=true;
            spec["semantics"]["market"]["fetchesStockExperiment"]="";
            spec["semantics"]["market"]["suppliesDirectStock"]=purpose==1;
            spec["semantics"]["market"]["suppliesDirectStockMaterials"]={"food"};
            world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
            auto* hybrid=world.addBuilding("inn",4,4);
            CHECK(Cortex::servesRole(*AIEngine::AIWorldView::capture(world.game,AIEngine::AIWorldView::captureCatalog(world.game)),*hybrid->type,Cortex::CORTEX_BUILD_EXCHANGE)==(purpose==1));
            ObservedCortex ai(world.game.players[0]);
            Uint16 gid=hybrid->gid;
            REQUIRE(ai.findFlagByGid(gid)==hybrid);
            hybrid->maxUnitWorking=3;
            ai.clearOneFlag(gid);
            CHECK(gid==NOGBID);
            if(purpose<2) CHECK(ai.orderQueue.empty());
            else {
                REQUIRE(ai.orderQueue.size()==1);
                CHECK(std::dynamic_pointer_cast<OrderModifyBuilding>(ai.orderQueue.front())!=nullptr);
                drain(ai,world.game);CHECK(hybrid->maxUnitWorking==0);
            }
            CHECK(hybrid->buildingState==Building::ALIVE);
        }
    }

    TEST_CASE("mixed renamed provider has multiple policy roles but one model and worker identity")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        const int id=world.game.buildingsTypes.getFinishedTypeNum("inn");
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& spec=snapshot["variants"][id];
        spec["properties"]["type"]="unfamiliar-service";spec["properties"]["shortTypeNum"]=IntBuildingType::STONE_WALL;
        spec["semantics"]["production"]["recipes"]={{"explorer",{{"enabled",true},{"duration",20},{"cost",nlohmann::json::object()}}}};
        spec["semantics"]["production"]["fallbackUnit"]=EXPLORER;
        spec["semantics"]["production"]["initialRatios"]={0,0,0};
        spec["semantics"]["feeding"]["cost"]=nlohmann::json::object();
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        auto* building=world.game.addBuilding(4,4,id,0,1,1);REQUIRE(building);
        auto obs = Cortex::makeEmptyObservation(); obs.valid = 1;
        bool found = false; Sint32 x=0,y=0,r=0;
        Cortex::observeBuildings(obs, world.team, &world.game, 0, NOGBID, found, x,y,r);
        CHECK(Cortex::cortexFinishedBuildings(obs, Cortex::CORTEX_BUILD_SWARM) == 1);
        CHECK(Cortex::cortexFinishedBuildings(obs, Cortex::CORTEX_BUILD_FOOD) == 1);
        CHECK(obs.swarmCount == 1); CHECK(obs.innCount == 0);
        int modelCount=0;
        for (const auto& role : obs.modelBuildingCountPerLevel) for (int count : role) modelCount += count;
        CHECK(modelCount == 1);
        CHECK(obs.feedCapacity > 0);
        ObservedCortex ai(world.game.players[0]);
        ai.translateAction(Cortex::makeSetProductionAction(2,3,4), obs);
        REQUIRE(ai.orderQueue.size() == 1);
        const auto order = std::dynamic_pointer_cast<OrderModifySwarm>(ai.orderQueue.front());
        REQUIRE(order);
        // Recipe masking survives the real executor; no unavailable unit is requested.
        drain(ai, world.game);
        CHECK(building->ratio[WORKER] == 0);
        CHECK(building->ratio[EXPLORER] == 3);
        CHECK(building->ratio[WARRIOR] == 0);
    }

    TEST_CASE("actual observation and policy build split overlay producers for requested classes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto prototype=snapshot["variants"][world.game.buildingsTypes.getFinishedTypeNum("warflag")];
        for(auto& variant:snapshot["variants"]) {
            variant["semantics"]["production"]["recipes"]=nlohmann::json::object();
            variant["semantics"]["production"]["initialRatios"]={0,0,0};
        }
        const char* names[]={"worker","explorer","warrior"};
        int ids[3];
        for(int unit=0;unit<3;++unit) {
            auto variant=prototype; ids[unit]=snapshot["variants"].size();
            variant["id"]=ids[unit];variant["key"]=std::string("split.")+names[unit];
            variant["properties"]["type"]=std::string("split-")+names[unit];
            variant["properties"]["zonable"]={0,0,0};
            variant["semantics"]["assignmentLimit"]=0;
            variant["presentation"]["defaultAssigned"]=0;
            variant["semantics"]["production"]["fallbackUnit"]=unit;
            variant["semantics"]["production"]["initialRatios"]={0,0,0};
            variant["semantics"]["production"]["recipes"]={{names[unit],{{"enabled",true},{"duration",150},{"cost",nlohmann::json::object()}}}};
            snapshot["variants"].push_back(variant);
        }
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        world.game.gameHeader.setHungerDisabled(true);
        world.game.gameHeader.setUnitUpgradesDisabled(true);
        world.game.configureBuildingCatalog();
        for(int i=0;i<10;++i) REQUIRE(world.addUnit(WORKER));
        // No ground footprint can be built: valid producer placement must use
        // the engine's overlay occupancy path rather than a fabricated slot.
        for(int y=0;y<world.game.map.getH();++y)
            for(int x=0;x<world.game.map.getW();++x)world.game.map.setResourceByIndex(x,y,STONE,1);
        ObservedCortex ai(world.game.players[0]);
        Cortex::CortexPolicy policy;
        for(int unit=0;unit<3;++unit) {
            bool created=false;
            for(int cycle=0;cycle<4 && !created;++cycle) {
                world.game.stepCounter+=300;
                refreshStats(*world.team);
                const auto obs=Cortex::observe(world.game.players[0],0,NOGBID);
                Sint32 requested[3];Cortex::CortexPolicy::productionTargets(obs,requested);
                CAPTURE(unit);CAPTURE(cycle);CAPTURE(obs.totalUnit);CAPTURE(obs.workers);CAPTURE(obs.hungerDisabled);CAPTURE(obs.productionMask);CAPTURE(obs.productionPlannedMask);CAPTURE(requested[0]);CAPTURE(requested[1]);CAPTURE(requested[2]);
                REQUIRE(obs.productionPlacementType==ids[unit]);
                REQUIRE(obs.buildCandidates[Cortex::CORTEX_BUILD_SWARM][0].valid);
                const auto action=policy.decide(obs);
                ai.translateAction(action,obs);
                if(action.kind==Cortex::ACTION_BUILD) {
                    REQUIRE(action.buildingType==Cortex::CORTEX_BUILD_SWARM);
                    REQUIRE(!ai.orderQueue.empty());
                    auto order=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());
                    REQUIRE(order);CHECK(order->typeNum==ids[unit]);created=true;
                }
                drain(ai,world.game);
            }
            REQUIRE(created);
            bool found=false;
            for(int i=0;i<Building::MAX_COUNT;++i)
                if(auto* b=world.team->myBuildings[i];b && b->typeNum==ids[unit])found=true;
            CHECK(found);
        }
    }

    TEST_CASE("actual production observation preserves positive weights and settles output transitions")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        world.game.gameHeader.setHungerDisabled(true);
        world.game.gameHeader.setUnitUpgradesDisabled(true);
        auto* producer=world.addBuilding("swarm",4,4);
        for(int i=0;i<10;++i) REQUIRE(world.addUnit(WORKER));
        refreshStats(*world.team);
        auto obs=Cortex::observe(world.game.players[0],0,NOGBID);
        Sint32 target[3];Cortex::CortexPolicy::productionTargets(obs,target);
        for(int unit=0;unit<3;++unit)producer->ratio[unit]=target[unit]>0?target[unit]+7:0;
        obs=Cortex::observe(world.game.players[0],0,NOGBID);
        CHECK_FALSE(obs.productionNeedsRetune);
        Cortex::CortexPolicy policy;
        const auto initial=policy.decide(obs);
        CHECK(initial.kind!=Cortex::ACTION_SET_PRODUCTION);
        ObservedCortex ai(world.game.players[0]);ai.translateAction(initial,obs);drain(ai,world.game);
        for(int unit=0;unit<3;++unit)producer->ratio[unit]=0;
        obs=Cortex::observe(world.game.players[0],0,NOGBID);
        REQUIRE(obs.productionNeedsRetune);
        const auto action=policy.decide(obs);
        REQUIRE(action.kind==Cortex::ACTION_SET_PRODUCTION);
        ai.translateAction(action,obs);drain(ai,world.game);
        obs=Cortex::observe(world.game.players[0],0,NOGBID);
        CHECK_FALSE(obs.productionNeedsRetune);
        CHECK(policy.decide(obs).kind!=Cortex::ACTION_SET_PRODUCTION);
    }

    TEST_CASE("provider ranking uses construction materials independently of storage")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
        auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        const int first=world.game.buildingsTypes.getPlaceableTypeNum("inn");
        const int second=world.game.buildingsTypes.getPlaceableTypeNum("hospital");
        const int finished=world.game.buildingsTypes.get(second)->nextLevel;
        snapshot["variants"][first]["properties"]["maxMaterial"]=std::vector<int>(MaterialSlotCount,0);
        snapshot["variants"][first]["semantics"]["constructionCost"]={{"wood",50}};
        snapshot["variants"][finished]["semantics"]["feeding"]["enabled"]=true;
        snapshot["variants"][finished]["semantics"]["feeding"]["unitMask"]=7;
        snapshot["variants"][second]["properties"]["maxMaterial"]=std::vector<int>(MaterialSlotCount,0);
        snapshot["variants"][second]["properties"]["maxMaterial"][WHEAT]=100;
        snapshot["variants"][second]["semantics"]["constructionCost"]={{"wood",1}};
        world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        CHECK(Cortex::selectBuilding(world.game,*world.team,Cortex::CORTEX_BUILD_FOOD).placementType==world.game.buildingsTypes.getPlaceableTypeNum("hospital"));
    }

    TEST_CASE("live capability upgrade eligibility and descriptor capacities drive construction")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* worker=world.addUnit(WORKER,20,20,0,2);
        REQUIRE(worker->workerLevel()==2);
        auto* first=world.addBuilding("barracks",4,4);
        auto* second=world.addBuilding("barracks",12,4);
        ObservedCortex ai(world.game.players[0]);
        REQUIRE(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==first);
        first->hp-=1;
        CHECK(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==second);
        second->hp-=1;
        CHECK(ai.findUpgradeTarget(IntBuildingType::ATTACK_BUILDING)==nullptr);
        first->hp=first->getEffectiveMaxHp(); second->hp=second->getEffectiveMaxHp();
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        bool found=false; Sint32 x=0,y=0,r=0;
        Cortex::observeBuildings(obs,world.team,&world.game,2,NOGBID,found,x,y,r);
        CHECK(obs.upgradableCount[Cortex::CORTEX_BUILD_ATTACK]==2);
        globals->settings.setBuildingAssignment(world.game.buildingsTypes.fingerprint(),
            *world.game.buildingsTypes.get(world.game.buildingsTypes.getTypeNum("barracks",1,true)),7);
        globals->settings.setBuildingAssignment(world.game.buildingsTypes.fingerprint(),
            *world.game.buildingsTypes.get(world.game.buildingsTypes.getTypeNum("barracks",1,false)),9);
        ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto order=std::dynamic_pointer_cast<OrderConstruction>(ai.orderQueue.front());
        REQUIRE(order!=nullptr);
        CHECK(order->gid==first->gid); CHECK(order->unitWorking==4); CHECK(order->unitWorkingFuture==0);
        // A pending upgrade stays private at both supported scheduling extremes.
        for (int delay : {0,8}) {
            CAPTURE(delay);
            world.game.stepCounter += delay;
            ai.translateAction(Cortex::makeUpgradeAction(Cortex::CORTEX_BUILD_ATTACK),obs);
            CHECK(ai.orderQueue.size()==1);
            CHECK(first->constructionResultState==Building::NO_CONSTRUCTION);
            CHECK(first->type->isBuildingSite==false);
        }
        drain(ai,world.game);
        CHECK(first->constructionResultState==Building::UPGRADE);
        CHECK(first->maxUnitWorking==4);
    }

    TEST_CASE("build actions reject invalid slots and cooldown suppresses duplicate orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        ObservedCortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        for (int type : {-1,Cortex::CORTEX_BUILDING_TYPES})
            ai.translateAction(Cortex::makeBuildAction(type,0),obs);
        for (int slot : {-1,Cortex::CORTEX_BUILD_CANDIDATES})
            ai.translateAction(Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,slot),obs);
        ai.translateAction(Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,0),obs);
        CHECK(ai.orderQueue.empty());
        auto& candidate=obs.buildCandidates[Cortex::CORTEX_BUILD_FOOD][0];
        candidate.valid=1; candidate.x=4; candidate.y=4;
        const auto action=Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,0);
        ai.translateAction(action,obs); ai.translateAction(action,obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto order=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());
        REQUIRE(order!=nullptr); CHECK(order->posX==4); CHECK(order->posY==4);
        drain(ai,world.game);
        const auto gid=world.game.map.getBuilding(4,4);
        REQUIRE(gid!=NOGBID);
        auto* inn=world.team->myBuildings[Building::GIDtoID(gid)];
        CHECK(inn->type->isBuildingSite);
        CHECK(inn->type->shortTypeNum==IntBuildingType::FOOD_BUILDING);
    }

    TEST_CASE("war flags create once reconcile force limits and retarget through real orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        ObservedCortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1; obs.tick=100;
        auto& wave=ai.offenseWaves[0];
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,-2,-3,-4,1,obs);
        REQUIRE(ai.orderQueue.size()==1);
        auto create=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());
        REQUIRE(create); CHECK(create->unitWorking==0); CHECK(create->flagRadius==1);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        REQUIRE(wave.gid!=NOGBID);
        auto* flag=ai.findFlagByGid(wave.gid); REQUIRE(flag);
        // Reconciliation changes private intent; the live flag changes only
        // when its emitted priority order is executed.
        CHECK(flag->priority==0); CHECK(flag->unitStayRange==1);
        REQUIRE_FALSE(ai.orderQueue.empty());
        drain(ai,world.game);
        CHECK(flag->priority==1);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,4,4,1,0,0,1,obs);
        CHECK(ai.orderQueue.empty());
        ai.ensureFlagAt(wave.gid,wave.createCooldown,12,12,999,999,999,0,obs);
        CHECK(ai.orderQueue.size()==4);
        drain(ai,world.game);
        CHECK(flag->posX==12); CHECK(flag->posY==12);
        CHECK(flag->maxUnitWorking==Cortex::CORTEX_MAX_FLAG_UNITS);
        CHECK(flag->minLevelToFlag==NB_UNIT_LEVELS-1); CHECK(flag->priority==0);
        ai.ensureFlagAt(wave.gid,wave.createCooldown,12,12,999,999,999,0,obs);
        CHECK(ai.orderQueue.empty());
    }

    TEST_CASE("flag rediscovery excludes claimed flags and stale defense teardown is idempotent")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        ObservedCortex ai(world.game.players[0]);
        auto* first=world.addBuilding("warflag",4,4);
        auto* second=world.addBuilding("warflag",12,12);
        world.addBuilding("explorationflag",3,3);
        ai.offenseWaves[0].gid=first->gid;
        Uint16 unknown=NOGBID;
        REQUIRE(ai.rediscoverFlag(unknown,4,4)==second);
        CHECK(unknown==second->gid);
        ai.defenseFlags[0].gid=second->gid;
        CHECK(ai.rediscoverFlag(unknown,4,4)==nullptr);
        CHECK(ai.isOwnedGid(first->gid)); CHECK_FALSE(ai.isOwnedGid(NOGBID));
        auto obs=Cortex::makeEmptyObservation(); obs.buildingsUnderAttack=1;
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.empty());
        obs.buildingsUnderAttack=0;
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.size()==1);
        CHECK(ai.defenseFlags[0].gid==NOGBID);
        ai.reconcileStaleDefenseFlag(obs); CHECK(ai.orderQueue.size()==1);
        drain(ai,world.game);
        ai.clearAllOffenseFlags(); CHECK(ai.orderQueue.size()==1);
        CHECK(ai.offenseWaves[0].gid==NOGBID);
        CHECK(ai.offenseWaves[0].landingX==-1); CHECK(ai.offenseWaves[0].phase==AICortex::WAVE_NONE);
        drain(ai,world.game);
        ai.clearAllOffenseFlags(); CHECK(ai.orderQueue.empty());
        CHECK(ai.findFlagByGid(second->gid)==nullptr);
    }

    TEST_CASE("rally selection and bound warrior arrivals respect colony preference and toroidal range")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        ObservedCortex ai(world.game.players[0]);
        int x=-1,y=-1;
        CHECK_FALSE(ai.computeRallyPoint(x,y));
        world.addBuilding("inn",4,4);
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==4); CHECK(y==4);
        auto* swarm=world.addBuilding("swarm",12,4);
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==12); CHECK(y==4);
        swarm->buildingState=Building::DEAD;
        REQUIRE(ai.computeRallyPoint(x,y)); CHECK(x==4); CHECK(y==4);
        swarm->buildingState=Building::ALIVE;
        auto* flag=world.addBuilding("warflag",0,0); flag->unitStayRange=1;
        auto* inside=world.addUnit(WARRIOR,31,31);
        auto* outside=world.addUnit(WARRIOR,10,10);
        flag->unitsWorking.push_back(inside); flag->unitsWorking.push_back(outside);
        flag->unitsWorking.push_back(nullptr);
        CHECK(ai.countArrivedAtFlag(flag)==1);
        CHECK(ai.countArrivedAtFlag(nullptr)==0);
        flag->unitsWorking.clear();
    }

    TEST_CASE("production clamps ratios applies orders and deduplicates finished swarms")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* swarm=world.addBuilding("swarm",4,4);
        world.addBuilding("inn",12,4);
        ObservedCortex ai(world.game.players[0]);
        auto obs=Cortex::makeEmptyObservation(); obs.valid=1;
        const auto action=Cortex::makeSetProductionAction(-2,3,99);
        ai.translateAction(action,obs);
        REQUIRE(ai.orderQueue.size()==1);
        drain(ai,world.game);
        CHECK(swarm->ratio[WORKER]==0); CHECK(swarm->ratio[EXPLORER]==3);
        CHECK(swarm->ratio[WARRIOR]==Cortex::CORTEX_MAX_RATIO);
        ai.translateAction(action,obs); CHECK(ai.orderQueue.empty());
        swarm->buildingState=Building::WAITING_FOR_DESTRUCTION;
        ai.translateAction(Cortex::makeSetProductionAction(1,0,0),obs);
        CHECK(ai.orderQueue.empty());
        swarm->buildingState=Building::ALIVE;
    }
}

TEST_CASE("Cortex rejection receipts clear exact delayed upgrade and flag intents" * doctest::test_suite("CortexActionCoverage"))
{
    glob2test::HeadlessGlobals globals;
    for(const int delay:{0,8}) {
        glob2test::HeadlessGame fixture({.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto* building=fixture.addBuilding("barracks",4,4);REQUIRE(building);
        AICortex ai(fixture.game.players[0]);
        const int role=Cortex::CORTEX_BUILD_ATTACK;
        ai.pendingUpgradeType=role;ai.pendingUpgradeUntil=2000;ai.buildCooldownUntil[role]=250;
        auto upgrade=std::make_shared<OrderConstruction>(building->gid,1,1);
        ai.rememberQueuedBuild(*upgrade,role);ai.orderQueue.push(upgrade);
        const auto view=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
        const std::vector<AIEngine::ExecutionReceipt> empty;
        AIEngine::DecisionContext context{*view,0,0,empty};context.pollSequence=17;context.scheduledTick=delay;
        CHECK(ai.getOrder(context)->getOrderType()==ORDER_CONSTRUCTION);
        REQUIRE(ai.issuedCommands.size()==1);
        CHECK((ai.issuedCommands[0].target==BuildingRef{building->gid,building->scriptIdentity}));
        AIEngine::ExecutionReceipt rejected;
        rejected.request={0,1,view->tick,17,0};rejected.scheduledTick=delay;
        rejected.executionTick=delay;rejected.status=AIEngine::ExecutionStatus::Rejected;
        rejected.command=ai.issuedCommands[0].bytes;
        fixture.game.stepCounter=delay+1;
        const auto later=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
        const std::vector<AIEngine::ExecutionReceipt> receipts{rejected};
        AIEngine::DecisionContext feedback{*later,0,0,receipts};feedback.pollSequence=18;feedback.scheduledTick=later->tick+delay;
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream saved(backend);ai.save(&saved);saved.flush();
        const auto bytes=backend->takeContents();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
        AICortex resumed(&input,fixture.game.players[0],VERSION_MINOR);
        REQUIRE(resumed.issuedCommands.size()==1);
        resumed.getOrder(feedback);
        CHECK(resumed.pendingUpgradeType==-1);CHECK(resumed.issuedCommands.empty());
        ai.getOrder(feedback);
        CHECK(ai.pendingUpgradeType==-1);CHECK(ai.pendingUpgradeUntil==0);
        CHECK(ai.buildCooldownUntil[role]==0);CHECK(ai.issuedCommands.empty());
        // Repeated old feedback cannot retire a newer request on the same target.
        ai.pendingUpgradeType=role;ai.pendingUpgradeUntil=4000;ai.buildCooldownUntil[role]=500;
        ai.rememberQueuedBuild(*upgrade,role);ai.orderQueue.push(upgrade);
        ai.getOrder(feedback);REQUIRE(ai.issuedCommands.size()==1);
        ai.getOrder(feedback);
        CHECK(ai.pendingUpgradeUntil==4000);CHECK(ai.buildCooldownUntil[role]==500);
        CHECK(ai.issuedCommands.size()==1);
        ai.issuedCommands.clear();
        const int flag=fixture.game.buildingsTypes.getTypeNum("warflag",0,false);
        REQUIRE(flag>=0);
        auto create=std::make_shared<OrderCreate>(0,10,10,flag,1,1,4);
        ai.offenseWaves[0].createCooldown=750;ai.offenseWaves[1].createCooldown=750;
        ai.rememberFlagCreation(*create,ai.offenseWaves[0].createCooldown);ai.orderQueue.push(create);
        context.pollSequence=19;ai.getOrder(context);REQUIRE(ai.issuedCommands.size()==1);
        rejected.request.pollSequence=19;rejected.command=ai.issuedCommands[0].bytes;
        const std::vector<AIEngine::ExecutionReceipt> flagReceipt{rejected};
        AIEngine::DecisionContext flagFeedback{*later,0,0,flagReceipt};flagFeedback.pollSequence=20;
        ai.getOrder(flagFeedback);
        CHECK(ai.offenseWaves[0].createCooldown==0);
        CHECK(ai.offenseWaves[1].createCooldown==750);
    }
}
TEST_CASE("Cortex delayed staffing overlay respects entity incarnations" * doctest::test_suite("CortexActionCoverage"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto* building=fixture.addBuilding("swarm",4,4);REQUIRE(building);
    ObservedCortex ai(fixture.game.players[0]);
    const int oldWorkers=building->maxUnitWorking;
    const int requested=oldWorkers==3 ? 4 : 3;
    ai.orderQueue.push(std::make_shared<OrderModifyBuilding>(building->gid,requested));
    const auto view=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    const std::vector<AIEngine::ExecutionReceipt> receipts;
    AIEngine::DecisionContext context{*view,0,0,receipts};context.scheduledTick=8;
    ai.getOrder(context);
    ai.intents.clear();ai.applyQueuedIntent(*view);
    CHECK(Cortex::plannedWorkers(ai.intents,*view->buildingAtSlot(building->gid))==requested);
    CHECK(building->maxUnitWorking==oldWorkers);
    building->scriptIdentity=fixture.game.allocateScriptIdentity(true,building->gid);
    const auto replacement=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    ai.intents.clear();ai.applyQueuedIntent(*replacement);
    CHECK(Cortex::plannedWorkers(ai.intents,*replacement->buildingAtSlot(building->gid))==oldWorkers);
    ai.queueForTest(std::make_shared<OrderModifyBuilding>(building->gid,requested));
    building->scriptIdentity=fixture.game.allocateScriptIdentity(true,building->gid);
    const auto reused=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    AIEngine::DecisionContext later{*reused,0,0,receipts};later.scheduledTick=8;later.pollSequence=1;
    CHECK(ai.getOrder(later)->getOrderType()==ORDER_NULL);
    CHECK(ai.orderQueue.empty());CHECK(ai.queuedCommands.empty());
    CHECK(ai.issuedCommands.size()==1); // the first issued request still awaits its own receipt
}
