// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "FileManager.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "Version.h"
#include "echo/Echo.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>

GlobalContainer* globalContainer=nullptr;
using namespace AIEcho::Gradients;
using namespace GAGCore;

class EchoContinuationTest
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
    class QuietAI : public AIEcho::EchoAI
    {
    public:
        bool load(InputStream*,Player*,Sint32) override {return true;}
        void save(OutputStream*) override {}
        void tick(AIEcho::Echo&) override {}
        void handle_message(AIEcho::Echo&,const std::string&) override {}
    };
    static AIEcho::Echo& echo(Game& game,int i)
    {
        return *dynamic_cast<AIEcho::Echo*>(game.players[i]->ai->aiImplementation);
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
            player->ai->aiImplementation=new AIEcho::Echo(new QuietAI,player);
        }
    }
    static std::string saveEcho(AIEcho::Echo& value)
    {
        auto* backend=new MemoryStreamBackend;
        BinaryOutputStream out(backend);value.save(&out);out.flush();
        return backend->takeContents();
    }
    static void independentManagers()
    {
        Game source(nullptr),target(nullptr);setup(source);setup(target);
        // Each controller owns its cache, regardless of poll order.
        echo(source,1).getOrder();echo(source,0).getOrder();
        auto& first=echo(source,0).get_gradient_manager();
        auto& second=echo(source,1).get_gradient_manager();
        assert(&first!=&second);
        first.get_gradient(info(new Entities::Resource(WHEAT)));
        first.queue_gradient(info(new Entities::Water));
        assert(second.gradients.empty());
        for(int i=0;i<2;++i)
        {
            const auto bytes=saveEcho(echo(source,i));
            auto* backend=new MemoryStreamBackend;
            backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
            BinaryInputStream in(backend);
            assert(echo(target,i).load(&in,target.players[i],VERSION_MINOR));
            assert(saveEcho(echo(target,i))==bytes);
        }
        assert(&echo(target,0).get_gradient_manager()!=&echo(target,1).get_gradient_manager());
        for(int tick=0;tick<5;++tick)
            for(int i=0;i<2;++i)
            {
                echo(source,i).getOrder();echo(target,i).getOrder();
                assert(saveEcho(echo(source,i))==saveEcho(echo(target,i)));
            }

        // An older shared manager must become a complete, independent copy
        // when a later Echo player references its previous owner.
        auto legacyCopy=first.clone();
        assert(save(*legacyCopy,false)==save(first,false));
        assert(legacyCopy->gradients[0]!=first.gradients[0]);
        assert(legacyCopy->gradients[0]->gradient_info.sources[0]!=
            first.gradients[0]->gradient_info.sources[0]);
        const auto originalFirst=save(first,false);
        legacyCopy->update();
        assert(save(first,false)==originalFirst);
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
            assert(load(restored,bytes,text));
            assert(save(restored,text)==bytes);
            assert(restored.gradients[0]->get_height(3,4)==0);
            assert(!restored.is_updated(wheat));
            for(int tick=0;tick<5;++tick)
            {
                original.update();restored.update();
                assert(save(original,text)==save(restored,text));
                assert(original.is_updated(wheat)==restored.is_updated(wheat));
            }
            GradientManager empty(&map),emptyRestored(&map);
            assert(load(emptyRestored,save(empty,text),text));
            assert(save(emptyRestored,text)==save(empty,text));
            original.queuedGradients.push(1000);
            GradientManager badQueue(&map);
            assert(!load(badQueue,save(original,text),text));
        }
        independentManagers();
        std::cout<<"Echo gradient continuation: independent managers, binary/text fields, stale ages, queued work and invalid indices PASS\n";
    }
};
int main(int argc,char** argv)
{
    assert(argc==3 && std::string(argv[1]).find("glob2-save-test-")==0);
    GlobalContainer globals(argv[1]);globalContainer=&globals;globals.runNoX=true;
    globals.fileManager->addDir(argv[2]);
    globals.buildingsTypes.init();IntBuildingType::init();
    EchoContinuationTest::run();
}
