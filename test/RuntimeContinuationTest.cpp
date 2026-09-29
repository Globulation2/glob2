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
#include "shared_runtime/Runtime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>

GlobalContainer* globalContainer=nullptr;
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
    static void sharing()
    {
        Game source(nullptr),target(nullptr);setup(source);setup(target);
        // Slot 1 owns updating, but slot 0 is the first serialized sharer.
        runtime(source,1).getOrder();runtime(source,0).getOrder();
        auto& manager=runtime(source,0).get_gradient_manager();
        manager.get_gradient(info(new Entities::Resource(WHEAT)));
        manager.queue_gradient(info(new Entities::Water));
        for(int i=0;i<2;++i)
        {
            const auto bytes=saveRuntime(runtime(source,i));
            auto* backend=new MemoryStreamBackend;
            backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
            BinaryInputStream in(backend);
            assert(runtime(target,i).load(&in,target.players[i],VERSION_MINOR));
            assert(saveRuntime(runtime(target,i))==bytes);
        }
        assert(&runtime(target,0).get_gradient_manager()==&runtime(target,1).get_gradient_manager());
        for(int tick=0;tick<5;++tick)
            for(int i=0;i<2;++i)
            {
                runtime(source,i).getOrder();runtime(target,i).getOrder();
                assert(saveRuntime(runtime(source,i))==saveRuntime(runtime(target,i)));
            }
        source.players[0]->type=BasePlayer::P_LOCAL;
        const auto bytes=saveRuntime(runtime(source,1));
        source.players[0]->type=BasePlayer::playerTypeFromImplementationID(AI::NICOWAR);
        Game humanSlot(nullptr);setup(humanSlot);
        auto* backend=new MemoryStreamBackend;
        backend->write(bytes.data(),bytes.size());backend->seekFromStart(0);
        BinaryInputStream in(backend);
        assert(runtime(humanSlot,1).load(&in,humanSlot.players[1],VERSION_MINOR));
        assert(saveRuntime(runtime(humanSlot,1))==bytes);
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
        sharing();
        std::cout<<"Runtime gradient continuation: binary/text fields, stale ages, queued work, duplicate entries and invalid indices PASS\n";
    }
};
int main(int argc,char** argv)
{
    assert(argc==3 && std::string(argv[1]).find("glob2-save-test-")==0);
    GlobalContainer globals(argv[1]);globalContainer=&globals;globals.runNoX=true;
    globals.fileManager->addDir(argv[2]);
    globals.buildingsTypes.init();IntBuildingType::init();
    RuntimeContinuationTest::run();
}
