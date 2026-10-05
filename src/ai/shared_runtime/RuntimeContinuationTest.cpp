// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <string>
#include <memory>
#include <iostream>
#include "GlobalContainer.h"
#include "FileManager.h"
#include "shared_runtime/BuildingDemands.h"
#include "Game.h"
#include "Version.h"
#include "shared_runtime/Runtime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>

// SDL compiler flags may rename main even when SDL_MAIN_HANDLED is set.
using namespace AISharedRuntime::Gradients;
using namespace GAGCore;

class RuntimeContinuationTest
{
    static GradientInfo info(Entities::Entity* source)
    {
        GradientInfo result;result.add_source(source);return result;
    }
    static std::string save(GradientManager& manager,bool text)
    {
        auto* backend=new MemoryStreamBackend;
        std::unique_ptr<OutputStream> out(text ? static_cast<OutputStream*>(new TextOutputStream(backend))
                                              : static_cast<OutputStream*>(new BinaryOutputStream(backend)));
        manager.save(out.get());out->flush();
        return backend->takeContents();
    }
    static bool load(GradientManager& manager,const std::string& bytes,bool text)
    {
        auto* backend=new MemoryStreamBackend;
        backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
        std::unique_ptr<InputStream> in(text ? static_cast<InputStream*>(new TextInputStream(backend))
                                           : static_cast<InputStream*>(new BinaryInputStream(backend)));
        return manager.load(in.get(),nullptr,VERSION_MINOR);
    }
    class QuietAI : public AISharedRuntime::RuntimeAI
    {
    public:
        bool load(InputStream*,Player*,Sint32) override {return true;}
        void save(OutputStream*) override {}
        void tick(AISharedRuntime::Runtime&) override {}
        void handle_message(AISharedRuntime::Runtime&,const std::string&) override {}
    };
    static AISharedRuntime::Runtime& runtime(Game& game,int i)
    {
        return *dynamic_cast<AISharedRuntime::Runtime*>(game.players[i]->ai->aiImplementation);
    }
    static void setup(Game& game)
    {
        game.map.setSize(5,5,GRASS);game.map.setGame(&game);
        game.gameHeader.setNumberOfPlayers(2);
        for(int i=0;i<2;++i)
        {
            game.addTeam();game.teams[i]->race.loadDefault();
            auto* player=new Player(i,"test",game.teams[i],BasePlayer::playerTypeFromImplementationID(AI::NICOWAR));
            game.players[i]=player;
            delete player->ai->aiImplementation;
            player->ai->aiImplementation=new AISharedRuntime::Runtime(new QuietAI,player);
        }
    }
    static std::string saveRuntime(AISharedRuntime::Runtime& value)
    {
        auto* backend=new MemoryStreamBackend;
        BinaryOutputStream out(backend);value.save(&out);out.flush();
        return backend->takeContents();
    }
    static void independentManagers()
    {
        Game source(nullptr),target(nullptr);setup(source);setup(target);
        // Each controller owns its cache, regardless of poll order.
        runtime(source,1).getOrder();runtime(source,0).getOrder();
        auto& first=runtime(source,0).get_gradient_manager();
        auto& second=runtime(source,1).get_gradient_manager();
        REQUIRE((&first!=&second));
        first.get_gradient(info(new Entities::Resource(WHEAT)));
        first.queue_gradient(info(new Entities::Water));
        REQUIRE(second.gradients.empty());
        for(int i=0;i<2;++i)
        {
            const auto bytes=saveRuntime(runtime(source,i));
            auto* backend=new MemoryStreamBackend;
            backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
            BinaryInputStream in(backend);
            REQUIRE(runtime(target,i).load(&in,target.players[i],VERSION_MINOR));
            REQUIRE(saveRuntime(runtime(target,i))==bytes);
        }
        REQUIRE((&runtime(target,0).get_gradient_manager()!=&runtime(target,1).get_gradient_manager()));
        for(int tick=0;tick<5;++tick)
            for(int i=0;i<2;++i)
            {
                runtime(source,i).getOrder();runtime(target,i).getOrder();
                REQUIRE(saveRuntime(runtime(source,i))==saveRuntime(runtime(target,i)));
            }
        // An older shared manager must become a complete, independent copy
        // when a later runtime player references its previous owner.
        auto legacyCopy=first.clone();
        REQUIRE(save(*legacyCopy,false)==save(first,false));
        REQUIRE(legacyCopy->gradients[0]!=first.gradients[0]);
        REQUIRE(legacyCopy->gradients[0]->gradient_info.sources[0]!=
            first.gradients[0]->gradient_info.sources[0]);
        const auto originalFirst=save(first,false);
        legacyCopy->update();
        REQUIRE(save(first,false)==originalFirst);
    }
    static void completedTransitionsReleaseWaits()
    {
        Game game(nullptr);
        const int initial=game.buildingsTypes.getTypeNum("inn",0,false);
        const int site=game.buildingsTypes.get(initial)->nextLevel;
        REQUIRE(site>=0);
        const int destination=game.buildingsTypes.get(site)->nextLevel;
        REQUIRE(destination>=0);
        auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
        snapshot["variants"][initial]["properties"]["level"]=3;
        snapshot["variants"][site]["properties"]["level"]=0;
        snapshot["variants"][destination]["properties"]["level"]=0;
        // These are key-only variants: presentation levels need not form a
        // unique legacy family/level tuple.
        for(auto& variant:snapshot["variants"]) variant["properties"]["type"]="";
        game.buildingsTypes.loadSnapshotJson(snapshot.dump());game.configureBuildingCatalog();
        setup(game);
        auto* building=game.addBuilding(4,4,initial,0);
        REQUIRE(building);
        auto& registry=runtime(game,0).get_building_register();
        registry.initiate();
        REQUIRE(registry.get_building(0)==building);
        registry.set_upgrading(0);
        CHECK(registry.is_building_upgrading(0));
        AISharedRuntime::Conditions::ParticularBuilding firstStage(new AISharedRuntime::Conditions::BuildingLevel(1),0);
        AISharedRuntime::Conditions::ParticularBuilding nextStage(new AISharedRuntime::Conditions::BeingUpgradedTo(2),0);
        CHECK(bool(firstStage.passes(runtime(game,0))));
        CHECK(bool(nextStage.passes(runtime(game,0))));
        // A short transition may complete between the controller's observations.
        building->bindType(destination);
        registry.tick();
        CHECK_FALSE(registry.is_building_upgrading(0));
        CHECK(registry.get_type(0)==destination);
        CHECK(registry.get_level(0)==2);
        AISharedRuntime::Conditions::ParticularBuilding secondStage(new AISharedRuntime::Conditions::BuildingLevel(2),0);
        CHECK(bool(secondStage.passes(runtime(game,0))));
        REQUIRE(building->isUpgradeAvailable());
        registry.set_upgrading(0);
        CHECK(registry.is_building_upgrading(0));
        // An instant repair retains this variant and its next upgrade edge.
        registry.tick();
        CHECK_FALSE(registry.is_building_upgrading(0));
        CHECK(registry.get_type(0)==destination);
    }
    static void placementInputsUseConstructionPrice()
    {
        Game game(nullptr);
        const int site=game.buildingsTypes.getTypeNum("inn",0,true);
        const int completed=game.buildingsTypes.get(site)->nextLevel;
        auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
        snapshot["variants"][site]["semantics"]["constructionCost"]={{"stone",5}};
        auto& storage=snapshot["variants"][site]["properties"]["maxResource"];
        for(auto& value:storage) value=0;
        storage[WOOD]=9;
        snapshot["variants"][completed]["semantics"]["feeding"]["cost"]=nlohmann::json::object();
        game.buildingsTypes.loadSnapshotJson(snapshot.dump());game.configureBuildingCatalog();
        CHECK(game.buildingsTypes.get(completed)->semantics.feeding.costMask==0);
        setup(game);
        auto& controller=runtime(game,0);MersenneTwister random(713);controller.setRandomEngine(random);
        AISharedRuntime::Construction::BuildingOrder order(controller,AISharedRuntime::BuildingDemand::Feed,2);
        CHECK(order.input_resource_mask(controller)==(1u<<STONE));
    }
    static void terrainTravelContinuation()
    {
        {
            Map neutral; neutral.setSize(4,4,GRASS);
            auto diagonal=info(new Entities::Position(0,0));
            diagonal.terrainTravel=field::TerrainTravel::Walk;
            GradientManager manager(&neutral);
            REQUIRE(manager.get_gradient(diagonal).get_height(3,3)==3);
            neutral.setCellTerrain(8,8,TRAIL);
            REQUIRE(manager.get_gradient(diagonal).get_height(3,3)==3);
        }
        Game game(nullptr); game.map.setSize(4,4,WATER); game.map.setGame(&game);
        auto& map=game.map;
        for(int x=0;x<16;++x) map.setCellTerrain(x,1,GRASS);
        auto walking=info(new Entities::Position(0,1));
        walking.terrainTravel=field::TerrainTravel::Walk;
        auto geometry=info(new Entities::Position(0,1));
        GradientManager original(&map);
        REQUIRE(original.get_gradient(walking).get_height(4,1)==4);
        REQUIRE(original.get_gradient(geometry).get_height(4,1)==4);
        for(int x=0;x<16;++x) map.setCellTerrain(x,1,TRAIL);
        for(bool text:{false,true})
        {
            GradientManager restored(&map);
            const auto bytes=save(original,text);
            REQUIRE(load(restored,bytes,text));
            REQUIRE(save(restored,text)==bytes);
            REQUIRE(!restored.is_updated(walking));
            REQUIRE(restored.get_gradient(walking).get_height(4,1)==2);
            REQUIRE(restored.get_gradient(geometry).get_height(4,1)==4);
            auto copy=restored.clone();
            REQUIRE(save(*copy,text)==save(restored,text));
        }
        REQUIRE(original.get_gradient(walking).get_height(4,1)==2);
    }

public:
    static void run()
    {
        Game game(nullptr);game.map.setSize(5,5,GRASS);game.map.setGame(&game);
        Map& map=game.map;
        for(bool text:{false,true})
        {
            map.setResource(3,4,WHEAT,5);
            GradientManager original(&map),restored(&map);
            const auto wheat=info(text ? static_cast<Entities::Entity*>(new Entities::AnyResource)
                                       : static_cast<Entities::Entity*>(new Entities::Resource(WHEAT)));
            const auto water=info(new Entities::Water);
            auto avoidsIrrigation=info(new Entities::Position(0,0));
            avoidsIrrigation.add_obstacle(new Entities::Water);
            original.get_gradient(wheat);
            original.get_gradient(avoidsIrrigation);
            const auto resources=info(new Entities::ResourceSet((1u<<WHEAT)|(1u<<WOOD)));
            original.get_gradient(resources);
            // Save a stale field, an uncomputed queued field and duplicate
            // queue entries. Reload must retain their age/order, not rebuild.
            original.ticks_since_update[0]=151;
            original.queue_gradient(water);
            original.queue_gradient(wheat);
            original.queue_gradient(wheat);
            original.timer=937;
            map.setNoResource(3,4,0);
            const auto bytes=save(original,text);
            REQUIRE(load(restored,bytes,text));
            REQUIRE(save(restored,text)==bytes);
            REQUIRE(restored.gradients[0]->get_height(3,4)==0);
            REQUIRE(!restored.is_updated(wheat));
            for(int tick=0;tick<5;++tick)
            {
                original.update();restored.update();
                REQUIRE(save(original,text)==save(restored,text));
                REQUIRE(original.is_updated(wheat)==restored.is_updated(wheat));
            }
            GradientManager empty(&map),emptyRestored(&map);
            REQUIRE(load(emptyRestored,save(empty,text),text));
            REQUIRE(save(emptyRestored,text)==save(empty,text));
            original.queuedGradients.push(1000);
            GradientManager badQueue(&map);
            REQUIRE(!load(badQueue,save(original,text),text));
        }
        independentManagers();
        completedTransitionsReleaseWaits();
        placementInputsUseConstructionPrice();
        terrainTravelContinuation();
        std::cout<<"Runtime gradient continuation: independent managers, binary/text fields, stale ages, queued work and invalid indices PASS\n";
    }
};
TEST_SUITE("RuntimeContinuation")
{
	TEST_CASE("gradient manager state survives binary and text round trips [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		RuntimeContinuationTest::run();
	}
}

TEST_CASE("terrain weighted influence retains obstacles and sub-tile trail costs" *
          doctest::test_suite("RuntimeContinuation"))
{
    // Isolate a corridor from toroidal shortcuts; two trail entries cost one
    // strength unit, while one ice entry costs two.
    std::vector<unsigned char> values(64,0);
    std::vector<TerrainType> terrain(64,GRASS);
    for(int x=1;x<=6;++x)values[8+x]=1;
    values[9]=20;terrain[10]=terrain[11]=TRAIL;terrain[12]=ICE;
    field::expandTerrainInfluence(values.data(),8,8,[&](size_t i){return terrain[i];});
    CHECK(values[9]==20);CHECK(values[10]==19);CHECK(values[11]==19);
    CHECK(values[12]==17);CHECK(values[13]==16);CHECK(values[8]==0);
}

TEST_CASE("irrigation heuristics require a positive enabled fertility contribution" *
          doctest::test_suite("RuntimeContinuation"))
{
    auto source=terrainProperties(WATER);
    CHECK(terrainProvidesFertility(source));
    source.fertilityQ8=-256;CHECK_FALSE(terrainProvidesFertility(source));
    source.fertilityQ8=0;CHECK_FALSE(terrainProvidesFertility(source));
    source.fertilityQ8=256;source.fertilitySource=false;
    CHECK_FALSE(terrainProvidesFertility(source));
}

TEST_CASE("position entities retain historical binary and text payloads" *
          doctest::test_suite("RuntimeContinuation"))
{
    struct SavedPosition : Entities::Position
    {
        using Position::Position;
        using Position::save;
        using Position::load;
        bool matches(int x,int y) { return is_entity(nullptr,x,y); }
    };
    for(bool text:{false,true}) for(int version:{133,VERSION_MINOR})
    {
        SavedPosition original(7,13),restored(0,0);
        auto* memory=new MemoryStreamBackend;
        std::unique_ptr<OutputStream> output(text?static_cast<OutputStream*>(new TextOutputStream(memory)):
            static_cast<OutputStream*>(new BinaryOutputStream(memory)));
        original.save(output.get());output->writeUint32(0xabc123,"sentinel");output->flush();
        const auto bytes=memory->takeContents();
        auto* source=new MemoryStreamBackend(bytes.data(),bytes.size());source->seekFromStart(0);
        std::unique_ptr<InputStream> input(text?static_cast<InputStream*>(new TextInputStream(source)):
            static_cast<InputStream*>(new BinaryInputStream(source)));
        CHECK(restored.load(input.get(),nullptr,version));
        CHECK(restored.matches(7,13));CHECK_FALSE(restored.matches(7,0));
        CHECK(input->readUint32("sentinel")==0xabc123);
    }
}
