// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <string>
#include <memory>
#include <iostream>
#include "GlobalContainer.h"
#include "FileManager.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "Version.h"
#include "shared_runtime/Runtime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>

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
            original.get_gradient(wheat);
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
