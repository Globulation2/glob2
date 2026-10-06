// SPDX-License-Identifier: GPL-3.0-or-later
// Hungry units swap inns when that shortens both walks: a unit that booked the
// last place in the inn near a team mate, from farther away, hands it over.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Ressource.h"
#include <cstdio>
#include <cstdlib>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

struct World
{
	GameGUI gui;
	Game& game = gui.game;
	Team* team = nullptr;

	World()
	{
		game.map.setSize(6, 6, GRASS);  // 64x64
		game.map.setGame(&game);
		game.addTeam(0);
		team = game.teams[0];
		// The gradient scheduler expects an in-use field; allocation is lazy (#243 lifts this).
		game.map.getMaterialGradient(0, WOOD, 0);
	}

	// An inn with `meals` wheat: it takes bookings while it has more wheat than guests.
	Building* addInn(int x, int y, int meals)
	{
		const int typeNum = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		require(typeNum >= 0, "inn type exists");
		Building* b = game.addBuilding(x, y, typeNum, 0);
		require(b != nullptr, "inn placed");
		game.map.setBuilding(x, y, b->type->width, b->type->height, b->gid);
		b->materials[WHEAT] = meals;
		b->update();
		return b;
	}

	// A worker that looks for food on its next activity check.
	Unit* addHungryWorker(int x, int y)
	{
		Unit* u = game.addUnit(x, y, 0, WORKER, 0, 0, 0, 0);
		require(u != nullptr, "worker placed");
		u->hungry = 0;
		u->medical = Unit::MED_HUNGRY;
		u->needToRecheckMedical = true;
		return u;
	}

	static bool booked(const Building* inn, const Unit* u)
	{
		for (const Unit* guest : inn->unitsInside)
			if (guest == u)
				return true;
		return false;
	}
};

// Inn A (west) has one meal, inn B (east) plenty. The far unit books A first
// and fills it; the near unit then has to book B. At that moment both walks
// together are longer than with the inns exchanged, so they trade.
static void theLaterBookerTradesWithTheOneItWouldCross()
{
	World world;
	Building* a = world.addInn(4, 8, 1);
	Building* b = world.addInn(44, 8, 10);
	Unit* farWorker = world.addHungryWorker(20, 9);
	Unit* nearWorker = world.addHungryWorker(8, 9);
	require(world.game.integrity(), "scenario setup is consistent");

	// Units look for food at the end of their current action.
	for (int i = 0; i < 100 && !(farWorker->attachedBuilding && nearWorker->attachedBuilding); ++i)
		world.game.syncStep(0);

	require(farWorker->activity == Unit::ACT_UPGRADING && farWorker->destinationPurpose == FEED, "the far unit goes to eat");
	require(nearWorker->activity == Unit::ACT_UPGRADING && nearWorker->destinationPurpose == FEED, "the near unit goes to eat");
	require(nearWorker->attachedBuilding == a && nearWorker->targetBuilding == a, "the near unit ends up with the near inn");
	require(farWorker->attachedBuilding == b && farWorker->targetBuilding == b, "the far unit ends up with the far inn");
	require(World::booked(a, nearWorker) && !World::booked(a, farWorker), "inn A's guest list follows");
	require(World::booked(b, farWorker) && !World::booked(b, nearWorker), "inn B's guest list follows");
	require(a->unitsInside.size() == 1 && b->unitsInside.size() == 1, "each inn keeps one booking");
	require(world.game.integrity(), "integrity after the swap");

	// Both walk to their inn (a tile takes a couple of dozen ticks) and eat.
	bool nearAte = false, farAte = false;
	for (int i = 0; i < 3000 && !(nearAte && farAte); ++i)
	{
		world.game.syncStep(0);
		nearAte |= nearWorker->displacement == Unit::DIS_INSIDE && nearWorker->attachedBuilding == a;
		farAte |= farWorker->displacement == Unit::DIS_INSIDE && farWorker->attachedBuilding == b;
	}
	if (!(nearAte && farAte))
		std::fprintf(stderr, "near: act=%d dis=%d att=%p dead=%d hp=%d | far: act=%d dis=%d att=%p dead=%d hp=%d (a=%p b=%p)\n",
			nearWorker->activity, nearWorker->displacement, (void*)nearWorker->attachedBuilding, nearWorker->isDead, nearWorker->hp,
			farWorker->activity, farWorker->displacement, (void*)farWorker->attachedBuilding, farWorker->isDead, farWorker->hp, (void*)a, (void*)b);
	require(nearAte, "the near unit eats at inn A");
	require(farAte, "the far unit eats at inn B");
	std::puts("PASS a later booker trades inns with the team mate it would have crossed");
}

// With the inns already matched to the units, booking changes nothing.
static void aGoodBookingStays()
{
	World world;
	Building* a = world.addInn(4, 8, 10);
	Building* b = world.addInn(44, 8, 10);
	Unit* west = world.addHungryWorker(8, 9);
	Unit* east = world.addHungryWorker(40, 9);

	for (int i = 0; i < 100 && !(west->attachedBuilding && east->attachedBuilding); ++i)
		world.game.syncStep(0);

	require(west->attachedBuilding == a && east->attachedBuilding == b, "each unit keeps its nearest inn");
	require(World::booked(a, west) && World::booked(b, east), "the guest lists match");
	require(world.game.integrity(), "integrity without a swap");
	std::puts("PASS a booking that is already the shorter one is kept");
}
}

TEST_SUITE("InnSwap")
{
	TEST_CASE("the later booker trades with the one it would cross")
	{
		glob2test::HeadlessGlobals globals;
		theLaterBookerTradesWithTheOneItWouldCross();
	}
	TEST_CASE("a good booking stays")
	{
		glob2test::HeadlessGlobals globals;
		aGoodBookingStays();
	}
}
