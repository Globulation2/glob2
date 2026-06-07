// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"
#include "CortexNet.h"

#include <optional>

// AICortex policy layer. Maps an Observation to an Action intent and NOTHING
// else. It must not include Game.h / Team.h / Order.h or touch any engine
// pointer — its entire input is the CortexObservation, its entire output is a
// CortexAction. That isolation is what lets v0 (hand rules, here) be swapped for
// a behavior tree or a neural net later without rewriting observation or action
// code (see docs/AI/cortex/README.md).

namespace Cortex
{
	class CortexPolicy
	{
	public:
		CortexPolicy();

		/// Decide the next action intent from the current observation.
		/// Scaffold: always returns NoOp. Both engine bindings share this.
		CortexAction decide(const CortexObservation& obs);

		/// Worker-hauling tuning, evaluated EVERY decision cycle in PARALLEL with
		/// decide()'s single primary action — NOT as a competing ACTION_* the
		/// build/upgrade/offense ladder could starve or be delayed by. Returns an
		/// ACTION_TUNE_WORKERS action setting each tracked swarm/inn/site's
		/// maxUnitWorking, or ACTION_NOOP when nothing crosses a threshold this cycle.
		/// Keeping existing buildings fed is independent of starting new ones: the
		/// tune emits OrderModifyBuilding (a worker-count change), which need not
		/// contend for the cycle's one build/upgrade slot — the action layer drains
		/// both alongside each other, exactly like wantWheatProtection().
		CortexAction tuneWorkers(const CortexObservation& obs) const;

		/// Wheat-forbidden upkeep decision, evaluated EVERY decision cycle in
		/// PARALLEL with decide()'s single primary action — not as a competing
		/// ACTION_* the build/upgrade ladder could starve. Painting the checkerboard
		/// is area-paint (OrderAlterateForbidden), not an OrderCreate, so it need not
		/// contend for the cycle's one action slot. The policy still owns the gate:
		/// true only when the colony is not starving (never wall off wheat while the
		/// colony is dying) and the reconcile has real work (newly-revealed wheat to
		/// forbid, or wheat gone/out of view to un-forbid). The open-margin N feeds
		/// the executor from obs.wheatOpenMargin (the ML seam — a learned policy later
		/// outputs it). The action layer (AICortex::enqueueWheatForbidden) rebuilds
		/// the full ADD/DEL tile masks and emits the orders.
		bool wantWheatProtection(const CortexObservation& obs) const;

	private:
		/// Facts derived ONCE from the (const) observation at the top of decide() and
		/// shared, unchanged, by every priority helper. obs is a const input, so none
		/// of these can change during a single decide() call — caching them just avoids
		/// recomputing the same finished-building counts / ratios many times. The VALUES
		/// are identical to what the old inline decide() computed; only the redundant
		/// recomputation is removed.
		struct DecideFacts
		{
			Sint32 inns, innSites;
			Sint32 swarms, swarmSites;
			Sint32 barracks, barracksSites;
			Sint32 school, schoolSites;
			Sint32 heal, healSites;
			Sint32 race, raceSites;
			Sint32 warriors;

			bool starving, hungry;
			bool combatPhase;
			bool canExpand;

			int growWorker, growExplorer, growWarrior;
			bool panic;

			int fillableNeeded;    ///< open jobs at building levels the current workforce (HARVEST<=maxBuildLevel) can staff.
			int unfillableNeeded;  ///< open jobs at building levels above the workforce's level — only training (a school) clears these, not more workers.
		};

		/// Build the shared fact bundle from the observation (see DecideFacts).
		static DecideFacts computeFacts(const CortexObservation& obs);

		// --- decide() priority helpers ----------------------------------------
		// Each returns the action for its priority when that priority fires this
		// cycle, or std::nullopt to fall through to the next. decide() evaluates them
		// in the SAME order the old inline ladder did and returns the first engaged
		// one — preserving the original first-match-wins precedence exactly. They take
		// the same (const obs, const facts) inputs and mutate nothing.

		/// Priority 0: pre-combat panic defense (and the steady-state priority-split
		/// branch that shares its if/else-if). Returns an action while it still has
		/// setup work; nullopt once panic setup is complete (so the economy runs) or
		/// when no panic and the split already holds.
		std::optional<CortexAction> tryPanicDefense(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 1: production control (the swarm-ratio block). nullopt while panicking.
		std::optional<CortexAction> tryProductionControl(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 2: feed capacity (inns).
		std::optional<CortexAction> tryFeedCapacity(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 2.5: swarm recovery (rebuild a destroyed-only swarm).
		std::optional<CortexAction> trySwarmRecovery(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 3: school (SCIENCE) — first tech building.
		std::optional<CortexAction> trySchool(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 4: racetrack (WALKSPEED) — second tech building.
		std::optional<CortexAction> tryRacetrack(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 5: hospital (HEAL) — planned first hospital.
		std::optional<CortexAction> tryHospital(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 5.5: swimming pool (SWIMSPEED).
		std::optional<CortexAction> trySwimmingPool(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6: barracks (ATTACK) — the army pivot.
		std::optional<CortexAction> tryBarracks(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priorities 6.3+6.5: barracks expand-then-upgrade.
		std::optional<CortexAction> tryBarracksUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6.6: school upgrade.
		std::optional<CortexAction> trySchoolUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6.7: racetrack upgrade.
		std::optional<CortexAction> tryRacetrackUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6.8: inn (FOOD) upgrade — spare-first, feed-safe.
		std::optional<CortexAction> tryInnUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6.9: hospital expand + upgrade.
		std::optional<CortexAction> tryHospitalExpandUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 6.95: second swarm on a freshly-discovered wheat patch.
		std::optional<CortexAction> trySecondSwarm(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 7: defense (recall the army to a threatened building).
		std::optional<CortexAction> tryDefense(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 7.2: retire a purposeless war flag.
		std::optional<CortexAction> tryRetireFlag(const CortexObservation& obs, const DecideFacts& f) const;
		/// Priority 8: offense (plant the war flag on the nearest known enemy).
		std::optional<CortexAction> tryOffense(const CortexObservation& obs, const DecideFacts& f) const;

		// --- ML swarm worker-cap policy (effort B pilot) ----------------------
		// When GLOB2_CORTEX_POLICY=ml and a net loads from GLOB2_CORTEX_NET,
		// tuneWorkers() picks each SWARM's worker cap from swarmNet_ instead of the
		// hand rule (inn/site caps stay hand-coded). The net is integer/I16F16 and
		// the choice is a pure function of the observation, so orders stay
		// deterministic in lockstep — but every client must load the SAME blob (a
		// deployment concern; for the headless benchmark it is one process). Loaded
		// once in the ctor; mlSwarmCaps_ stays false (→ hand rule) if loading fails.
		bool mlSwarmCaps_;
		CortexNet swarmNet_;
	};
}
