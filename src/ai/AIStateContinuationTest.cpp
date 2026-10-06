// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "AIImplementation.h"
#include <fstream>
#include <locale>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <set>

namespace
{
struct World
{
    glob2test::HeadlessGame world{glob2test::GameOptions{
        .wDec=6, .hDec=6, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
    World(AI::ImplementationID id, bool depleted, Uint32 seed, bool mixed=false, bool shared=false)
    {
        GameHeader header;
        header.setNumberOfPlayers(shared ? 3 : 2);
        header.setHungerDisabled(true);
        header.getBasePlayer(0)=BasePlayer(0,"tested AI",0,BasePlayer::playerTypeFromImplementationID(id));
        header.getBasePlayer(1)=BasePlayer(1,"opponent",1,mixed ? BasePlayer::playerTypeFromImplementationID(AI::CASTOR) : BasePlayer::P_LOCAL);
        if (shared) header.getBasePlayer(2)=BasePlayer(2,"shared controller",0,BasePlayer::playerTypeFromImplementationID(AI::ECONO));
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
                building->materials[WHEAT]=depleted ? 0 : building->type->maxMaterial[WHEAT];
                building->update();
                // These fixtures materialize finished providers directly; real
                // games register static capabilities when construction completes.
                world.game.teams[team]->addToStaticAbilitiesLists(building);
            }
            for (int unit=0; unit<12; ++unit)
                world.addUnit(unit<8 ? WORKER : WARRIOR,4+offset+unit,12+offset,team);
            if (!depleted)
                for (int y=18+offset; y<24+offset; ++y)
                    for (int x=4+offset; x<20+offset; ++x) world.game.map.setResource(x,y,WHEAT,1);
        }
        // A sustainable colony exercises construction and scheduling rather
        // than ending early after exhausting the fixture's starting resources.
        for (int team=0;team<2;++team) for (int y=16;y<24;++y) for (int x=22;x<28;++x)
            world.game.map.setResource(x+team*32,y+team*32,WOOD,5);
        world.game.map.setMapDiscovered();
        world.game.teams[0]->stats.step(world.game.teams[0]);
        world.game.teams[1]->stats.step(world.game.teams[1]);
    }
};

std::vector<std::string> tick(Game& game)
{
    for (int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
        if (game.players[p]->ai) REQUIRE(game.players[p]->team->isAlive);
    return glob2test::stepAI(game);
}
std::vector<MersenneTwister> randomStates(Game& game)
{
    std::vector<MersenneTwister> result;
    for (int p=0;p<game.gameHeader.getNumberOfPlayers();++p)
        if (game.players[p]->ai) result.push_back(game.players[p]->ai->aiImplementation->snapshotRandom());
    return result;
}
std::string snapshot(Game& game)
{
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);
    game.save(&out,false,"AI continuation"); out.flush();
    return backend->takeContents();
}
std::unique_ptr<GameGUI> restore(const std::string& bytes)
{
    auto restored=std::make_unique<GameGUI>(false);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored->game.load(&input));
    restored->game.setWaitingOnMask(0);
    return restored;
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
void continuation(AI::ImplementationID id, bool depleted, int checkpoint, bool mixed=false, bool shared=false)
{
    CAPTURE(id); CAPTURE(depleted); CAPTURE(checkpoint);
    World initial(id,depleted,713,mixed,shared);
    auto& game=initial.world.game;
    const auto beforePause=state(game);
    CHECK(game.players[0]->ai->getOrder(true)->getOrderType() == ORDER_NULL);
    CHECK(state(game)==beforePause);
    std::set<int> types;
    for (int tickNumber=0; tickNumber<checkpoint; ++tickNumber)
        types.insert(static_cast<unsigned char>(tick(game)[0][0]));
    const auto bytes=snapshot(game);
    const auto before=state(game);
    std::vector<std::vector<std::string>> orders;
    std::vector<std::vector<MersenneTwister>> randomTraces;
    std::vector<MersenneTwister> simulationRandom;
    std::vector<std::vector<Uint32>> traces;
    for (int i=0; i<256; ++i)
    {
        orders.push_back(tick(game)); traces.push_back(state(game)); randomTraces.push_back(randomStates(game)); simulationRandom.push_back(game.syncRandom);
        types.insert(static_cast<unsigned char>(orders.back()[0][0]));
    }
    const auto random=game.syncRandom;
    auto restored=restore(bytes);
    REQUIRE(state(restored->game)==before);
    for (int i=0; i<256; ++i)
    {
        CAPTURE(i);
        REQUIRE(tick(restored->game)==orders[i]);
        REQUIRE(state(restored->game)==traces[i]);
        REQUIRE(randomStates(restored->game)==randomTraces[i]);
        REQUIRE(restored->game.syncRandom==simulationRandom[i]);
        if (i==63 || i==127) {
            const auto again=snapshot(restored->game);
            restored=restore(again);
            REQUIRE(state(restored->game)==traces[i]);
            REQUIRE(randomStates(restored->game)==randomTraces[i]);
        }
    }
    CHECK(restored->game.syncRandom==random);
    const auto name=std::to_string(int(id))+"-"+std::to_string(depleted)+"-"+std::to_string(checkpoint)+"-"+std::to_string(mixed)+"-"+std::to_string(shared);
    const auto directory=glob2test::artifactDir();
    { std::ofstream save(directory/(name+".game"),std::ios::binary); save.write(bytes.data(),bytes.size()); }
    std::ofstream trace(directory/(name+".tsv")); trace.imbue(std::locale::classic());
    for (int i=0;i<256;++i) {
        trace << i;
        for (auto value : traces[i]) trace << '\t' << value;
        for (const auto& order : orders[i]) {
            trace << "\torder:";
            for (unsigned char byte : order) trace << std::hex << int(byte) << ',';
            trace << std::dec;
        }
        for (const auto& engine : randomTraces[i]) trace << "\trng:" << engine;
        trace << "\tsimulation-rng:" << simulationRandom[i] << '\n';
    }
    // A rich colony must exercise active decision making, not merely NullOrder.
    if (!depleted) CHECK(types.size()>1);
}
}

TEST_SUITE("AIStateContinuation")
{
    TEST_CASE("Cortex economy and food shortage survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (bool depleted : {false,true}) continuation(AI::CORTEX,depleted,256);
    }
    TEST_CASE("Cabino specialist modules survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {0,1,49,50,51,127,256,512,1024,2048})
            for (bool depleted : {false,true}) continuation(AI::CABINO,depleted,checkpoint);
    }
    TEST_CASE("Warrush growth and attack preparation survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {0,1,2,16,17,30,31,33,34,49,50,51,256})
            for (bool depleted : {false,true}) continuation(AI::WARRUSH,depleted,checkpoint);
    }
    TEST_CASE("Numbi growth and attack preparation survive save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {0,1,31,32,256,1024,1025})
            for (bool depleted : {false,true}) continuation(AI::NUMBI,depleted,checkpoint);
    }
    TEST_CASE("Nicowar shared runtime survives save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {0,1,16,17,18,63,64,117,127,128,256})
            for (bool depleted : {false,true}) continuation(AI::NICOWAR,depleted,checkpoint);
    }
    TEST_CASE("Econo shared runtime survives save-load")
    {
        glob2test::HeadlessGlobals globals;
        for (int checkpoint : {0,1,127,128,256})
            for (bool depleted : {false,true}) continuation(AI::ECONO,depleted,checkpoint);
    }
    TEST_CASE("all controllers continue alongside another AI and a shared-team controller [slow]")
    {
        glob2test::HeadlessGlobals globals;
        for (auto id : {AI::NUMBI,AI::CASTOR,AI::WARRUSH,AI::ECONO,AI::NICOWAR,AI::CABINO,AI::CORTEX,AI::MAXIMA})
            continuation(id,false,65,true,true);
    }
    TEST_CASE("Castor and Maxima active full games preserve decisions and RNG [slow]")
    {
        glob2test::HeadlessGlobals globals;
        for (auto id : {AI::CASTOR,AI::MAXIMA})
            for (int checkpoint : {1,32,64,256}) continuation(id,false,checkpoint);
    }

}
