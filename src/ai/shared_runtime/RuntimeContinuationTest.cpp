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
#include "ai/observation/AIWorldView.h"
#include "shared_runtime/Runtime.h"
#include "AIMaximaRuntime.h"
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
        AISharedRuntime::Runtime::OwnerObservationScope source0(runtime(source,0)),source1(runtime(source,1));
        AISharedRuntime::Runtime::OwnerObservationScope target0(runtime(target,0)),target1(runtime(target,1));
        // Each controller owns its cache, regardless of poll order.
        runtime(source,1).getOrder();runtime(source,0).getOrder();
        auto& first=runtime(source,0).get_gradient_manager();
        auto& second=runtime(source,1).get_gradient_manager();
        REQUIRE((&first!=&second));
        first.get_gradient(info(new Entities::MaterialSource(WHEAT)));
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
    static void snapshotGradientContinuation()
    {
        Game game(nullptr);setup(game);
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime(game,0));
        game.map.setResourceByIndex(4,5,WHEAT,3);
        game.map.setCellTerrain(7,5,TRAIL);
        auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
        GradientManager live(&game.map),snapshot(*world);
        GradientInfo wheat=info(new Entities::MaterialSource(WHEAT));
        wheat.terrainTravel=field::TerrainTravel::Walk;
        live.queue_gradient(wheat);snapshot.queue_gradient(wheat);
        for(int tick=0;tick<160;++tick) {
            live.update();snapshot.update();
            REQUIRE(save(live,false)==save(snapshot,false));
            REQUIRE(live.is_updated(wheat)==snapshot.is_updated(wheat));
            if(tick%11==0) {
                live.get_gradient(wheat);snapshot.get_gradient(wheat);
                REQUIRE(save(live,false)==save(snapshot,false));
            }
        }
        for(bool text:{false,true}) {
            GradientManager restored(*world);
            REQUIRE(load(restored,save(snapshot,text),text));
            for(int tick=0;tick<5;++tick) {
                live.update();snapshot.update();restored.update();
                REQUIRE(save(snapshot,text)==save(restored,text));
            }
        }
        // Terrain may change after the last AI poll, before owner save. The
        // comparison binds current metadata without recomputing the old field.
        game.map.setCellTerrain(6,5,ICE);
        auto changed=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
        snapshot.bindWorld(*changed);
        REQUIRE(save(live,false)==save(snapshot,false));
        for(bool text:{false,true}) {
            GradientManager restored(*changed);
            REQUIRE(load(restored,save(snapshot,text),text));
            REQUIRE(restored.get_gradient(wheat).get_height(6,5)==snapshot.get_gradient(wheat).get_height(6,5));
            REQUIRE(save(restored,text)==save(snapshot,text));
        }
        snapshot.unbindWorld();
        // Saving uses cached metadata and field state, never an expired lease.
        CHECK_FALSE(save(snapshot,false).empty());
    }
    static void snapshotMapQueries()
    {
        Game game(nullptr);setup(game);
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime(game,0));
        game.map.setResourceByIndex(4,5,WHEAT,3);
        game.map.setResourceByIndex(6,7,WOOD,0);
        game.map.addForbidden(4,5,0);
        game.map.addClearArea(4,5,0);
        game.map.setMapDiscovered(4,5,game.teams[0]->me);
        const int type=game.buildingsTypes.getTypeNum("inn",0,false);
        REQUIRE(game.addBuilding(12,12,type,0));
        const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
        AISharedRuntime::SearchTools::MapInfo live(runtime(game,0));
        AISharedRuntime::SearchTools::MapInfo snapshot(*world,game.teams[0]->me);
        std::vector<GradientInfo> predicates;
        predicates.push_back(info(new Entities::Building(type,0,false)));
        predicates.push_back(info(new Entities::AnyTeamBuilding(0,false)));
        predicates.push_back(info(new Entities::AnyBuilding(false)));
        predicates.push_back(info(new Entities::MaterialSource(WHEAT)));
        predicates.push_back(info(new Entities::AnyResource));
        predicates.push_back(info(new Entities::MaterialSources((1u<<WHEAT)|(1u<<WOOD))));
        predicates.push_back(info(new Entities::MaterialSources));
        predicates.push_back(info(new Entities::Water));
        predicates.push_back(info(new Entities::Sand));
        predicates.push_back(info(new Entities::Unwalkable));
        predicates.push_back(info(new Entities::Position(4,5)));
        for(int y=0;y<game.map.getH();++y) for(int x=0;x<game.map.getW();++x) {
            CHECK(snapshot.is_resource(x,y)==live.is_resource(x,y));
            CHECK(snapshot.is_resource(x,y,WHEAT)==live.is_resource(x,y,WHEAT));
            CHECK(snapshot.is_forbidden_area(x,y)==live.is_forbidden_area(x,y));
            CHECK(snapshot.is_clearing_area(x,y)==live.is_clearing_area(x,y));
            CHECK(snapshot.is_discovered(x,y)==live.is_discovered(x,y));
            CHECK(snapshot.can_paint_farm(x,y)==live.can_paint_farm(x,y));
            CHECK(snapshot.is_resource_habitat(x,y,WHEAT)==live.is_resource_habitat(x,y,WHEAT));
            CHECK(snapshot.backs_onto_sand(x,y)==live.backs_onto_sand(x,y));
            for(auto& predicate:predicates)
                CHECK(predicate.match_source(*world,x,y)==predicate.match_source(&game.map,x,y));
        }
        game.map.setNoResource(4,5,0);
        game.map.removeForbidden(4,5,0);
        // The direct owner adapter observes changes at its next borrow boundary.
        runtime(game,0).refreshOwnerObservation();
        CHECK(snapshot.is_resource(4,5,WHEAT));
        CHECK(snapshot.is_forbidden_area(4,5));
        CHECK_FALSE(live.is_resource(4,5,WHEAT));
        CHECK_FALSE(live.is_forbidden_area(4,5));
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
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime(game,0));
        auto* building=game.addBuilding(4,4,initial,0);
        REQUIRE(building);
        auto& registry=runtime(game,0).get_building_register();
        registry.initiate();
        REQUIRE(registry.get_building(0)->gid==building->gid);
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
        // The scheduler may hold a command for eight observations. None may
        // release the upgrade intent until execution has actually happened.
        registry.set_upgrading(0,true);
        for(int delayedTick=0;delayedTick<8;++delayedTick) {
            registry.tick();
            CHECK(registry.is_building_upgrading(0));
        }
        auto bytes=saveRuntime(runtime(game,0));
        auto* backend=new MemoryStreamBackend;
        backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
        BinaryInputStream input(backend);
        REQUIRE(runtime(game,0).load(&input,game.players[0],VERSION_MINOR));
        registry.tick();
        CHECK(registry.is_building_upgrading(0));
        OrderConstruction request(building->gid,1,1);
        runtime(game,0).orderExecutionCompleted(request,false);
        CHECK_FALSE(registry.is_building_upgrading(0));
        registry.set_upgrading(0,true);
        runtime(game,0).orderExecutionCompleted(request,true);
        registry.tick(); // accepted instant repair, same variant
        CHECK_FALSE(registry.is_building_upgrading(0));
    }
    static void placementInputsUseConstructionPrice()
    {
        Game game(nullptr);
        const int site=game.buildingsTypes.getTypeNum("inn",0,true);
        const int completed=game.buildingsTypes.get(site)->nextLevel;
        auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
        snapshot["variants"][site]["semantics"]["constructionCost"]={{"stone",5}};
        auto& storage=snapshot["variants"][site]["properties"]["maxMaterial"];
        for(auto& value:storage) value=0;
        storage[WOOD]=9;
        snapshot["variants"][completed]["semantics"]["feeding"]["cost"]=nlohmann::json::object();
        game.buildingsTypes.loadSnapshotJson(snapshot.dump());game.configureBuildingCatalog();
        CHECK(game.buildingsTypes.get(completed)->semantics.feeding.costMask==0);
        setup(game);
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime(game,0));
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
    static void queuedTargetContinuation()
    {
        Game game(nullptr);setup(game);
        const int type=game.buildingsTypes.getTypeNum("inn",0,false);
        auto* building=game.addBuilding(4,4,type,0);REQUIRE(building);
        auto& controller=runtime(game,0);
        controller.getOrder();
        const auto identity=Game::refOf(building);
        auto& registry=controller.get_building_register();
        registry.set_upgrading(0,true);
        OrderConstruction receipt(building->gid,1,1);
        receipt.aiSelectedTarget=BuildingRef{identity.gid,identity.generation+1};
        controller.orderExecutionCompleted(receipt,false);
        CHECK(registry.is_building_upgrading(0));
        receipt.aiSelectedTarget=identity;
        controller.orderExecutionCompleted(receipt,false);
        CHECK_FALSE(registry.is_building_upgrading(0));
        controller.push_order(std::make_shared<OrderModifyBuilding>(building->gid,3));
        const auto bytes=saveRuntime(controller);
        auto emitted=controller.getOrder();
        REQUIRE(emitted->getOrderType()==ORDER_MODIFY_BUILDING);
        REQUIRE(emitted->aiSelectedTarget);
        CHECK(emitted->aiSelectedTarget->generation==identity.generation);
        controller.push_order(std::make_shared<OrderModifyBuilding>(building->gid,3));
        // Keep the GID occupied, but expose a different incarnation before
        // emission. Both uninterrupted and restored private queues reject it.
        building->scriptIdentity=identity.generation+1;
        auto restore=[&] {
            BinaryInputStream input(new MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0);REQUIRE(controller.load(&input,game.players[0],VERSION_MINOR));
        };
        CHECK(controller.getOrder()->getOrderType()==ORDER_NULL);
        CHECK_FALSE(controller.get_building_register().is_building_found(0));
        restore();
        CHECK(controller.getOrder()->getOrderType()==ORDER_NULL);
        building->scriptIdentity=identity.generation;
        class QuietMaxima : public AIMaximaRuntime::RuntimeAI {
        public:
            void tick(AIMaximaRuntime::Context&) override {}
            void handle_event(AIMaximaRuntime::Context&,const AIMaximaRuntime::RuntimeEvent&) override {}
        } quiet;
        AIMaximaRuntime::Context maxima(game.players[0]);
        {
            auto observed=maxima.scopeOwnerObservation();
            maxima.getOrder(quiet);
        }
        maxima.push_order(std::make_shared<OrderModifyBuilding>(building->gid,3));
        auto* memory=new MemoryStreamBackend;
        BinaryOutputStream output(memory);
        {
            auto observed=maxima.scopeOwnerObservation();maxima.save(&output);
        }
        output.flush();const auto maximaBytes=memory->takeContents();
        building->scriptIdentity=identity.generation+1;
        {
            auto observed=maxima.scopeOwnerObservation();
            CHECK(maxima.getOrder(quiet)->getOrderType()==ORDER_NULL);
        }
        AIMaximaRuntime::Context resumed(game.players[0]);
        {
            auto observed=resumed.scopeOwnerObservation();
            BinaryInputStream input(new MemoryStreamBackend(maximaBytes.data(),maximaBytes.size()));input.seekFromStart(0);
            REQUIRE(resumed.load(&input,VERSION_MINOR));
            CHECK(resumed.getOrder(quiet)->getOrderType()==ORDER_NULL);
        }
        building->scriptIdentity=identity.generation;
    }
    static void staticMaterialSourceContinuation()
    {
        using Json=nlohmann::json;
        Game game(nullptr); game.map.setSize(5,5,GRASS); game.map.setGame(&game);
        auto& map=game.map;
        const auto stone=info(new Entities::MaterialSource(STONE));
        const auto food=info(new Entities::MaterialSource(WHEAT));
        const auto paper=info(new Entities::MaterialSource(PAPYRUS));
        CHECK_FALSE(stone.needs_updating(&map));
        CHECK(food.needs_updating(&map));
        CHECK(paper.needs_updating(&map));
        auto combined=info(new Entities::MaterialSources((1u<<STONE)|(1u<<WHEAT)));
        CHECK(combined.needs_updating(&map));
        auto amended=stone.clone();
        CHECK_FALSE(amended.needs_updating(&map));
        amended.add_obstacle(new Entities::MaterialSource(WHEAT));
        CHECK(amended.needs_updating(&map));

        // A high resource ID with an arbitrary name has the same static behavior.
        const auto rocks=*map.resourceRegistry().find("rocks");
        const auto prototype=Json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(rocks)];
        Json additions=Json::array();
        for(unsigned i=0;i<300;++i)
        {
            auto spec=prototype; spec["key"]="test:permanent-"+std::to_string(i);
            additions.push_back(std::move(spec));
        }
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",additions}}.dump());
        const auto custom=static_cast<ResourceId>(map.resourceRegistry().size()-1);
        REQUIRE(resourceIndex(custom)>255);
        CHECK_FALSE(stone.needs_updating(&map));
        map.setResource(3,4,custom,0);
        map.setMaterialAmount(map.coordToIndex(3,4),MaterialId::Stone,3);
        GradientManager original(&map);
        REQUIRE(original.get_gradient(stone).get_height(3,4)==0);
        original.ticks_since_update[0]=151;
        original.queue_gradient(stone);
        CHECK(original.queuedGradients.empty());
        CHECK(original.is_updated(stone));
        const auto generation=map.staticMaterialSourceGeneration();
        REQUIRE(map.takeHarvest(2,4,1,0,MaterialId::Stone,0));
        CHECK(map.materialAmountAt(map.coordToIndex(3,4),MaterialId::Stone)==3);
        CHECK(map.staticMaterialSourceGeneration()==generation);
        CHECK(original.is_updated(stone));
        map.setMaterialAmount(map.coordToIndex(3,4),MaterialId::Stone,2);
        CHECK(map.staticMaterialSourceGeneration()==generation);
        CHECK(original.is_updated(stone));
        // Ordinary growing/harvestable stock changes must not invalidate this field.
        map.setResourceByIndex(8,8,WHEAT,0);
        map.setMaterialAmount(map.coordToIndex(8,8),MaterialId::Food,2);
        CHECK(map.staticMaterialSourceGeneration()==generation);
        CHECK(original.is_updated(stone));
        map.setMaterialAmount(map.coordToIndex(3,4),MaterialId::Stone,0);
        CHECK(map.staticMaterialSourceGeneration()!=generation);
        CHECK_FALSE(original.is_updated(stone));
        original.queue_gradient(stone);
        REQUIRE(original.queuedGradients.size()==1);
        for(bool text:{false,true})
        {
            const auto bytes=save(original,text);
            GradientManager restored(&map);
            REQUIRE(load(restored,bytes,text));
            CHECK(save(restored,text)==bytes);
            CHECK_FALSE(restored.is_updated(stone));
            auto cloned=original.clone();
            CHECK(save(*cloned,text)==bytes);
            CHECK_FALSE(cloned->is_updated(stone));
            for(int tick=0;tick<3;++tick)
            {
                restored.update(); cloned->update();
                CHECK(save(restored,text)==save(*cloned,text));
                CHECK(restored.is_updated(stone));
            }
            CHECK(restored.get_gradient(stone).get_height(3,4)!=0);
        }
        original.update();
        CHECK(original.is_updated(stone));
        map.setMaterialAmount(map.coordToIndex(3,4),MaterialId::Stone,1);
        CHECK_FALSE(original.is_updated(stone));
        CHECK(original.get_gradient(stone).get_height(3,4)==0);
        map.setNoResource(3,4,0);
        CHECK_FALSE(original.is_updated(stone));
        CHECK(original.get_gradient(stone).get_height(3,4)!=0);
        map.setResource(10,10,custom,0);
        CHECK_FALSE(original.is_updated(stone));
        CHECK(original.get_gradient(stone).get_height(10,10)==0);
        auto currentClone=original.clone();
        CHECK(currentClone->is_updated(stone));
        CHECK(save(*currentClone,false)==save(original,false));

        // An unplaced finite source changes intrinsic mutability immediately.
        auto finite=prototype; finite["key"]="test:finite-stone";
        finite["yields"]["stone"]["consumption"]="one";
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({finite})}}.dump());
        CHECK(stone.needs_updating(&map));
        CHECK_FALSE(original.is_updated(stone));
        original.get_gradient(stone);
        original.queue_gradient(stone);
        CHECK(original.queuedGradients.size()==1);
    }
    static void recurringInputsAndProviderLookup()
    {
        using AISharedRuntime::Management::MaterialTracker;
        using AISharedRuntime::Management::RecurringInputStock;
        using AISharedRuntime::BuildingDemand::Feed;
        using AISharedRuntime::BuildingDemand::ProduceWorker;
        for(bool paid:{false,true})
        {
            CAPTURE(paid);
            Game game(nullptr);
            const int completed=game.buildingsTypes.getTypeNum("inn",0,false);
            const int site=game.buildingsTypes.getTypeNum("inn",0,true);
            auto catalog=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
            auto& variant=catalog["variants"][completed];
            auto& semantics=variant["semantics"];
            semantics["feeding"]["enabled"]=paid;
            semantics["feeding"]["cost"]={{"food",2},{"oranges",1}};
            semantics["production"]["scheduling"]="weighted_committed_job";
            semantics["production"]["recipes"]={
                {"worker",{{"enabled",paid},{"duration",0},{"cost",{{"food",3},{"prunes",1}}}}},
                {"explorer",{{"enabled",false},{"duration",0},{"cost",{{"wood",1}}}}},
                {"warrior",{{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}}}};
            for(int resource:{WOOD,WHEAT,ORANGE,PRUNE}) variant["properties"]["maxMaterial"][resource]=50;
            game.buildingsTypes.loadSnapshotJson(catalog.dump());game.configureBuildingCatalog();
            // Parse the authored costs so all masks have the same validated
            // provenance as a real match; do not mutate cold descriptors.
            CHECK(game.buildingsTypes.get(completed)->semantics.production.recipes[WORKER].costMask
                ==((1u<<WHEAT)|(1u<<PRUNE)));
            setup(game);
            AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime(game,0));
            auto* recurring=game.addBuilding(4,4,completed,0);
            auto* ordinary=game.addBuilding(12,4,completed,0);
            auto* construction=game.addBuilding(20,4,site,0);
            REQUIRE(recurring);REQUIRE(ordinary);REQUIRE(construction);
            recurring->materials[WHEAT]=7;recurring->materials[ORANGE]=5;
            recurring->materials[PRUNE]=11;recurring->materials[WOOD]=13;
            ordinary->materials[WOOD]=17;
            auto& controller=runtime(game,0);
            controller.getOrder(); // initialize the register through its normal path
            auto& registry=controller.get_building_register();
            REQUIRE(registry.get_building(0)->gid==recurring->gid);
            REQUIRE(registry.get_building(1)->gid==ordinary->gid);
            REQUIRE(registry.get_building(2)->gid==construction->gid);
            CHECK(registry.provides(0,Feed)==paid);
            CHECK(registry.provides(0,ProduceWorker)==paid);
            CHECK(registry.provides(2,Feed)==paid); // site resolves completion
            CHECK(registry.provides(2,ProduceWorker)==paid);
            CHECK(registry.provides(0,int(AIPlanning::BuildingIntent::ProduceWarrior)));
            CHECK_FALSE(registry.provides(0,int(AIPlanning::BuildingIntent::ProduceExplorer)));
            CHECK_FALSE(registry.provides(0,-1));
            CHECK_FALSE(registry.provides(0,AISharedRuntime::BuildingDemand::Count));
            CHECK(registry.get_building(1000000)==nullptr);
            CHECK_FALSE(registry.provides(1000000,Feed));
            const int pending=registry.register_building();
            REQUIRE(registry.is_building_pending(pending));
            CHECK(registry.get_building(pending)==nullptr);
            CHECK_FALSE(registry.provides(pending,Feed));
            controller.add_material_tracker(new MaterialTracker(controller,0,1,RecurringInputStock),0);
            controller.add_material_tracker(new MaterialTracker(controller,1,1,WOOD),1);
            controller.add_material_tracker(new MaterialTracker(controller,pending,1,RecurringInputStock),pending);
            auto retained=controller.get_material_tracker(0);
            for(int tick=0;tick<AISharedRuntime::AI_SHARED_RUNTIME_TRACKER_SAMPLE_INTERVAL_TICKS;++tick) controller.getOrder();
            // Wheat is shared by feeding and production and counted once.
            // Disabled explorer wood is excluded; prune exercises the highest
            // supported bit. The always-enabled free warrior adds no inputs.
            CHECK(retained->get_total_level()==(paid ? 23 : 0));
            CHECK(controller.get_material_tracker(1)->get_total_level()==17);
            CHECK(controller.get_material_tracker(pending)->get_total_level()==0);
            REQUIRE(game.removeUnitAndBuildingAndFlags(4,4,unsigned(Game::DEL_BUILDING)));
            // The register still holds its observation until the next tick;
            // a disappeared live slot must safely return null immediately.
            REQUIRE(registry.is_building_found(0));
            CHECK(registry.get_building(0)==nullptr);
            CHECK_FALSE(registry.provides(0,Feed));
            controller.getOrder();
            CHECK_FALSE(controller.get_material_tracker(0));
            CHECK(retained->get_total_level()==(paid ? 23 : 0));
        }
    }
    static void run()
    {
        Game game(nullptr);game.map.setSize(5,5,GRASS);game.map.setGame(&game);
        Map& map=game.map;
        for(bool text:{false,true})
        {
            map.setResourceByIndex(3,4,WHEAT,5);
            GradientManager original(&map),restored(&map);
            const auto wheat=info(text ? static_cast<Entities::Entity*>(new Entities::AnyResource)
                                       : static_cast<Entities::Entity*>(new Entities::MaterialSource(WHEAT)));
            const auto water=info(new Entities::Water);
            auto avoidsIrrigation=info(new Entities::Position(0,0));
            avoidsIrrigation.add_obstacle(new Entities::Water);
            original.get_gradient(wheat);
            original.get_gradient(avoidsIrrigation);
            const auto resources=info(new Entities::MaterialSources((1u<<WHEAT)|(1u<<WOOD)));
            original.get_gradient(resources);
            auto groundObstacles=info(new Entities::Position(0,0));
            groundObstacles.add_obstacle(new Entities::ResourceGroundObstacle);
            original.get_gradient(groundObstacles);
            auto buildingObstacles=info(new Entities::Position(1,1));
            buildingObstacles.add_obstacle(new Entities::ResourceBuildingObstacle);
            original.get_gradient(buildingObstacles);
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
        snapshotMapQueries();
        snapshotGradientContinuation();
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

TEST_CASE("recurring input tracking and provider lookup preserve composite membership" *
          doctest::test_suite("RuntimeContinuation"))
{
    glob2test::HeadlessGlobals globals;
    RuntimeContinuationTest::recurringInputsAndProviderLookup();
}

TEST_CASE("runtime owner helpers and decisions release their full observation leases" *
          doctest::test_suite("RuntimeContinuation"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.header=true});
    fixture.addBuilding("swarm",4,4);
    fixture.game.players[0]->makeItAI(AI::ECONO);
    auto& controller=*dynamic_cast<AISharedRuntime::Runtime*>(fixture.game.players[0]->ai->aiImplementation);
    CHECK_THROWS_AS(controller.observation(),std::logic_error);
    std::weak_ptr<const SimulationSnapshot::Entities> lease;
    {
        AISharedRuntime::Runtime::OwnerObservationScope scope(controller);
        controller.observation();
        lease=controller.observation().components().entities;
        CHECK_FALSE(lease.expired());
    }
    CHECK(lease.expired());
    controller.getOrder();
    CHECK_THROWS_AS(controller.observation(),std::logic_error);
    auto world=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    std::weak_ptr<const AIEngine::AIWorldView> borrowed=world;
    {
        const std::vector<AIEngine::ExecutionReceipt> receipts;
        controller.getOrder({*world,0,0,receipts,world});
    }
    world.reset();
    CHECK(borrowed.expired());
    CHECK_THROWS_AS(controller.observation(),std::logic_error);
    class FailingAI : public AISharedRuntime::RuntimeAI {
    public:
        bool load(InputStream*,Player*,Sint32) override {return true;}
        void save(OutputStream*) override {}
        void handle_message(AISharedRuntime::Runtime&,const std::string&) override {}
        void tick(AISharedRuntime::Runtime&) override {throw std::runtime_error("fixture failure");}
    };
    AISharedRuntime::Runtime failing(new FailingAI,fixture.game.players[0]);
    world=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    borrowed=world;
    {
        const std::vector<AIEngine::ExecutionReceipt> receipts;
        AIEngine::DecisionContext context{*world,0,0,receipts,world};
        CHECK_THROWS_WITH(failing.getOrder(context),"fixture failure");
    }
    world.reset();
    CHECK(borrowed.expired());
    CHECK_THROWS_AS(failing.observation(),std::logic_error);
}

TEST_CASE("flat building relationships resolve captured unit incarnations" *
          doctest::test_suite("RuntimeContinuation"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.loadDefaultRace=true,.header=true});
    auto* building=fixture.addBuilding("inn",4,4);
    auto* worker=fixture.addUnit(WORKER,12,12);
    auto* occupant=fixture.addUnit(WARRIOR,13,12);
    building->unitsWorking.push_back(worker);
    building->unitsInside.push_back(occupant);
    auto observed=AIEngine::AIWorldView::capture(fixture.game,AIEngine::AIWorldView::captureCatalog(fixture.game));
    // The observation owns relationships independently of the live lists.
    building->unitsWorking.clear();building->unitsInside.clear();
    const auto* captured=observed->buildingAtSlot(building->gid);
    REQUIRE(captured);
    REQUIRE(observed->workers(*captured).size()==1);
    REQUIRE(observed->occupants(*captured).size()==1);
    const auto* readWorker=observed->unit(observed->workers(*captured).front());
    const auto* readOccupant=observed->unit(observed->occupants(*captured).front());
    REQUIRE(readWorker);REQUIRE(readOccupant);
    CHECK(readWorker->gid==worker->gid);
    CHECK(readOccupant->gid==occupant->gid);
    const auto* identityWorker=observed->unitAtSlot(worker->gid);
    const auto* identityOccupant=observed->unitAtSlot(occupant->gid);
    REQUIRE(identityWorker);REQUIRE(identityOccupant);
    CHECK(readWorker->scriptIdentity==identityWorker->identity.generation);
    CHECK(readOccupant->scriptIdentity==identityOccupant->identity.generation);
}

TEST_CASE("queued orders preserve selection incarnations through save and slot replacement" *
          doctest::test_suite("RuntimeContinuation"))
{
    glob2test::HeadlessGlobals globals;
    RuntimeContinuationTest::queuedTargetContinuation();
    OrderModifyBuilding order(17,3);
    auto* memory=new MemoryStreamBackend;
    BinaryOutputStream output(memory);output.writeUint32(0x51A139,"following");output.flush();
    BinaryInputStream input(new MemoryStreamBackend(memory->getBuffer(),memory->getPosition()));input.seekFromStart(0);
    AIEngine::loadSelectedTarget(input,order,139);
    CHECK_FALSE(order.aiSelectedTarget);
    CHECK(input.readUint32("following")==0x51A139);
    glob2test::HeadlessGame empty(glob2test::GameOptions{.header=true});
    const auto observation=AIEngine::AIWorldView::capture(empty.game,AIEngine::AIWorldView::captureCatalog(empty.game));
    AIEngine::selectTarget(order,*observation);
    REQUIRE(order.aiSelectedTarget);
    CHECK(order.aiSelectedTarget->gid==17);
    CHECK(order.aiSelectedTarget->generation==0);
    CHECK_FALSE(AIEngine::selectedTargetExists(order,*observation));
}

TEST_CASE("static material fields invalidate on source edits and preserve continuation" *
          doctest::test_suite("RuntimeContinuation"))
{
    glob2test::HeadlessGlobals globals;
    RuntimeContinuationTest::staticMaterialSourceContinuation();
}
