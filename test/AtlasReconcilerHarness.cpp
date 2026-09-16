// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

/*
  Contract tests for the Atlas reconciler (src/ai/atlas/AtlasReconciler.cpp).

  The reconciler is the actuator of a level-triggered control loop, so its
  contract is mostly about what it does NOT do: an already-satisfied field
  must produce no orders, and an unsatisfied one must produce exactly the
  orders that close the gap and no others. A reconciler that emits nothing is
  trivially "correct" on the first half and useless on the second, so every
  test here checks both directions.

  Run: scons atlas-reconciler-test && ./build/src/AtlasReconcilerHarness
*/

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Order.h"
#include "Player.h"
#include "Team.h"
#include "ai/atlas/AtlasFieldSource.h"
#include "ai/atlas/AtlasReconciler.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <vector>

GlobalContainer *globalContainer = nullptr;

namespace
{
	int failures = 0;
	int checks = 0;

	void check(bool ok, const char *what)
	{
		checks++;
		if (!ok)
		{
			std::fprintf(stderr, "FAIL: %s\n", what);
			failures++;
		}
	}

	//! Count orders of a given type in a plan.
	size_t countType(const std::vector<std::shared_ptr<Order>> &orders, Uint8 type)
	{
		size_t n = 0;
		for (const auto &order : orders)
			if (order->getOrderType() == type)
				n++;
		return n;
	}
} // namespace

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.buildingsTypes.init();
	IntBuildingType::init();

	Game game(nullptr);
	game.map.setSize(5, 5, GRASS); // 32x32, power-of-two like every real map
	game.map.setGame(&game);
	game.addTeam();
	Team *team = game.teams[0];
	// Without this every placement is "into fog" and canPlace refuses, which
	// would make the emit-side tests vacuous.
	game.map.setMapDiscovered();
	// Game::executeOrder resolves the sender through players[], so test 7 —
	// which applies an area order through the real engine path rather than
	// poking the map — needs one registered player.
	game.players[0] = new Player(0, "atlas-harness", team, BasePlayer::P_LOCAL);
	game.gameHeader.setNumberOfPlayers(1);

	auto addBuilding = [&](int x, int y, int shortType, int level, bool site) {
		const Sint32 id = globals.buildingsTypes.getTypeNum(
			IntBuildingType::reverseConversionMap[shortType], level, site);
		check(id >= 0, "building type resolves");
		Building *b = game.addBuilding(x, y, id, 0);
		check(b != nullptr, "building added");
		return b;
	};

	Building *inn = addBuilding(8, 8, IntBuildingType::FOOD_BUILDING, 0, false);
	addBuilding(16, 16, IntBuildingType::SWARM_BUILDING, 0, false);

	Atlas::ReconcilerConfig config;
	Atlas::Reconciler reconciler;
	reconciler.init(team, config);

	// Every test starts from a freshly snapshotted identity field rather than
	// a shared one. Some tests below apply their orders to the live game, so a
	// baseline captured once at startup would go stale and later tests would
	// see spurious diffs against state an earlier test changed.
	auto baseline = [&]() {
		Atlas::DesiredState d;
		check(Atlas::IdentityFieldSource::snapshot(team, d), "baseline snapshot succeeds");
		return d;
	};

	Atlas::DesiredState desired;
	check(Atlas::IdentityFieldSource::snapshot(team, desired), "identity snapshot succeeds");
	check(desired.valid(), "identity snapshot is well formed");
	check(desired.w == game.map.getW() && desired.h == game.map.getH(),
	      "identity snapshot matches map size");

	// --- 1. Identity is a fixed point -------------------------------------
	// Desired == observed must close to zero orders. This is what pins the
	// top-left anchoring convention: if the snapshot wrote a building to a
	// different cell than the reconciler reads it from, the diff would see a
	// missing building AND a stray one, and this would not be empty.
	{
		auto plan = reconciler.plan(desired);
		check(plan.empty(), "identity field produces no orders");
	}

	// --- 2. A new desire produces exactly one OrderCreate ------------------
	{
		Atlas::DesiredState d = baseline();
		d.building[d.index(20, 20)] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		auto plan = reconciler.plan(d);
		check(plan.size() == 1, "one new desired building yields one order");
		check(countType(plan, ORDER_CREATE) == 1, "and that order is an OrderCreate");
	}

	// --- 3. A blocked desire RELOCATES rather than dying -------------------
	// (8,8) already holds the inn, so a second inn on its footprint is not
	// placeable there. The field names a preference, not a coordinate (the
	// score/legality split borrowed from AIEcho), so the reconciler must put
	// the building on the best legal cell nearby instead of dropping a desire
	// that could then never be satisfied.
	{
		reconciler.init(team, config);
		Atlas::DesiredState d = baseline();
		d.building[d.index(8, 9)] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		auto plan = reconciler.plan(d);
		check(countType(plan, ORDER_CREATE) == 1, "a blocked desire is relocated, not dropped");
		check(reconciler.stats().relocated == 1, "and is counted as a relocation");
		if (countType(plan, ORDER_CREATE) == 1)
		{
			auto created = std::static_pointer_cast<OrderCreate>(plan[0]);
			const int dist = std::max(std::abs(created->posX - 8), std::abs(created->posY - 9));
			check(dist > 0 && dist <= config.placementSearchRadius,
			      "and lands within the search radius of the preferred cell");
		}
	}

	// --- 3b. Relocation can be switched off --------------------------------
	// placementSearchRadius = 0 restores exact-coordinate behaviour, which is
	// what the oracle wants when it is being used to measure how faithfully a
	// teacher's exact layout can be reproduced.
	{
		Atlas::ReconcilerConfig exact = config;
		exact.placementSearchRadius = 0;
		reconciler.init(team, exact);
		Atlas::DesiredState d = baseline();
		d.building[d.index(8, 9)] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		auto plan = reconciler.plan(d);
		check(plan.empty(), "with relocation disabled the blocked desire is dropped");
		check(reconciler.stats().illegalSkipped > 0, "and is counted as illegal");
		reconciler.init(team, config);
	}

	// --- 4. Absence of desire does NOT immediately demolish -----------------
	// The anti-thrash rule, and the single most important behaviour here: a
	// field that momentarily forgets a building must not tear it down.
	{
		Atlas::DesiredState d = baseline();
		d.building[d.index(8, 8)] = 0;
		bool demolishedEarly = false;
		for (int step = 0; step < config.demolishPersistSteps - 1; step++)
			if (!reconciler.plan(d).empty())
				demolishedEarly = true;
		check(!demolishedEarly, "sustained empty desire does not demolish before the threshold");

		auto plan = reconciler.plan(d);
		check(countType(plan, ORDER_DELETE) == 1, "but does demolish once the threshold is reached");
	}

	// --- 5. commit shields a building from demolition ----------------------
	{
		reconciler.init(team, config); // clear the streak counters from test 4
		Atlas::DesiredState d = baseline();
		d.building[d.index(8, 8)] = 0;
		d.commit[d.index(8, 8)] = config.commitBlocksDemolish;
		bool demolished = false;
		for (int step = 0; step < config.demolishPersistSteps * 3; step++)
			if (countType(reconciler.plan(d), ORDER_DELETE) > 0)
				demolished = true;
		check(!demolished, "a committed cell is never demolished");
	}

	// --- 6. Restaffing --------------------------------------------------
	{
		reconciler.init(team, config);
		Atlas::DesiredState d = baseline();
		const size_t i = d.index(8, 8);
		d.workers[i] = Uint8(inn->maxUnitWorking + 1);
		auto plan = reconciler.plan(d);
		check(countType(plan, ORDER_MODIFY_BUILDING) == 1, "a staffing change yields one modify order");
	}

	// --- 7. Area painting round-trips -------------------------------------
	// Guard areas are the segmentation head's output, so the diff has to be
	// exact in both directions: painting cells on must emit an ADD, and the
	// identity field must then be a fixed point again.
	{
		reconciler.init(team, config);
		Atlas::DesiredState d = baseline();
		for (Sint32 y = 4; y < 7; y++)
			for (Sint32 x = 4; x < 7; x++)
				d.areas[d.index(x, y)] |= Atlas::AREA_GUARD;
		auto plan = reconciler.plan(d);
		check(countType(plan, ORDER_ALTER_GUARD_AREA) == 1, "painting guard cells yields one area order");

		// Apply it and confirm the field becomes satisfied — this is what
		// proves the bbox and bit indices agree with Game::executeAlterGuardArea
		// rather than merely being self-consistent.
		for (const auto &order : plan)
		{
			// Orders normally get their sender stamped by NetGame; in the
			// harness we are the transport, so set it ourselves.
			order->sender = 0;
			game.executeOrder(order, 0);
		}
		auto after = reconciler.plan(d);
		check(after.empty(), "after applying the area order the field is satisfied");

		Atlas::DesiredState back;
		check(Atlas::IdentityFieldSource::snapshot(team, back), "snapshot after area change");
		check((back.areas[back.index(5, 5)] & Atlas::AREA_GUARD) != 0,
		      "the painted cell reads back as guard area");
		check((back.areas[back.index(9, 9)] & Atlas::AREA_GUARD) == 0,
		      "an unpainted cell does not");
	}

	// --- 8. Urgency orders the plan ---------------------------------------
	{
		reconciler.init(team, config);
		Atlas::DesiredState d = baseline();
		const size_t low = d.index(20, 20);
		const size_t high = d.index(24, 24);
		d.building[low] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		d.urgency[low] = 10;
		d.building[high] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		d.urgency[high] = 250;
		auto plan = reconciler.plan(d);
		check(plan.size() == 2, "two desires yield two orders");
		if (plan.size() == 2)
		{
			auto first = std::static_pointer_cast<OrderCreate>(plan[0]);
			check(first->posX == 24 && first->posY == 24,
			      "the higher-urgency order is planned first");
		}
	}

	// --- 9. The queue cap holds -------------------------------------------
	{
		Atlas::ReconcilerConfig capped = config;
		capped.maxQueuedOrders = 3;
		reconciler.init(team, capped);
		Atlas::DesiredState d = baseline();
		for (Sint32 k = 0; k < 10; k++)
			d.building[d.index(20 + k % 5, 20 + k / 5)] = Uint8(IntBuildingType::FOOD_BUILDING + 1);
		auto plan = reconciler.plan(d);
		check(plan.size() <= 3, "the plan respects maxQueuedOrders");
		check(reconciler.stats().cappedOut > 0, "and reports what it dropped");
	}

	// --- 10. A malformed field is inert, not a crash -----------------------
	// A learned policy will eventually emit garbage; the reconciler must
	// absorb it rather than assert.
	{
		reconciler.init(team, config);
		Atlas::DesiredState d = baseline();
		d.building[d.index(20, 20)] = 200; // no such building type
		auto plan = reconciler.plan(d);
		check(plan.empty(), "an out-of-range building type is ignored");

		Atlas::DesiredState wrongSize;
		wrongSize.reset(8, 8);
		check(reconciler.plan(wrongSize).empty(), "a field sized for another map is ignored");
	}

	// --- 11. A displaced flag is MOVED, not rebuilt ---------------------
	// The flag-identity rule. A field that wants the same flag type a short
	// hop away is describing a move; answering it with a delete and a create
	// would put the flag through the demolish-persistence gate and throw its
	// construction away.
	{
		reconciler.init(team, config);
		Building *flag = addBuilding(20, 20, IntBuildingType::EXPLORATION_FLAG, 0, false);
		check(flag != nullptr, "flag placed for the move test");

		Atlas::DesiredState d = baseline();
		d.building[d.index(20, 20)] = 0;
		d.building[d.index(24, 20)] = Uint8(IntBuildingType::EXPLORATION_FLAG + 1);
		auto plan = reconciler.plan(d);
		check(countType(plan, ORDER_MOVE_FLAG) == 1, "a displaced flag yields one move order");
		check(countType(plan, ORDER_DELETE) == 0, "and no delete");
		check(countType(plan, ORDER_CREATE) == 0, "and no create");

		// The destination's attributes must still be reconciled — a flag that
		// arrives keeping its old garrison size is only half moved.
		Atlas::DesiredState staffed = baseline();
		staffed.building[staffed.index(20, 20)] = 0;
		staffed.building[staffed.index(24, 20)] = Uint8(IntBuildingType::EXPLORATION_FLAG + 1);
		staffed.workers[staffed.index(24, 20)] = Uint8(flag->maxUnitWorking + 1);
		reconciler.init(team, config);
		auto restaffed = reconciler.plan(staffed);
		check(countType(restaffed, ORDER_MOVE_FLAG) == 1, "move order still issued when restaffing");
		check(countType(restaffed, ORDER_MODIFY_BUILDING) == 1,
		      "and the moved flag's destination staffing is reconciled");

		// Beyond maxFlagMoveDist it is a create and a destroy, not a move:
		// the flag would arrive instantly but its garrison would walk. The map
		// here is 32x32 and wraps, so the greatest possible separation is 16 —
		// (30,20) is 10 from (20,20), comfortably past the limit of 8.
		Atlas::DesiredState far = baseline();
		far.building[far.index(20, 20)] = 0;
		far.building[far.index(30, 20)] = Uint8(IntBuildingType::EXPLORATION_FLAG + 1);
		reconciler.init(team, config);
		auto farPlan = reconciler.plan(far);
		check(countType(farPlan, ORDER_MOVE_FLAG) == 0, "a distant flag desire is not a move");
		check(countType(farPlan, ORDER_CREATE) == 1, "it is a create");

		// Two flags and two vacancies: the matching loop has to consume both
		// pairs, not stop after the first.
		reconciler.init(team, config);
		Building *first = addBuilding(2, 28, IntBuildingType::EXPLORATION_FLAG, 0, false);
		Building *second = addBuilding(10, 28, IntBuildingType::EXPLORATION_FLAG, 0, false);
		check(first && second, "two flags placed for the pairing test");
		Atlas::DesiredState pairing = baseline();
		pairing.building[pairing.index(2, 28)] = 0;
		pairing.building[pairing.index(10, 28)] = 0;
		pairing.building[pairing.index(3, 28)] = Uint8(IntBuildingType::EXPLORATION_FLAG + 1);
		pairing.building[pairing.index(11, 28)] = Uint8(IntBuildingType::EXPLORATION_FLAG + 1);
		auto pairPlan = reconciler.plan(pairing);
		check(countType(pairPlan, ORDER_MOVE_FLAG) == 2, "both flags are matched by a move");
		check(countType(pairPlan, ORDER_CREATE) == 0, "neither vacancy is built fresh");
		int shortHops = 0;
		for (const auto &order : pairPlan)
			if (order->getOrderType() == ORDER_MOVE_FLAG)
			{
				auto move = std::static_pointer_cast<OrderMoveFlag>(order);
				const int from = (move->gid == first->gid) ? 2 : 10;
				if (std::abs(move->x - from) == 1)
					shortHops++;
			}
		check(shortHops == 2, "each flag takes the adjacent vacancy, not the far one");
	}

	std::printf("Atlas reconciler: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
