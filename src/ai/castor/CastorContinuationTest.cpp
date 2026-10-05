// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "AICastor.h"
#include "AICastorTuning.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <set>

namespace
{
struct World
{
    glob2test::HeadlessGame world{glob2test::GameOptions{
        .wDec=6, .hDec=6, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
    World(AI::ImplementationID id, bool depleted, Uint32 seed)
    {
        GameHeader header;
        header.setNumberOfPlayers(2);
        header.getBasePlayer(0)=BasePlayer(0,"tested AI",0,BasePlayer::playerTypeFromImplementationID(id));
        header.getBasePlayer(1)=BasePlayer(1,"opponent",1,BasePlayer::P_LOCAL);
        header.setRandomSeed(seed);
        world.game.setGameHeader(header,true);
        world.game.setWaitingOnMask(0);
        for (int team=0; team<2; ++team)
        {
            const int offset=team*32;
            world.game.teams[team]->startPosX=4+offset;
            world.game.teams[team]->startPosY=4+offset;
            world.game.teams[team]->startPosSet=Team::START_POS_FROM_UNIT;
            auto* swarm=world.addBuilding("swarm",4+offset,4+offset,0,team);
            auto* inn=world.addBuilding("inn",10+offset,4+offset,0,team);
            for (auto* building : {swarm,inn})
            {
                building->resources[WHEAT]=depleted ? 0 : building->type->maxResource[WHEAT];
                building->update();
            }
            for (int unit=0; unit<12; ++unit)
                world.addUnit(unit<8 ? WORKER : WARRIOR,4+offset+unit,12+offset,team);
            if (!depleted)
                for (int y=18+offset; y<24+offset; ++y)
                    for (int x=4+offset; x<20+offset; ++x) world.game.map.setResource(x,y,WHEAT,1);
        }
        world.game.map.setMapDiscovered();
        world.game.teams[0]->stats.step(world.game.teams[0]);
        world.game.teams[1]->stats.step(world.game.teams[1]);
    }
};

std::string tick(Game& game)
{
    auto order=game.players[0]->ai->getOrder(false);
    REQUIRE(order != nullptr);
    const auto length=order->getDataLength();
    REQUIRE(length >= 0);
    std::string encoded(1,static_cast<char>(order->getOrderType()));
    if (length) encoded.append(reinterpret_cast<const char*>(order->getData()),length);
    order->sender=0;
    game.executeOrder(order,0);
    game.syncStep(0);
    return encoded;
}
std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result,buildings,units;
    game.checkSum(&result,&buildings,&units,true);
    result.erase(result.begin()); // Loaded header records the save format.
    result.insert(result.end(),buildings.begin(),buildings.end());
    result.insert(result.end(),units.begin(),units.end());
    return result;
}
void continuation(AI::ImplementationID id, bool depleted, int checkpoint)
{
    CAPTURE(id); CAPTURE(depleted); CAPTURE(checkpoint);
    World initial(id,depleted,713);
    auto& game=initial.world.game;
    const auto beforePause=state(game);
    CHECK(game.players[0]->ai->getOrder(true)->getOrderType() == ORDER_NULL);
    CHECK(state(game)==beforePause);
    std::set<int> types;
    for (int tickNumber=0; tickNumber<checkpoint; ++tickNumber)
        types.insert(static_cast<unsigned char>(tick(game)[0]));
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);
    game.save(&out,false,"AI coverage continuation"); out.flush();
    const auto bytes=backend->takeContents();
    const auto before=state(game);
    std::vector<std::string> orders;
    std::vector<std::vector<Uint32>> traces;
    for (int i=0; i<128; ++i)
    {
        orders.push_back(tick(game)); traces.push_back(state(game));
        types.insert(static_cast<unsigned char>(orders.back()[0]));
    }
    const auto random=game.syncRandom;
    GameGUI restored(false);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input));
    restored.game.setWaitingOnMask(0);
    REQUIRE(state(restored.game)==before);
    for (int i=0; i<128; ++i)
    {
        CAPTURE(i);
        REQUIRE(tick(restored.game)==orders[i]);
        REQUIRE(state(restored.game)==traces[i]);
    }
    CHECK(restored.game.syncRandom==random);
    // A rich colony must exercise active decision making, not merely NullOrder.
    if (!depleted) CHECK(types.size()>1);
}
}

TEST_SUITE("CastorContinuation")
{
    TEST_CASE("crop habitat excludes transition sprite sixteen and new paved terrain")
    {
        glob2test::HeadlessGlobals globals;
        World world(AI::CASTOR,false,713);
        auto& map=world.world.game.map;
        map.setTerrain(20,20,16); // Historical >16 check accidentally accepted this shore.
        map.setCellTerrain(24,20,TRAIL);
        map.setCellTerrain(28,20,ICE);
        AICastor ai(world.world.game.players[0]);
        for(int tick=0;tick<AI_CASTOR_BOOT_IDLE_TICKS+2;++tick) ai.getOrder();
        for(int x:{20,24,28})
            CHECK(ai.notGrassMap[20*64+x]==AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE);
        CHECK(ai.notGrassMap[40*64+40]<AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE);
    }
    TEST_CASE("Castor boot and active projects survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {1,32,40,49,64,256,512})
            for (bool depleted : {false,true}) continuation(AI::CASTOR,depleted,checkpoint);
    }
    TEST_CASE("Castor seeded boot and active decisions repeat exactly")
    {
        glob2test::HeadlessGlobals globals;
        std::vector<std::string> orders;
        std::vector<std::vector<Uint32>> traces;
        {
            setSyncRandSeed(713);
            World initial(AI::CASTOR,false,713);
            for (int i=0; i<512; ++i) { orders.push_back(tick(initial.world.game)); traces.push_back(state(initial.world.game)); }
        }
        setSyncRandSeed(713);
        World repeated(AI::CASTOR,false,713);
        std::set<int> types;
        for (int i=0; i<512; ++i)
        {
            CAPTURE(i);
            const auto order=tick(repeated.world.game);
            types.insert(static_cast<unsigned char>(order[0]));
            REQUIRE(order==orders[i]); REQUIRE(state(repeated.world.game)==traces[i]);
        }
        CHECK(types.size()>1);
    }
    TEST_CASE("binary and text snapshots preserve project fields and cache history")
    {
        glob2test::HeadlessGlobals globals;
        World world(AI::CASTOR,false,713);
        AICastor initial(world.world.game.players[0]);
        for (int i=0; i<300; ++i) initial.getOrder();
        auto* project=new AICastor::Project(IntBuildingType::FOOD_BUILDING,"snapshot");
        project->amount=7; project->mainWorkers=3; project->foodWorkers=4;
        project->subPhase=AICastor::AI_CASTOR_SUBPHASE_WAIT_FINISHED;
        project->timer=217; project->blocking=false; project->priority=17;
        initial.projects.push_back(project);
        for (bool text : {false,true})
        {
            auto* backend=new GAGCore::MemoryStreamBackend;
            std::unique_ptr<GAGCore::OutputStream> output;
            if (text) output=std::make_unique<GAGCore::TextOutputStream>(backend);
            else output=std::make_unique<GAGCore::BinaryOutputStream>(backend);
            initial.save(output.get()); output->flush();
            const auto bytes=backend->takeContents();
            std::unique_ptr<GAGCore::InputStream> input;
            auto* data=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size());
            data->seekFromStart(0);
            if (text) input=std::make_unique<GAGCore::TextInputStream>(data);
            else input=std::make_unique<GAGCore::BinaryInputStream>(data);
            input->seekFromStart(0);
            AICastor restored(world.world.game.players[0]);
            REQUIRE(restored.load(input.get(),world.world.game.players[0],126));
            REQUIRE(restored.projects.size()==initial.projects.size());
            CHECK(restored.projects.back()->debugStdName==project->debugStdName);
            CHECK(std::string(restored.projects.back()->debugName)==project->debugStdName);
            auto* secondBackend=new GAGCore::MemoryStreamBackend;
            std::unique_ptr<GAGCore::OutputStream> second;
            if (text) second=std::make_unique<GAGCore::TextOutputStream>(secondBackend);
            else second=std::make_unique<GAGCore::BinaryOutputStream>(secondBackend);
            restored.save(second.get()); second->flush();
            CHECK(secondBackend->takeContents()==bytes);
        }
    }
    TEST_CASE("historical timer-only AI saves remain readable")
    {
        glob2test::HeadlessGlobals globals;
        World world(AI::CASTOR,false,713);
        for (int version : {1,2})
        {
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(backend);
            output.writeEnterSection("AICastor");
            output.writeSint32(version,"aiFileVersion");
            output.writeUint32(97,"timer");
            output.writeLeaveSection(); output.flush();
            const auto bytes=backend->takeContents();
            AICastor restored(world.world.game.players[0]);
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0);
            REQUIRE(restored.load(&input,world.world.game.players[0],125));
            CHECK(restored.timer==97); CHECK(restored.computeBoot==0); CHECK(restored.projects.empty());
        }
    }
}
