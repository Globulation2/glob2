// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit effects at buildings (UnitEffects.h) are render-only: recording them must
// not change what the simulation computes. Two identical AI games run side by
// side, one recording effects as a displayed game does, and their full
// checksums must agree on every tick.
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "AI.h"
#include "Player.h"
#include "render/GameAnimations.h"
#include "render/UnitEffects.h"
#include <set>
#include <vector>

namespace
{
struct World
{
    glob2test::HeadlessGame world{glob2test::GameOptions{
        .wDec=6, .hDec=6, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
    explicit World(bool recordEffects)
    {
        GameHeader header;
        header.setNumberOfPlayers(2);
        header.getBasePlayer(0)=BasePlayer(0,"castor",0,BasePlayer::playerTypeFromImplementationID(AI::CASTOR));
        header.getBasePlayer(1)=BasePlayer(1,"warrush",1,BasePlayer::playerTypeFromImplementationID(AI::WARRUSH));
        header.setRandomSeed(1307);
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
            auto* hospital=world.addBuilding("hospital",14+offset,4+offset,0,team);
            for (auto* building : {swarm,inn,hospital})
            {
                building->setMaterial(WHEAT, building->type->maxMaterial[WHEAT]);
                building->update();
                world.game.teams[team]->addToStaticAbilitiesLists(building);
            }
            for (int unit=0; unit<12; ++unit)
                world.addUnit(unit<8 ? WORKER : WARRIOR,4+offset+unit,12+offset,team);
            for (int y=18+offset; y<24+offset; ++y)
                for (int x=4+offset; x<20+offset; ++x) world.game.map.setResourceByIndex(x,y,WHEAT,1);
        }
        for (int team=0;team<2;++team) for (int y=16;y<24;++y) for (int x=22;x<28;++x)
            world.game.map.setResourceByIndex(x+team*32,y+team*32,WOOD,5);
        world.game.map.setMapDiscovered();
        if (recordEffects)
            world.game.setAnimations(std::make_unique<GameAnimations>(true,
                world.game.map.getSectorW()*world.game.map.getSectorH()));
    }
};

std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result,buildings,units;
    game.checkSum(&result,&buildings,&units,true);
    result.insert(result.end(),buildings.begin(),buildings.end());
    result.insert(result.end(),units.begin(),units.end());
    return result;
}
}

TEST_SUITE("UnitEffectDeterminism")
{
TEST_CASE("recording unit effects leaves every tick's checksum unchanged [slow][display:640x480]")
{
    // A display loads the death and explosion sprites that recording animations use.
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true});
    constexpr int Ticks=3000;
    std::vector<std::vector<Uint32>> trace;
    {
        World plain(false);
        for (int tick=0; tick<Ticks; ++tick)
        {
            glob2test::stepAI(plain.world.game);
            trace.push_back(state(plain.world.game));
        }
    }
    std::set<Uint8> kinds;
    World recorded(true);
    for (int tick=0; tick<Ticks; ++tick)
    {
        glob2test::stepAI(recorded.world.game);
        REQUIRE_MESSAGE(state(recorded.world.game)==trace[tick], "checksums differ at tick ", tick);
        const Game& game=recorded.world.game;
        for (int i=0; i<game.map.getSectorW()*game.map.getSectorH(); ++i)
            for (const auto& effect : game.animations->getUnitEffects(i)) kinds.insert(effect.kind);
    }
    // The comparison only means something if effects were actually recorded.
    CHECK(kinds.count(Uint8(UnitEffect::Kind::Spawn)));
    CHECK((kinds.count(Uint8(UnitEffect::Kind::Feed)) || kinds.count(Uint8(UnitEffect::Kind::Heal))));
    MESSAGE("effect kinds recorded: ", kinds.size());
}
}
