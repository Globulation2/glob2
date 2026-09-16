// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Neurotica reconciler: desired state + observed state -> orders.

  This is the actuator half of the declarative design described in
  NeuroticaDesiredState.h. It is deliberately mechanical. Every gram of strategy
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

#include "NeuroticaDesiredState.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Building;
class Game;
class Map;
class Order;
class Player;
class Team;

namespace Neurotica
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

		//! Greatest distance, in tiles, over which an existing flag may be
		//! retargeted to satisfy a desire for a flag of the same type instead
		//! of building a new one. See the flag-identity note in the .cpp:
		//! a desired-state field describes configurations, not objects, so
		//! "flag moved" and "flag replaced" are the same picture and the
		//! reconciler has to choose a reading.
		//!
		//! 8 matches AI_NICOWAR_DEFENSE_FLAG_MAX_MOVE_TILES. A long move is
		//! worse than a fresh flag, not better: the flag arrives instantly but
		//! the units assigned to it have to walk the whole way, so a flag
		//! dragged across the map takes its garrison out of the game twice.
		int maxFlagMoveDist = 8;

		//! How far from its preferred cell a building may be relocated when
		//! that cell is not buildable. 0 disables relocation, restoring the
		//! exact-coordinate behaviour.
		int placementSearchRadius = 6;

		//! How long a relocated placement stays bound to its desire while the
		//! construction site has not yet appeared, in ticks. Covers the gap
		//! between issuing OrderCreate and the building becoming observable.
		int pendingBindingTicks = 200;

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
		int flagsMoved = 0;
		int swarmsRetuned = 0;
		int repriorised = 0;
		int areaOrders = 0;
		int illegalSkipped = 0;
		//! Breakdown of illegalSkipped by cause. A desire dropped for fog is
		//! waiting on exploration; one dropped for an occupied footprint is
		//! waiting on something being cleared or moved. The two want opposite
		//! fixes, so counting them together hides the diagnosis.
		int illegalFog = 0;
		int illegalOccupied = 0;
		//! Desires satisfied at a cell other than the one the field preferred.
		int relocated = 0;
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
		//! `tick` is the current game tick, used to expire pending relocation
		//! bindings. Defaults to 0 for callers that do not model time.
		std::vector<std::shared_ptr<Order>> plan(const DesiredState &desired, Uint32 tick = 0);

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

		//! Cells whose diff is already handled by a flag move, so the main
		//! pass must neither create at the destination nor demolish at the
		//! source.
		struct FlagPlan
		{
			//! Destination cell -> the flag being retargeted there. Held as a
			//! building rather than a bare flag so the main pass can still
			//! reconcile the destination's staffing, radius and min-level: a
			//! moved flag that keeps its old garrison size is half a move.
			std::unordered_map<size_t, Building *> moved;
			//! Source cells, which must not be read as buildings abandoned by
			//! the field and demolished.
			std::unordered_set<size_t> vacated;
		};

		//! Match unmet flag desires against flags the field no longer wants
		//! where they stand, and retarget rather than rebuild.
		void planFlagMoves(const DesiredState &desired, FlagPlan &plan,
		                   std::vector<Candidate> &out);

		//! Expire dead bindings and rebuild boundCells_.
		void refreshBindings(const DesiredState &desired, Uint32 tick);

		void planBuildings(const DesiredState &desired, const FlagPlan &plan,
		                   std::vector<Candidate> &out);
		void planAreas(const DesiredState &desired, std::vector<Candidate> &out);

		//! True when a building of `shortType` may legally be placed with its
		//! top-left at (x,y): the map is discovered there and the footprint is
		//! free. Virtual buildings (flags) skip the ground-occupancy test.
		bool canPlace(int shortType, int x, int y);

		//! Best legal cell for `shortType` within placementSearchRadius of
		//! (x,y), excluding cells already claimed this step. Returns false when
		//! nothing nearby is legal. Ranked by the field's score, then by
		//! distance — the score is the policy's opinion and outranks mere
		//! proximity, but proximity breaks ties so relocation stays local.
		bool findNearbyPlacement(const DesiredState &desired, int shortType, Sint32 x, Sint32 y,
		                         const std::unordered_set<size_t> &claimed, Sint32 &outX,
		                         Sint32 &outY);

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

		/*!
		  Where a desire was actually satisfied, when that was not the cell it
		  named.

		  Relocation breaks the property the whole level-triggered design rests
		  on: that re-asserting a satisfied desire is a no-op. A building the
		  field wanted at C but which had to go to C' leaves C looking empty, so
		  the next diff relocates it again, and again, while the copies at C'
		  are orphans the demolition path then tears down. The first attempt at
		  this built more buildings than the teacher and lost faster.

		  This is AIEcho's BuildingRegister lesson in miniature: it registers an
		  id for a building before the building exists and carries it through
		  pending to found, precisely so intent stays bound to its realisation.
		  Binding by cell rather than gid because OrderCreate does not report
		  the gid it will produce, so the site's arrival has to be observed.
		*/
		struct Binding
		{
			size_t actualCell;
			int shortType;
			//! Tick the order was issued, so a binding whose site never
			//! appeared expires instead of blocking the desire forever.
			Uint32 issuedTick;
		};
		std::unordered_map<size_t, Binding> bindings_;

		//! Cells holding a building that satisfies a desire elsewhere. They
		//! must not be read as unwanted and demolished.
		std::unordered_set<size_t> boundCells_;

		Uint32 sequence_ = 0;
		//! Tick of the plan in progress, stamped onto new bindings.
		Uint32 planTick_ = 0;
	};
} // namespace Neurotica
