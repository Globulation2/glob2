// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include <BinaryStream.h>
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
void continuation(AI::ImplementationID id, bool depleted, int checkpoint, int followup=256)
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
    for (int i=0; i<followup; ++i)
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
    for (int i=0; i<followup; ++i)
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

TEST_SUITE("AIDecisionCoverage")
{
    TEST_CASE("Cortex economy and food shortage survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::CORTEX,depleted,checkpoint);
    }
    TEST_CASE("Cabino specialist modules survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::CABINO,depleted,checkpoint);
    }
    TEST_CASE("Warrush growth and attack preparation survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::WARRUSH,depleted,checkpoint);
    }
    TEST_CASE("Numbi growth and attack preparation survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::NUMBI,depleted,checkpoint);
    }
    TEST_CASE("Nicowar shared runtime survives save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::NICOWAR,depleted,checkpoint);
    }
    TEST_CASE("Econo shared runtime survives save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {64,256}) for (bool depleted : {false,true}) continuation(AI::ECONO,depleted,checkpoint);
    }
    TEST_CASE("Castor and Maxima growth and shortage resume at early and active checkpoints")
    {
        glob2test::HeadlessGlobals globals;
        for (auto id : {AI::CASTOR,AI::MAXIMA})
            for (int checkpoint : {64,256})
                for (bool depleted : {false,true}) continuation(id,depleted,checkpoint);
    }
    TEST_CASE("long AI campaigns resume with identical orders state and game RNG [slow][artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        for (auto id : {AI::CASTOR,AI::CORTEX,AI::CABINO,AI::NICOWAR,AI::MAXIMA})
            continuation(id,false,1024,4096);
    }
}
