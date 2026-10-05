// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
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
    explicit CatalogWorld(AI::ImplementationID ai,bool missing=false,bool populatedProvider=false)
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
        }
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
            game.teams[team]->stats.step(game.teams[team]);
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
TEST_CASE("Maxima exact anchors support rectangular footprints across the torus")
{
    glob2test::HeadlessGlobals globals;
    CatalogWorld fixture(AI::MAXIMA);
    auto& game=fixture.world.game;
    auto* type=game.buildingsTypes.get(fixture.replacement);
    type->width=4;type->height=2;
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
}
