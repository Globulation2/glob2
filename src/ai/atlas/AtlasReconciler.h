// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Atlas reconciler: desired state + observed state -> orders.

  This is the actuator half of the declarative design described in
  AtlasDesiredState.h. It is deliberately mechanical. Every gram of strategy
  that leaks in here is strategy the policy cannot learn and a reviewer cannot
  see, so the rules below are the whole of its judgement:

    1. Diff desired against observed, where observed INCLUDES in-flight
       construction. An under-construction inn at (x,y) satisfies "want inn at
       (x,y)" — otherwise the reconciler re-issues OrderCreate for the entire
       construction time.
    2. Drop anything illegal (occupied ground, undiscovered map).
    3. Sort by the desired field's `urgency` plane, descending, and cap the
       queue. Ordering is the policy's call, not ours: the field is routinely
       unaffordable and *something* must choose what happens first.
    4. Absence of desire NEVER means demolish. Tearing a building down needs
       `demolishPersistSteps` consecutive policy steps of wanting nothing there
       AND a `commit` below threshold. This is the anti-thrash rule and it is
       the one that matters: a level-triggered controller re-asserts its target
       every step, so a field that flickers would otherwise build and demolish
       on alternate steps and burn the economy doing it.

  Affordability is deliberately NOT gated here. Glob2 lets you place a building
  site without holding the resources — workers deliver them over time — so an
  unaffordable desire degrades into a slow build rather than an invalid order.
  The urgency sort plus the queue cap is the whole of the throttling.
*/

#include "AtlasDesiredState.h"

#include <memory>
#include <unordered_map>
#include <vector>

class Building;
class Game;
class Map;
class Order;
class Player;
class Team;

namespace Atlas
{
	struct ReconcilerConfig
	{
		//! Ticks between policy steps. The engine offers one order per tick
		//! (EngineRun.cpp), so this also sets the burst budget: at 25 the
		//! reconciler may drain up to 25 orders before the next re-diff.
		int policyPeriodTicks = 25;

		//! Consecutive policy steps a cell must be desired-empty, while
		//! holding a building, before demolition is allowed. At the default
		//! policy period this is 8 seconds of sustained intent.
		int demolishPersistSteps = 8;

		//! A cell whose `commit` plane is at or above this is never demolished,
		//! however long it has been desired-empty.
		Uint8 commitBlocksDemolish = 128;

		//! Hard cap on orders emitted per policy step. Prevents one wild field
		//! from monopolising the order channel for many seconds.
		size_t maxQueuedOrders = 64;
	};

	//! Per-step counters, for telemetry and for the M0 round-trip test. Purely
	//! diagnostic — nothing here feeds a decision.
	struct ReconcilerStats
	{
		int created = 0;
		int demolished = 0;
		int upgraded = 0;
		int restaffed = 0;
		int flagsRetuned = 0;
		int areaOrders = 0;
		int illegalSkipped = 0;
		int cappedOut = 0;

		void clear() { *this = ReconcilerStats(); }
	};

	/*!
	  Stateless with respect to strategy; stateful only in the small bookkeeping
	  that anti-thrash requires (how long each cell has been desired-empty).
	  That state is per-cell and derivable from replaying the field history, so
	  it is cheap to serialize and safe to lose on load — a reset merely delays
	  the next demolition by `demolishPersistSteps`.
	*/
	class Reconciler
	{
	public:
		//! The reconciler needs a team, not a player: everything it reads
		//! (buildings, area layers, the map) hangs off Team, and taking the
		//! narrower dependency is what lets the harness drive it against a
		//! bare Game with no Player attached.
		void init(Team *team, const ReconcilerConfig &config);

		//! Diff `desired` against the live game state and return the orders
		//! that close the gap, highest urgency first. Returns an empty vector
		//! when the field is absent, malformed, or already satisfied.
		std::vector<std::shared_ptr<Order>> plan(const DesiredState &desired);

		const ReconcilerStats &stats() const { return stats_; }
		const ReconcilerConfig &config() const { return config_; }

	private:
		//! A pending order plus the urgency it inherited from its cell.
		struct Candidate
		{
			std::shared_ptr<Order> order;
			Uint8 urgency = 0;
			//! Tie-break so equal-urgency candidates keep a stable, explainable
			//! order rather than depending on unordered_map iteration.
			Uint32 sequence = 0;
		};

		//! Index this team's live buildings by their anchor cell.
		void indexObserved();

		void planBuildings(const DesiredState &desired, std::vector<Candidate> &out);
		void planAreas(const DesiredState &desired, std::vector<Candidate> &out);

		//! True when a building of `shortType` may legally be placed with its
		//! top-left at (x,y): the map is discovered there and the footprint is
		//! free. Virtual buildings (flags) skip the ground-occupancy test.
		bool canPlace(int shortType, int x, int y) const;

		Team *team_ = nullptr;
		Game *game_ = nullptr;
		Map *map_ = nullptr;
		ReconcilerConfig config_;
		ReconcilerStats stats_;

		//! Anchor cell index -> live building at that cell.
		std::unordered_map<size_t, Building *> observed_;

		//! Consecutive policy steps each cell has been desired-empty while
		//! holding a building. Sized w*h on first plan().
		std::vector<Uint16> emptyStreak_;

		Uint32 sequence_ = 0;
	};
} // namespace Atlas
