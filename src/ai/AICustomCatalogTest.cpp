// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "Version.h"
#include "AINumbi.h"
#include "AICastor.h"
#include "AIWarrush.h"
#include "AICabino.h"
#include "AINicowar.h"
#include "AISharedRuntimeTuning.h"
#include "AIMaxima.h"
#include "AIMaximaBuildings.h"
#include "ai/cortex/AICortex.h"
#include "ai/cortex/CortexObservation.h"
#include "shared_runtime/Runtime.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <functional>

namespace
{
constexpr AI::ImplementationID Controllers[]={AI::NUMBI,AI::CASTOR,AI::WARRUSH,
    AI::ECONO,AI::NICOWAR,AI::CORTEX,AI::MAXIMA,AI::CABINO};
using Intent=AIPlanning::BuildingIntent;

// Fixed ablations intentionally change identity, ordering and service ownership
// together. Only hospital-shaped variants retain feeding, alongside healing.
struct CatalogWorld
{
    glob2test::HeadlessGame world{glob2test::GameOptions{
        .wDec=6,.hDec=6,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
    int replacement=-1,completed=-1,anchor=-1;
    explicit CatalogWorld(AI::ImplementationID ai,bool missing=false,bool populatedProvider=false,bool splitProduction=false,
        const std::function<void(nlohmann::json&)>& customize={})
    {
        auto& game=world.game;
        auto stock=game.buildingsTypes;
        const int food=stock.getTypeNum("inn",0,false);
        const int hospital=stock.getTypeNum("hospital",0,false);
        const int site=stock.getTypeNum("hospital",0,true);
        const auto anchorKey=stock.get(stock.getTypeNum("swarm",0,false))->key;
        const auto siteKey=stock.get(site)->key,completeKey=stock.get(hospital)->key;
        auto snapshot=nlohmann::json::parse(stock.snapshotJson());
        auto& variants=snapshot["variants"];
        const auto feeding=variants[food]["semantics"]["feeding"];
        for(auto& variant:variants) {
            const int oldId=variant["id"].get<int>();
            const auto* old=stock.get(oldId);
            variant["id"]=int(variants.size())-1-oldId;
            variant["properties"]["type"]="fixture-family-"+std::to_string(old->shortTypeNum);
            // Presentation levels deliberately disagree with transition order.
            variant["properties"]["level"]=(old->level+1)%NB_UNIT_LEVELS;
            variant["semantics"]["feeding"]["enabled"]=false;
            if(!missing && old->type=="hospital" && !old->isBuildingSite) {
                variant["semantics"]["feeding"]=feeding;
                variant["semantics"]["assignmentLimit"]=20;
                variant["properties"]["maxResource"][WHEAT]=32;
            }
            if(splitProduction) {
                auto& recipes=variant["semantics"]["production"]["recipes"];
                recipes=nlohmann::json::object();
                int output=-1;
                if(old->type=="swarm" && !old->isBuildingSite) output=WORKER;
                if(old->type=="hospital" && old->level==0 && !old->isBuildingSite) output=EXPLORER;
                if(old->type=="barracks" && old->level==0 && !old->isBuildingSite) output=WARRIOR;
                if(output>=0) {
                    static const char* names[]={"worker","explorer","warrior"};
                    recipes[names[output]]={{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}};
                    variant["semantics"]["production"]["scheduling"]="weighted_committed_job";
                    variant["semantics"]["assignmentLimit"]=20;
                    if(output!=WORKER) {variant["semantics"]["placeable"]=true;variant["semantics"]["instantPlacement"]=true;}
                }
            }
        }
        if(customize) customize(snapshot);
        game.buildingsTypes.loadSnapshotJson(snapshot.dump());
        game.configureBuildingCatalog();
        replacement=game.buildingsTypes.findByKey(siteKey);
        completed=game.buildingsTypes.findByKey(completeKey);
        anchor=game.buildingsTypes.findByKey(anchorKey);
        REQUIRE(replacement!=site);
        GameHeader header;header.setNumberOfPlayers(2);header.setRandomSeed(713);
        header.getBasePlayer(0)=BasePlayer(0,"catalog AI",0,BasePlayer::playerTypeFromImplementationID(ai));
        header.getBasePlayer(1)=BasePlayer(1,"opponent",1,BasePlayer::P_LOCAL);
        game.setGameHeader(header,true);game.setWaitingOnMask(0);
        for(int team=0;team<2;++team) {
            const int offset=32*team;
            auto* building=game.addBuilding(4+offset,4+offset,anchor,team,4,4);
            REQUIRE(building);
            building->resources[WHEAT]=building->type->maxResource[WHEAT];
            game.teams[team]->addToStaticAbilitiesLists(building);
            game.teams[team]->startPosX=4+offset;game.teams[team]->startPosY=4+offset;
            game.teams[team]->startPosSet=Team::START_POS_FROM_UNIT;
            if(populatedProvider) {
                auto* provider=game.addBuilding(10+offset,4+offset,completed,team,3,3);
                REQUIRE(provider);provider->resources[WHEAT]=32;
                game.teams[team]->addToStaticAbilitiesLists(provider);
            }
            for(int i=0;i<12;++i)world.addUnit(i<10?WORKER:WARRIOR,4+offset+i,12+offset,team);
            for(int y=18;y<24;++y)for(int x=8;x<24;++x)game.map.setResource(x+offset,y+offset,WHEAT,1);
            for(int x=24;x<29;++x)for(int y=18;y<24;++y)game.map.setResource(x+offset,y+offset,WOOD,5);
            for(int sample=0;sample<TeamStats::STATS_SMOOTH_SIZE;++sample) game.teams[team]->stats.step(game.teams[team]);
        }
        // Nicowar's existing siting policy requires a finite distance from water.
        game.map.setCellTerrain(0,0,WATER);
        game.map.setMapDiscovered();
    }
};

int selectThroughController(CatalogWorld& fixture,AI::ImplementationID id,bool missing)
{
    auto& game=fixture.world.game;
    auto* implementation=game.players[0]->ai->aiImplementation;
    switch(id) {
    case AI::NUMBI:return dynamic_cast<AINumbi*>(implementation)->selectBuilding(Intent::Feed);
    case AI::CASTOR:return dynamic_cast<AICastor*>(implementation)->selectBuilding(AICastor::FeedUnits);
    case AI::WARRUSH:return dynamic_cast<AIWarrush*>(implementation)->selectBuilding(Intent::Feed);
    case AI::CABINO:return Cabino::selectBuilding(*dynamic_cast<Cabino::AICabino*>(implementation),Cabino::FeedUnits);
    case AI::ECONO:case AI::NICOWAR: {
        auto& runtime=*dynamic_cast<AISharedRuntime::Runtime*>(implementation);
        runtime.gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);
        runtime.br.initiate();
        if(id==AI::ECONO) {
            auto& ai=*dynamic_cast<AISharedRuntime::Econo*>(runtime.runtimeai.get());
            ai.timer=AISharedRuntime::AI_SHARED_RUNTIME_RTI_INN_INTERVAL_TICKS;
            ai.tick_inns_near_wheat(runtime);
        } else dynamic_cast<NewNicowar*>(runtime.runtimeai.get())->order_regular_inn(runtime);
        REQUIRE(runtime.building_orders.size()==1);
        const int selected=runtime.building_orders.front()->get_concrete_type();
        for(int step=0;step<32 && !runtime.building_orders.empty();++step) {
            runtime.gm->update();runtime.update_building_orders();
        }
        CHECK(runtime.building_orders.empty());
        bool emitted=false;
        for(const auto& order:runtime.orders)
            if(auto create=std::dynamic_pointer_cast<OrderCreate>(order)) {
                CHECK(create->typeNum==fixture.replacement);emitted=true;
            }
        CHECK(emitted==!missing);
        if(missing) {
            CHECK(runtime.br.pending_buildings.empty());
            runtime.update_management_orders();
            CHECK(runtime.management_orders.empty());
        }
        return selected;
    }
    case AI::CORTEX: {
        auto& ai=*dynamic_cast<AICortex*>(implementation);
        auto obs=Cortex::makeEmptyObservation();obs.valid=1;
        auto& slot=obs.buildCandidates[Cortex::CORTEX_BUILD_FOOD][0];slot.valid=1;slot.x=30;slot.y=4;
        ai.translateAction(Cortex::makeBuildAction(Cortex::CORTEX_BUILD_FOOD,0),obs);
        if(missing){CHECK(ai.orderQueue.empty());return -1;}
        REQUIRE(ai.orderQueue.size()==1);
        auto create=std::dynamic_pointer_cast<OrderCreate>(ai.orderQueue.front());REQUIRE(create);
        return create->typeNum;
    }
    case AI::MAXIMA: {
        auto& ai=*dynamic_cast<AIMaxima::Maxima*>(implementation);
        AIMaximaRuntime::Construction::BuildingOrder order(AIMaximaBuildings::Feeding,2);
        order.add_constraint(new AIMaximaRuntime::Construction::SinglePosition(30,4));
        bool complete=false;const auto placement=order.find_location(ai.context,1,complete);
        CHECK(complete);CHECK(placement.found==!missing);
        return order.concreteType;
    }
    default:FAIL("unhandled controller");return -1;
    }
}

std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result,buildings,units;
    game.checkSum(&result,&buildings,&units,true);
    result.erase(result.begin()); // Save-format metadata changes at the loading boundary.
    result.insert(result.end(),buildings.begin(),buildings.end());
    result.insert(result.end(),units.begin(),units.end());return result;
}
}

TEST_SUITE("AICustomCatalog")
{
TEST_CASE("all native controllers select renamed mixed providers and release missing demands")
{
    glob2test::HeadlessGlobals globals;
    for(auto id:Controllers)for(bool missing:{false,true}) {
        CAPTURE(id);CAPTURE(missing);CatalogWorld fixture(id,missing);
        const int selected=selectThroughController(fixture,id,missing);
        CHECK(selected==(missing ? -1 : fixture.replacement));
        if(!missing) {
            const auto& index=fixture.world.game.buildingCapabilities();
            CHECK(index.matches(fixture.completed,Intent::Feed));
            CHECK(index.matches(fixture.completed,Intent::Heal));
        }
    }
}
TEST_CASE("Nicowar and Econo keep independent weighted material objectives and stone clearance")
{
    glob2test::HeadlessGlobals globals;
    for(auto controller:{AI::NICOWAR,AI::ECONO}) for(bool pool:{false,true}) {
        CAPTURE(controller);CAPTURE(pool);
        CatalogWorld fixture(controller,false,false,false,[&](nlohmann::json& snapshot) {
            // Stock pools need only wood. Add a second ingredient to exercise
            // the existing wheat preference with a genuinely mixed recipe.
            if(pool) for(auto& variant:snapshot["variants"])
                if(variant["key"]=="swimmingpool.0.site") variant["semantics"]["constructionCost"]["wheat"]=1;
        });
        auto& game=fixture.world.game;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) game.map.setNoResource(x,y,0);
        game.map.setResource(4,20,WOOD,5);game.map.setResource(28,20,STONE,5);game.map.setResource(28,40,WHEAT,5);
        auto& runtime=*dynamic_cast<AISharedRuntime::Runtime*>(game.players[0]->ai->aiImplementation);
        runtime.gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);runtime.br.initiate();
        if(controller==AI::NICOWAR) {
            auto& ai=*dynamic_cast<NewNicowar*>(runtime.runtimeai.get());
            if(pool) ai.order_regular_swimmingpool(runtime);else ai.order_regular_racetrack(runtime);
        } else {
            auto& ai=*dynamic_cast<AISharedRuntime::Econo*>(runtime.runtimeai.get());
            ai.timer=pool?AISharedRuntime::AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_OFFSET_TICKS:AISharedRuntime::AI_SHARED_RUNTIME_RTI_RACETRACK_OFFSET_TICKS;
            if(pool) ai.tick_swimmingpool_near_wheat_wood(runtime);else ai.tick_racetrack_near_stone_wood(runtime);
        }
        REQUIRE(runtime.building_orders.size()==1);
        std::map<int,std::shared_ptr<AISharedRuntime::Construction::MinimizedDistance>> objectives;
        std::shared_ptr<AISharedRuntime::Construction::MinimumDistance> stoneClearance;
        for(const auto& constraint:runtime.building_orders.front()->constraints) {
            auto* gradient=constraint->get_gradient_info();if(!gradient || gradient->sources.size()!=1) continue;
            auto resource=std::dynamic_pointer_cast<AISharedRuntime::Gradients::Entities::Resource>(gradient->sources.front());
            if(!resource) continue;
            if(auto objective=std::dynamic_pointer_cast<AISharedRuntime::Construction::MinimizedDistance>(constraint)) objectives.emplace(resource->resource_type,objective);
            if(resource->resource_type==STONE) if(auto clearance=std::dynamic_pointer_cast<AISharedRuntime::Construction::MinimumDistance>(constraint)) stoneClearance=clearance;
        }
        REQUIRE(objectives.contains(WOOD));REQUIRE(objectives.contains(pool?WHEAT:STONE));
        CHECK(objectives.size()==2);
        CHECK(objectives.at(WOOD)->weight==4);CHECK(objectives.at(pool?WHEAT:STONE)->weight==1);
        REQUIRE(stoneClearance);
        CHECK(stoneClearance->passes_constraint(runtime,4,20));
        CHECK_FALSE(stoneClearance->passes_constraint(runtime,28,20));
        if(!pool) {
            CHECK(objectives.at(WOOD)->calculate_constraint(runtime,6,20)==objectives.at(WOOD)->calculate_constraint(runtime,4,22));
            CHECK(objectives.at(STONE)->calculate_constraint(runtime,6,20)>objectives.at(STONE)->calculate_constraint(runtime,4,22));
        }
    }
}
TEST_CASE("shared runtime retirement distinguishes attraction and feeding and preserves mixed services")
{
    glob2test::HeadlessGlobals globals;
    for(int variant:{0,1,2}) {
        CAPTURE(variant);
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& game=world.game;
        const int flagType=game.buildingsTypes.getTypeNum("warflag",0,false);
        const int foodType=game.buildingsTypes.getTypeNum("inn",0,false);
        auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
        auto& flag=snapshot["variants"][flagType];auto& food=snapshot["variants"][foodType];
        if(variant==1) {
            flag["properties"]["maxUnitInside"]=1;
            flag["semantics"]["training"]["armor"]={{"enabled",true},{"unitMask",7},{"targetLevel",1},{"duration",32},{"cost",nlohmann::json::object()}};
            food["semantics"]["healing"]={{"enabled",true},{"unitMask",7},{"duration",32},{"cost",nlohmann::json::object()}};
        }
        if(variant==2) {
            flag["semantics"]["market"]["suppliesDirectStock"]=true;
            flag["semantics"]["market"]["suppliesDirectStockResources"]={"wood"};
            flag["properties"]["maxResource"][WOOD]=8;
            food["semantics"]["feeding"]["cost"]=nlohmann::json::object();
        }
        game.buildingsTypes.loadSnapshotJson(snapshot.dump());game.configureBuildingCatalog();
        auto* flagBuilding=game.addBuilding(4,4,flagType,0,2,2);
        auto* foodBuilding=game.addBuilding(12,12,foodType,0,2,2);
        REQUIRE(flagBuilding);REQUIRE(foodBuilding);
        AISharedRuntime::Runtime runtime(new AISharedRuntime::Econo,game.players[0]);runtime.br.initiate();
        using namespace AISharedRuntime::Management;
        for(int id:{0,1}) {
            std::unique_ptr<ManagementOrder> retirement(id==0 ? static_cast<ManagementOrder*>(new RetireAttraction(id,1u<<WARRIOR)) : new RetireFeeding(id));
            auto* memory=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(memory);
            ManagementOrder::save_order(retirement.get(),&output);output.flush();const auto bytes=memory->takeContents();
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
            runtime.add_management_order(ManagementOrder::load_order(&input,game.players[0],VERSION_MINOR));
            runtime.update_management_orders();
            if(variant==0) {
                REQUIRE(runtime.orders.size()==1);
                auto deletion=std::dynamic_pointer_cast<OrderDelete>(runtime.orders.front());REQUIRE(deletion);
                CHECK(deletion->gid==(id==0?flagBuilding:foodBuilding)->gid);
            } else CHECK(runtime.orders.empty());
            runtime.orders.clear();
        }
        // Explicit demolition remains available; the two retirement policies
        // carry their own preservation rules instead of changing every delete.
        runtime.add_management_order(new DestroyBuilding(1));runtime.update_management_orders();
        REQUIRE(runtime.orders.size()==1);CHECK(runtime.orders.front()->getOrderType()==ORDER_DELETE);
    }
}
TEST_CASE("Nicowar retained attraction retirement completes missions across save load")
{
    glob2test::HeadlessGlobals globals;
    for(int kind:{0,1,2}) {
        CAPTURE(kind);
        CatalogWorld fixture(AI::NICOWAR,false,false,false,[kind](auto& snapshot) {
            for(auto& variant:snapshot["variants"]) if(variant["key"]=="warflag.0.finished") {
                if(kind==0) {
                    variant["properties"]["maxUnitInside"]=1;
                    variant["semantics"]["healing"]={{"enabled",true},{"unitMask",7},{"duration",32},{"cost",nlohmann::json::object()}};
                }
                if(kind==1) {
                    variant["semantics"]["occupiesGround"]=true;
                    variant["properties"]["hpMax"]=100;variant["properties"]["hpInit"]=100;
                }
                if(kind==2) variant["properties"]["zonable"][EXPLORER]=1;
            }
        });
        auto& game=fixture.world.game;
        auto* flag=game.addBuilding(20,28,game.buildingsTypes.findByKey("warflag.0.finished"),0,4,4);REQUIRE(flag);
        game.teams[1]->myBuildings[0]->seenByMask|=game.teams[0]->me;
        auto& runtime=*dynamic_cast<AISharedRuntime::Runtime*>(game.players[0]->ai->aiImplementation);
        runtime.gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);runtime.br.initiate();
        int id=-1;for(auto it=runtime.br.begin();it!=runtime.br.end();++it) if(runtime.br.get_building(it->first)==flag) id=it->first;
        REQUIRE(id>=0);
        auto& ai=*dynamic_cast<NewNicowar*>(runtime.runtimeai.get());
        NicowarStrategyLoader loader;ai.strategy=loader.getParticularStrategy("default");ai.target=1;ai.war=true;
        ai.attack_flags.push_back(id);ai.defense_flags.push_back(id);ai.explorer_attack_flags.push_back(id);
        using namespace AISharedRuntime::Management;using namespace AISharedRuntime::Conditions;
        RetireAttraction retire(id,1u<<WARRIOR);retire.modify(runtime);
        CHECK(runtime.attraction_retired_or_destroyed(id,1u<<WARRIOR));
        CHECK_FALSE(runtime.attraction_retired_or_destroyed(id,1u<<EXPLORER));
        if(kind==1) {REQUIRE(runtime.orders.size()==1);CHECK(runtime.orders.front()->getOrderType()==ORDER_MODIFY_BUILDING);}
        else CHECK(runtime.orders.empty());
        for(const auto& message:{"attack finished ","guard flag deleted "}) {
            auto* completion=new SendMessage(std::string(message)+std::to_string(id));
            completion->add_condition(new AttractionRetiredOrDestroyed(id,1u<<WARRIOR));runtime.add_management_order(completion);
        }
        auto* explorerCompletion=new SendMessage("explorer attack flag deleted "+std::to_string(id));
        explorerCompletion->add_condition(new AttractionRetiredOrDestroyed(id,1u<<EXPLORER));runtime.add_management_order(explorerCompletion);
        auto* memory=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(memory);runtime.save(&output);output.flush();const auto bytes=memory->takeContents();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);REQUIRE(runtime.load(&input,game.players[0],VERSION_MINOR));
        CHECK(runtime.attraction_retired_or_destroyed(id,1u<<WARRIOR));
        runtime.update_management_orders();CHECK(ai.attack_flags.empty());CHECK(ai.defense_flags.empty());CHECK(ai.explorer_attack_flags.size()==1);
        ai.handle_message(runtime,"attack finished "+std::to_string(id));
        ai.handle_message(runtime,"guard flag deleted "+std::to_string(id));
        REQUIRE(flag->buildingState==Building::ALIVE);
        ai.control_attacks(runtime);REQUIRE(ai.attack_flags.size()==1);CHECK(ai.attack_flags.front()!=id);
        CHECK(runtime.begin_attraction(id,1u<<WARRIOR));
        CHECK_FALSE(runtime.attraction_retired_or_destroyed(id,1u<<WARRIOR));
        {
            auto* again=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream savedAgain(again);runtime.save(&savedAgain);savedAgain.flush();const auto state=again->takeContents();
            GAGCore::BinaryInputStream reloaded(new GAGCore::MemoryStreamBackend(state.data(),state.size()));reloaded.seekFromStart(0);REQUIRE(runtime.load(&reloaded,game.players[0],VERSION_MINOR));
            CHECK_FALSE(runtime.attraction_retired_or_destroyed(id,1u<<WARRIOR));
        }
        RetireAttraction secondRetirement(id,1u<<WARRIOR);secondRetirement.modify(runtime);
        CHECK(runtime.attraction_retired_or_destroyed(id,1u<<WARRIOR));
        if(kind==2) {
            RetireAttraction retireExplorer(id,1u<<EXPLORER);retireExplorer.modify(runtime);runtime.update_management_orders();
            CHECK(ai.explorer_attack_flags.empty());
            ai.handle_message(runtime,"explorer attack flag deleted "+std::to_string(id));
        }
    }
}
TEST_CASE("production placement falls back from an obstructed preferred footprint")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    auto& game=world.game;
    const int anchor=game.buildingsTypes.getTypeNum("swarm",0,false);
    const int large=game.buildingsTypes.getTypeNum("hospital",0,false);
    const int small=game.buildingsTypes.getTypeNum("barracks",0,true);
    const int complete=game.buildingsTypes.getTypeNum("barracks",0,false);
    auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    auto& variants=snapshot["variants"];
    for(auto& variant:variants) {
        variant["semantics"]["production"]["recipes"]=nlohmann::json::object();
        variant["semantics"]["placeable"]=false;
    }
    for(int type:{anchor,large,complete}) {
        auto& variant=variants[type];
        variant["semantics"]["production"]["scheduling"]="weighted_committed_job";
        variant["semantics"]["production"]["recipes"]={{type==anchor?"worker":"explorer",{{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}}}};
        variant["semantics"]["assignmentLimit"]=20;
    }
    variants[large]["semantics"]["placeable"]=true;variants[large]["semantics"]["instantPlacement"]=true;
    variants[large]["properties"]["width"]=4;variants[large]["properties"]["height"]=4;
    for(int type:{small,complete}) {variants[type]["properties"]["width"]=2;variants[type]["properties"]["height"]=2;}
    variants[small]["semantics"]["placeable"]=true;
    variants[small]["semantics"]["constructionCost"]={{"wood",1}};
    variants[small]["semantics"]["requiredWorkerLevel"]=0;
    game.buildingsTypes.loadSnapshotJson(snapshot.dump());game.configureBuildingCatalog();
    REQUIRE(game.addBuilding(4,4,anchor,0,4,4));world.addUnit(WORKER,8,4);
    for(int sample=0;sample<TeamStats::STATS_SMOOTH_SIZE;++sample) world.team->stats.step(world.team);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
        if(!((x>=4 && x<8 && y>=4 && y<8) || (x>=14 && x<16 && y>=14 && y<16) || (y==8 && x>=8 && x<=14) || (x==14 && y>=8 && y<14))) game.map.setResource(x,y,STONE,1);
    const auto& candidates=game.buildingCapabilities().placementsByCost(Intent::ProduceExplorer);
    REQUIRE(candidates.size()==2);CHECK(candidates.front().placementType==large);
    for(int repeat=0;repeat<2;++repeat) {
        auto order=std::dynamic_pointer_cast<OrderCreate>(AIPlanning::missingProductionOrder(game,*world.team,{1,1,0},2,2));
        REQUIRE(order);CHECK(order->typeNum==small);CHECK(order->posX==14);CHECK(order->posY==14);
    }
}
TEST_CASE("Nicowar resolves one aggregate production demand per colony management pass")
{
    glob2test::HeadlessGlobals globals;
    for(bool missing:{false,true}) {
        CAPTURE(missing);
        CatalogWorld fixture(AI::NICOWAR,false,false,true,[missing](auto& snapshot) {
            if(!missing) return;
            for(auto& variant:snapshot["variants"]) {
                variant["semantics"]["placeable"]=false;
                auto& recipes=variant["semantics"]["production"]["recipes"];
                recipes.erase("explorer");recipes.erase("warrior");
            }
        });
        auto& game=fixture.world.game;auto& team=*game.teams[0];
        game.gameHeader.setHungerDisabled(true);game.gameHeader.setUnitUpgradesDisabled(true);
        CHECK(game.buildingsTypes.get(fixture.anchor)->semantics.production.enabledUnitMask==(1u<<WORKER));
        CHECK(game.buildingsTypes.getRuntime(fixture.anchor)->productionEnabledMask==(1u<<WORKER));
        for(int n=0;n<4;++n) REQUIRE(game.addBuilding(4+8*n,28,fixture.anchor,0,4,4));
        auto& runtime=*dynamic_cast<AISharedRuntime::Runtime*>(game.players[0]->ai->aiImplementation);
        runtime.gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);runtime.br.initiate();
        auto& ai=*dynamic_cast<NewNicowar*>(runtime.runtimeai.get());
        NicowarStrategyLoader loader;ai.strategy=loader.getParticularStrategy("default");
        ai.war_preparation=true;ai.growth_phase=true;ai.starving_recovery=false;
        for(auto it=runtime.br.begin();it!=runtime.br.end();++it) {
            AISharedRuntime::Management::AddResourceTracker tracker(16,AISharedRuntime::Management::RecurringInputStock,it->first);tracker.modify(runtime);
        }
        ai.manage_buildings(runtime);
        int creates=0;for(const auto& order:runtime.orders) creates+=order->getOrderType()==ORDER_CREATE;
        CHECK(creates==(missing?0:1));
        CHECK(runtime.br.pending_buildings.size()==std::size_t(missing?0:1));
        CHECK_FALSE(runtime.management_orders.empty());
    }
}
TEST_CASE("six native strategies construct and use separate demanded production classes")
{
    glob2test::HeadlessGlobals globals;
    for(auto controller:{AI::NUMBI,AI::CASTOR,AI::WARRUSH,AI::ECONO,AI::NICOWAR,AI::CABINO}) {
        CAPTURE(controller);
        CatalogWorld fixture(controller,false,false,true);auto& game=fixture.world.game;
        auto& team=*game.teams[0];auto* implementation=game.players[0]->ai->aiImplementation;
        game.gameHeader.setHungerDisabled(true);game.gameHeader.setUnitUpgradesDisabled(true);
        game.stepCounter=50000;
        auto* runtime=dynamic_cast<AISharedRuntime::Runtime*>(implementation);
        if(runtime) {
            runtime->gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);
            runtime->br.initiate();
            if(controller==AI::NICOWAR) {
                auto& ai=*dynamic_cast<NewNicowar*>(runtime->runtimeai.get());
                NicowarStrategyLoader loader;ai.strategy=loader.getParticularStrategy("default");
                ai.war_preparation=true;ai.growth_phase=true;ai.starving_recovery=false;
                ai.initialize(*runtime);runtime->update_management_orders();
            }
        }
        unsigned created=0,configured=0;
        auto apply=[&](const std::shared_ptr<Order>& order) {
            if(!order) return;
            if(auto create=std::dynamic_pointer_cast<OrderCreate>(order)) {
                const auto& recipes=game.buildingsTypes.get(create->typeNum)->semantics.production.recipes;
                for(int unit=EXPLORER;unit<=WARRIOR;++unit) if(recipes[unit].enabled) created|=1u<<unit;
            }
            if(auto ratios=std::dynamic_pointer_cast<OrderModifySwarm>(order))
                for(int unit=EXPLORER;unit<=WARRIOR;++unit) if(ratios->ratio[unit]>0) configured|=1u<<unit;
            order->sender=0;game.executeOrder(order,0);
            for(int id=0;id<Building::MAX_COUNT;++id) if(auto* b=team.myBuildings[id];b && (game.buildingCapabilities().intentMask(b->typeNum)&7u))
                if(std::find(team.swarms.begin(),team.swarms.end(),b)==team.swarms.end()) team.addToStaticAbilitiesLists(b);
        };
        for(int turn=0;turn<20;++turn) {
            if(controller==AI::NUMBI) apply(dynamic_cast<AINumbi*>(implementation)->swarmsForWorkers(1,8,4,1,1));
            if(controller==AI::CASTOR) {
                auto& ai=*dynamic_cast<AICastor*>(implementation);ai.warLevel=1;apply(ai.controlSwarms());
            }
            if(controller==AI::WARRUSH) {
                auto& ai=*dynamic_cast<AIWarrush*>(implementation);ai.buildingDelay=1000;ai.areaUpdatingDelay=1000;apply(ai.getOrder());
            }
            if(controller==AI::CABINO) {
                auto& ai=*dynamic_cast<Cabino::AICabino*>(implementation);
                auto& manager=*dynamic_cast<Cabino::BasicDistributedSwarmManager*>(ai.unit_module);
                manager.module_records.clear();
                for(int unit=0;unit<NB_UNIT_TYPE;++unit) manager.module_records["fixture demand"].requested[unit][HARVEST][0]=100;
                manager.moderateSwarms();
                while(!ai.orders.empty()) {auto order=ai.orders.front();ai.orders.pop();apply(order);}
            }
            if(runtime) {
                runtime->br.tick();runtime->update_management_orders();
                if(controller==AI::ECONO) {
                    auto& ai=*dynamic_cast<AISharedRuntime::Econo*>(runtime->runtimeai.get());
                    ai.timer=AISharedRuntime::AI_SHARED_RUNTIME_RTI_SWARM_OFFSET_TICKS;ai.tick_swarms_near_wheat(*runtime);
                } else dynamic_cast<NewNicowar*>(runtime->runtimeai.get())->manage_buildings(*runtime);
                runtime->update_management_orders();
                auto orders=std::move(runtime->orders);runtime->orders.clear();for(const auto& order:orders) apply(order);
            }
        }
        const unsigned expected=controller==AI::ECONO ? 1u<<EXPLORER : (1u<<EXPLORER)|(1u<<WARRIOR);
        CHECK((created&expected)==expected);CHECK((configured&expected)==expected);
        for(int unit=EXPLORER;unit<=WARRIOR;++unit) if(expected&(1u<<unit)) {
            const auto before=team.stats.measurements.births[unit];
            for(auto* producer:team.swarms) if(producer->type->semantics.production.recipes[unit].enabled) {
                REQUIRE(producer->ratio[unit]>0);
                for(int step=0;step<3;++step) producer->swarmStep();
            }
            CHECK(team.stats.measurements.births[unit]>before);
        }
    }
}
TEST_CASE("shared runtime exact anchors support rectangular attractors across the torus")
{
    glob2test::HeadlessGlobals globals;
    CatalogWorld fixture(AI::NICOWAR,false,false,false,[](auto& snapshot) {
        for(auto& variant:snapshot["variants"]) if(variant["key"]=="warflag.0.finished") {
            variant["properties"]["width"]=2;variant["properties"]["height"]=3;
        }
    });
    auto& game=fixture.world.game;
    const int flag=game.buildingsTypes.findByKey("warflag.0.finished");
    REQUIRE(flag>=0);
    const auto* type=game.buildingsTypes.get(flag);
    CHECK(game.buildingsTypes.getRuntime(flag)->width==2);CHECK(game.buildingsTypes.getRuntime(flag)->height==3);
    auto& runtime=*dynamic_cast<AISharedRuntime::Runtime*>(game.players[0]->ai->aiImplementation);
    runtime.gm=std::make_unique<AISharedRuntime::Gradients::GradientManager>(&game.map);
    REQUIRE(game.checkRoomForBuilding(63,62,type,0));
    using namespace AISharedRuntime::Construction;
    BuildingOrder order(runtime,AISharedRuntime::BuildingDemand::AttractWarriors,2);
    order.add_constraint(new SinglePosition(127,-2));
    const auto placed=order.find_location(runtime,&game.map,*runtime.gm);
    CHECK(order.get_concrete_type()==flag);
    CHECK(placed.x==63);CHECK(placed.y==62);
    BuildingOrder conflicting(runtime,AISharedRuntime::BuildingDemand::AttractWarriors,2);
    conflicting.add_constraint(new SinglePosition(127,-2));
    conflicting.add_constraint(new SinglePosition(2,62));
    const auto absent=conflicting.find_location(runtime,&game.map,*runtime.gm);
    CHECK(absent.x==-1);CHECK(absent.y==-1);
}
TEST_CASE("Maxima exact anchors support rectangular footprints across the torus")
{
    glob2test::HeadlessGlobals globals;
    CatalogWorld fixture(AI::MAXIMA,false,false,false,[](auto& snapshot) {
        for(auto& variant:snapshot["variants"]) if(variant["key"]=="hospital.0.site" || variant["key"]=="hospital.0.finished") {
            variant["properties"]["width"]=4;variant["properties"]["height"]=2;
        }
    });
    auto& game=fixture.world.game;
    const auto* type=game.buildingsTypes.get(fixture.replacement);
    CHECK(game.buildingsTypes.getRuntime(fixture.replacement)->width==4);CHECK(game.buildingsTypes.getRuntime(fixture.replacement)->height==2);
    auto& ai=*dynamic_cast<AIMaxima::Maxima*>(game.players[0]->ai->aiImplementation);
    REQUIRE(game.checkRoomForBuilding(62,60,type,0));
    AIMaximaRuntime::Construction::BuildingOrder order(AIMaximaBuildings::Feeding,2);
    order.add_constraint(new AIMaximaRuntime::Construction::SinglePosition(126,-4));
    bool complete=false;
    const auto placed=order.find_location(ai.context,1,complete);
    CHECK(complete);REQUIRE(placed.found);
    CHECK(placed.value.x==62);CHECK(placed.value.y==60);
    CHECK(order.concreteType==fixture.replacement);
    AIMaximaRuntime::Construction::BuildingOrder conflicting(AIMaximaBuildings::Feeding,2);
    conflicting.add_constraint(new AIMaximaRuntime::Construction::SinglePosition(126,-4));
    conflicting.add_constraint(new AIMaximaRuntime::Construction::SinglePosition(2,60));
    CHECK_FALSE(conflicting.find_location(ai.context,1,complete).found);
    CHECK(complete);
}
TEST_CASE("all native controllers continue deterministically with mixed permuted catalog")
{
    glob2test::HeadlessGlobals globals;
    for(auto id:Controllers) {
        CAPTURE(id);CatalogWorld fixture(id,false,true);auto& game=fixture.world.game;
        for(int tick=0;tick<96;++tick)glob2test::stepAI(game);
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);game.save(&output,false,"mixed catalog continuation");output.flush();
        const auto bytes=memory->takeContents();
        std::vector<std::vector<std::string>> orders;
        std::vector<std::vector<Uint32>> checksums;
        std::vector<MersenneTwister> random;
        for(int tick=0;tick<64;++tick) {
            orders.push_back(glob2test::stepAI(game));checksums.push_back(state(game));
            random.push_back(game.players[0]->ai->aiImplementation->snapshotRandom());
        }
        auto restored=std::make_unique<GameGUI>(false);
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
        REQUIRE(restored->game.load(&input));restored->game.setWaitingOnMask(0);
        CHECK(restored->game.buildingsTypes.fingerprint()==game.buildingsTypes.fingerprint());
        for(int tick=0;tick<64;++tick) {
            CAPTURE(tick);
            CHECK(glob2test::stepAI(restored->game)==orders[tick]);
            CHECK(state(restored->game)==checksums[tick]);
            CHECK(restored->game.players[0]->ai->aiImplementation->snapshotRandom()==random[tick]);
        }
    }
}
TEST_CASE("Castor projects only its strategic demands and preserves mixed staffing")
{
    glob2test::HeadlessGlobals globals;
    for(int service=0;service<4;++service) {
        CAPTURE(service);
        CatalogWorld fixture(AI::CASTOR,false,false,false,[&](nlohmann::json& snapshot) {
            for(auto& variant:snapshot["variants"]) {
                const auto key=variant["key"].get<std::string>();
                if(key=="hospital.0.site") {
                    variant["semantics"]["assignmentLimit"]=3;
                    variant["presentation"]["defaultAssigned"]=2;
                }
                if(key!="hospital.0.finished" && key!="hospital.1.finished")continue;
                auto& semantics=variant["semantics"];
                semantics["feeding"]["enabled"]=false;
                semantics["healing"]["enabled"]=false;
                semantics["training"]=nlohmann::json::object();
                semantics["production"]["recipes"]=nlohmann::json::object();
                semantics["assignmentLimit"]=6;
                semantics["admittedUnitMask"]=7;
                variant["properties"]["maxUnitInside"]=4;
                const auto training=nlohmann::json{{"enabled",true},{"unitMask",7},{"targetLevel",1},{"duration",1},{"cost",nlohmann::json::object()}};
                if(key=="hospital.1.finished" || service==2) {
                    semantics["training"]["walk"]=training;
                    semantics["training"]["walk"]["constructionLevel"]=1;
                } else if(service==0) {
                    const auto recipe=nlohmann::json{{"enabled",true},{"duration",1},{"cost",nlohmann::json::object()}};
                    semantics["production"]["recipes"]["worker"]=recipe;
                    semantics["production"]["recipes"]["explorer"]=recipe;
                } else if(service==1) {
                    const auto visit=nlohmann::json{{"enabled",true},{"unitMask",7},{"duration",1},{"cost",nlohmann::json::object()}};
                    semantics["feeding"]=visit;
                    semantics["healing"]=visit;
                } else {
                    semantics["training"]["armor"]=training;
                    semantics["training"]["build"]=training;
                }
            }
        });
        auto& game=fixture.world.game;
        auto& ai=*dynamic_cast<AICastor*>(game.players[0]->ai->aiImplementation);
        ai.computeBuildingSum();
        int initial[AICastor::DemandCount][2][NB_UNIT_LEVELS];
        for(int demand=0;demand<AICastor::DemandCount;++demand)
            for(int site=0;site<2;++site)for(int stage=0;stage<NB_UNIT_LEVELS;++stage)
                initial[demand][site][stage]=ai.buildingLevels[demand][site][stage];
        auto* finished=game.addBuilding(20,4,fixture.completed,0,4,4);
        REQUIRE(finished);
        finished->maxUnitWorking=4;
        ai.computeBuildingSum();
        for(int demand=0;demand<AICastor::DemandCount;++demand) {
            const bool expected=(service==0 && demand==AICastor::ProduceWorkers)
                || (service==1 && (demand==AICastor::FeedUnits || demand==AICastor::HealUnits))
                || (service==2 && (demand==AICastor::TrainWalking || demand==AICastor::TrainConstruction));
            CHECK(ai.buildingLevels[demand][0][0]-initial[demand][0][0]==int(expected));
        }
        // Additional production classes and unrelated training do not turn one
        // strategic demand into several; mixed Castor demands retain staffing.
        CHECK(ai.desiredWorkers(*finished,1)==((service==1 || service==2) ? 4 : 1));
        CHECK(ai.desiredWorkers(*finished,9)==6);
        auto* site=game.addBuilding(25,4,fixture.replacement,0,2,2);
        REQUIRE(site);
        CHECK(ai.desiredWorkers(*site,9)==3);
        if(service==1) {
            // An upgrade is counted at its explicit target stage. Staffing still
            // refers to the current feed+heal provider until that transition.
            finished->buildingState=Building::WAITING_FOR_CONSTRUCTION;
            finished->constructionResultState=Building::UPGRADE;
            ai.computeBuildingSum();
            CHECK(ai.buildingLevels[AICastor::FeedUnits][0][0]==initial[AICastor::FeedUnits][0][0]);
            CHECK(ai.buildingLevels[AICastor::HealUnits][0][0]==initial[AICastor::HealUnits][0][0]);
            CHECK(ai.buildingLevels[AICastor::TrainWalking][1][1]==1);
            CHECK(ai.buildingLevels[AICastor::TrainConstruction][1][1]==1);
            CHECK(ai.desiredWorkers(*finished,1)==4);
            finished->buildingState=Building::ALIVE;
            finished->constructionResultState=Building::NO_CONSTRUCTION;
        }
    }
}
TEST_CASE("Warrush staffing preserves intent priority slot order and current stage limits")
{
    glob2test::HeadlessGlobals globals;
    CatalogWorld fixture(AI::WARRUSH,false,false,true,[](nlohmann::json& snapshot) {
        nlohmann::json feeding;
        for(const auto& variant:snapshot["variants"])
            if(variant["key"]=="hospital.0.finished") feeding=variant["semantics"]["feeding"];
        for(auto& variant:snapshot["variants"]) {
            const auto key=variant["key"].get<std::string>();
            if(key=="hospital.0.finished") {
                variant["semantics"]["feeding"]["enabled"]=false;
                variant["semantics"]["assignmentLimit"]=1;
                variant["presentation"]["defaultAssigned"]=1;
            }
            if(key=="hospital.0.site") variant["semantics"]["assignmentLimit"]=2;
            if(key=="swarm.0.site") {
                variant["semantics"]["assignmentLimit"]=4;
                variant["presentation"]["defaultAssigned"]=4;
            }
            if(key=="inn.0.finished") variant["semantics"]["feeding"]=feeding;
        }
    });
    auto& game=fixture.world.game;
    auto& ai=*dynamic_cast<AIWarrush*>(game.players[0]->ai->aiImplementation);
    game.teams[0]->myBuildings[0]->maxUnitWorking=5;
    const auto add=[&](const char* key,int x,int y) {
        auto* building=game.addBuilding(x,y,game.buildingsTypes.findByKey(key),0,0,0);
        REQUIRE(building);return building;
    };
    const auto selected=[&](Building* building,int requested) {
        auto order=std::dynamic_pointer_cast<OrderModifyBuilding>(ai.staffingOrder());
        REQUIRE(order);CHECK(order->gid==building->gid);CHECK(order->numberRequested==requested);
    };
    auto* explorer=add("hospital.0.site",14,4);
    auto* worker=add("swarm.0.site",24,4);
    auto* secondWorker=add("swarm.0.site",14,14);
    auto* secondExplorer=add("hospital.0.site",24,14);
    // Worker intent wins over the lower-slot explorer; within one intent the
    // first slot wins. Requests use the site's cap, not its completion's cap.
    selected(worker,4);
    worker->maxUnitWorking=4;selected(secondWorker,4);
    secondWorker->maxUnitWorking=4;selected(explorer,2);
    explorer->maxUnitWorking=2;selected(secondExplorer,2);
    secondExplorer->maxUnitWorking=2;
    auto* finishedExplorer=add("hospital.0.finished",14,24);
    auto* finishedWarrior=add("barracks.0.finished",24,24);
    auto* trainingSite=add("barracks.1.site",34,14);
    auto* feeder=add("inn.0.finished",34,24);
    // Completed explorer/warrior staffing remains outside this strategy's
    // existing policy; feeding precedes a pure attack-training construction.
    CHECK(finishedExplorer->maxUnitWorking==0);
    CHECK(finishedWarrior->maxUnitWorking==0);
    selected(feeder,3);feeder->maxUnitWorking=3;
    selected(trainingSite,3);trainingSite->maxUnitWorking=3;
    CHECK(ai.staffingOrder()->getOrderType()==ORDER_NULL);
}

TEST_CASE("Warrush mixed producer staffing respects zero and saturated assignment limits")
{
    glob2test::HeadlessGlobals globals;
    for(int limit:{0,2}) {
        CAPTURE(limit);
        CatalogWorld fixture(AI::WARRUSH,false,false,false,[&](nlohmann::json& snapshot) {
            nlohmann::json feeding;
            for(const auto& variant:snapshot["variants"])
                if(variant["key"]=="hospital.0.finished") feeding=variant["semantics"]["feeding"];
            for(auto& variant:snapshot["variants"]) if(variant["key"]=="racetrack.0.finished") {
                auto& semantics=variant["semantics"];
                semantics["assignmentLimit"]=limit;
                variant["presentation"]["defaultAssigned"]=limit;
                semantics["feeding"]=feeding;
                semantics["production"]["scheduling"]="weighted_committed_job";
                const nlohmann::json recipe={{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}};
                semantics["production"]["recipes"]["worker"]=recipe;
                semantics["production"]["recipes"]["explorer"]=recipe;
            }
        });
        auto& game=fixture.world.game;
        auto& ai=*dynamic_cast<AIWarrush*>(game.players[0]->ai->aiImplementation);
        game.teams[0]->myBuildings[0]->maxUnitWorking=5;
        auto* hybrid=game.addBuilding(14,4,game.buildingsTypes.findByKey("racetrack.0.finished"),0,0,0);
        REQUIRE(hybrid);
        auto order=ai.staffingOrder();
        if(limit==0) CHECK(order->getOrderType()==ORDER_NULL);
        else {
            auto staffing=std::dynamic_pointer_cast<OrderModifyBuilding>(order);
            REQUIRE(staffing);CHECK(staffing->gid==hybrid->gid);CHECK(staffing->numberRequested==limit);
        }
        hybrid->maxUnitWorking=limit;
        CHECK(ai.staffingOrder()->getOrderType()==ORDER_NULL);
    }
}
}
