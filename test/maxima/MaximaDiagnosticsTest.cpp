// Exercise public AI registration and the normal game/save loading path.
#include "GlobalContainer.h"
#include "Engine.h"
#include "Utilities.h"
#include "Order.h"
#include "Player.h"
#include "AINames.h"
#include "AIMaxima.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

GlobalContainer* globalContainer=nullptr;
using namespace GAGCore;
static std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result,buildings,units;
    game.checkSum(&result,&buildings,&units,true);
    result.erase(result.begin()); // Loading updates the saved map header.
    result.insert(result.end(),buildings.begin(),buildings.end());
    result.insert(result.end(),units.begin(),units.end());
    return result;
}
static std::string step(Game& game)
{
    std::string orders;
    for(int seat=0;seat<game.gameHeader.getNumberOfPlayers();++seat)
    {
        auto order=game.players[seat]->ai->getOrder(false);
        order->sender=seat;
        orders+=char(order->getOrderType());
        orders.append(reinterpret_cast<const char*>(order->getData()),order->getDataLength());
        game.executeOrder(order,0);
    }
    game.syncStep(0);
    return orders;
}
static void run(Uint32 seed)
{
    GameGUI original;
    auto map=Engine::loadMapHeader("maps/balanced.map");
    GameHeader header;
    header.setNumberOfPlayers(2);header.setRandomSeed(seed);
    for(int seat=0;seat<2;++seat)
        header.getBasePlayer(seat)=BasePlayer(seat,"Maxima",seat,
            BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    assert(original.loadFromHeaders(map,header,true,true));
    for(int tick=0;tick<600;++tick) step(original.game);
    auto* maxima=dynamic_cast<AIMaxima::Maxima*>(original.game.players[0]->ai->aiImplementation);
    assert(maxima);
    // Registration and the ordinary load path are what this test covers; the
    // save/load continuation below exercises the AI's own state end to end.
    auto* storage=new MemoryStreamBackend;
    BinaryOutputStream output(storage);
    original.save(&output,"Maxima continuation regression");
    const std::string bytes(storage->getBuffer(),storage->getPosition());
    std::vector<std::string> expectedOrders;
    std::vector<std::vector<Uint32>> expectedStates;
    for(int tick=0;tick<300;++tick)
    {
        expectedOrders.push_back(step(original.game));
        expectedStates.push_back(state(original.game));
    }
    GameGUI restored;
    BinaryInputStream input(new MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);assert(restored.load(&input));
    restored.game.setGameHeader(header,true);
    for(int tick=0;tick<300;++tick)
    {
        const auto orders=step(restored.game);
        if(orders!=expectedOrders[tick] || state(restored.game)!=expectedStates[tick])
        {
            std::cerr<<"Continuation mismatch seed="<<seed<<" tick="<<tick
                <<" orders="<<(orders==expectedOrders[tick])<<"\n";
            assert(false);
        }
    }
    std::cout<<"seed "<<seed<<": 300 resumed orders and simulation checksums match\n";
}
int main()
{
    GlobalContainer globals("glob2-maxima-test");globalContainer=&globals;globals.runNoX=true;globals.load();
    assert(AI::CORTEX==6 && AI::MAXIMA==7);
    assert(AINames::parseAIName("maxima")==AI::MAXIMA);
    assert(AINames::getAIText(AI::MAXIMA)=="Maxima");
    assert(AINames::getAIText(AI::CORTEX)=="Cortex");
    assert(!AINames::getAIDescription(AI::MAXIMA).empty());
    run(123456);run(731);
}
