// SPDX-License-Identifier: GPL-3.0-or-later
// The material ledger (test/support/SimulationInvariants.h) at every tick of small AI
// economies, across seeds: what is harvested, grown, cleared, delivered, eaten and
// spent on births and buildings must account for every unit of every material.
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "Property.h"
#include "SimulationInvariants.h"
#include "AI.h"
#include "Building.h"
#include "Map.h"
#include "BuildingType.h"
#include "Player.h"
#include "TeamStat.h"
#include "Unit.h"

#include <optional>
#include <string>

namespace
{
// Two AI colonies on a 64x64 map, each with a swarm, an inn, workers, warriors,
// a wheat field and a wood grove, as in the AI continuation fixtures.
struct Colonies
{
	glob2test::HeadlessGame world{glob2test::GameOptions{
		.wDec=6, .hDec=6, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true}};
	Colonies(AI::ImplementationID first, AI::ImplementationID second, Uint32 seed, bool hunger)
	{
		GameHeader header;
		header.setNumberOfPlayers(2);
		header.setHungerDisabled(!hunger);
		header.getBasePlayer(0)=BasePlayer(0,"first",0,BasePlayer::playerTypeFromImplementationID(first));
		header.getBasePlayer(1)=BasePlayer(1,"second",1,BasePlayer::playerTypeFromImplementationID(second));
		header.setRandomSeed(seed);
		world.game.setGameHeader(header,true);
		world.game.setWaitingOnMask(0);
		for (int team=0; team<2; ++team)
		{
			const int offset=team*32;
			auto* t=world.game.teams[team];
			t->startPosX=4+offset; t->startPosY=4+offset; t->startPosSet=Team::START_POS_FROM_UNIT;
			for (auto* building : {world.addBuilding("swarm",4+offset,4+offset,0,team),
				world.addBuilding("inn",10+offset,4+offset,0,team)})
			{
				REQUIRE(building);
				building->setMaterial(WHEAT, building->type->maxMaterial[WHEAT]/2);
				building->update();
				t->addToStaticAbilitiesLists(building);
			}
			for (int unit=0; unit<12; ++unit)
				world.addUnit(unit<9 ? WORKER : WARRIOR,4+offset+unit,12+offset,team);
			for (int y=18+offset; y<24+offset; ++y)
				for (int x=4+offset; x<20+offset; ++x) world.game.map.setResourceByIndex(x,y,WHEAT,1);
			for (int y=16; y<24; ++y)
				for (int x=22; x<28; ++x) world.game.map.setResourceByIndex(x+offset,y+offset,WOOD,5);
		}
		world.game.map.setMapDiscovered();
		for (int team=0; team<2; ++team) world.game.teams[team]->stats.step(world.game.teams[team]);
	}
};

std::uint64_t total(const std::array<std::int64_t, MaterialSlotCount>& values)
{
	std::uint64_t sum=0;
	for (const auto value : values) sum+=std::uint64_t(value);
	return sum;
}
}

TEST_SUITE("MaterialLedger")
{
TEST_CASE("AI colonies account for every material at every tick")
{
	glob2test::HeadlessGlobals globals;
	glob2test::forEachSeed(2, [](std::uint32_t seed) {
		for (const bool hunger : {false, true})
		{
			CAPTURE(hunger);
			setSyncRandSeed(seed);
			Colonies colonies(AI::MAXIMA, AI::NUMBI, seed, hunger);
			Game& game=colonies.world.game;
			glob2test::stepAI(game); // starts statistics collection
			glob2test::MaterialLedger ledger(game);
			const auto before=ledger.flows();
			for (int tick=2; tick<=600; ++tick)
			{
				glob2test::stepAI(game);
				if (!ledger.check("AI economy tick "+std::to_string(tick))) break;
				if (tick%100==0) glob2test::expectBuildingInvariants(game);
			}
			const auto after=ledger.flows();
			// The economy actually moved material through the ledger's terms.
			CHECK(total(after.harvested)>total(before.harvested));
			CHECK(total(after.consumed)>total(before.consumed));
			INFO(ledger.inexactReasons());
			CHECK(ledger.checks()==599);
		}
	}, 713);
}
}
