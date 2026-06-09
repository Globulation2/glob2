// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h"
#include "CortexNet.h"

// AICortex policy layer. Maps an Observation to an Action intent and NOTHING
// else. It must not include Game.h / Team.h / Order.h or touch any engine
// pointer — its entire input is the CortexObservation, its entire output is a
// CortexAction. That isolation is what lets v0 (hand rules, here) be swapped for
// a behavior tree or a neural net later without rewriting observation or action
// code (see docs/AI/cortex/README.md).
//
// DECISION MODEL — utility selection. Every candidate decision is a function
// that SCORES itself from the observation (a ScoredAction: a score plus the
// action it would emit). decide() evaluates them ALL and emits the highest
// scorer. This replaces the old first-match-wins priority ladder: urgency is now
// a number each decision computes, not its position in a list, so a low-urgency
// decision can never be permanently starved behind a higher one — it simply
// loses cycles until its score wins. A score of 0 means "I decline to act this
// cycle"; a positive score with a NoOp action is a deliberate hold (claim the
// cycle, do nothing). See CortexScore for the banding.

namespace Cortex
{
	/// Decision score bands. decide() picks the highest-scoring decision each
	/// cycle, so these define both urgency and the tie-order. They are grouped
	/// into bands with gaps so a future graded score component (e.g. marginal
	/// value) can reorder decisions WITHIN a band without crossing into another.
	/// The initial values are strictly descending in the historical
	/// priority-ladder order, so the converted policy reproduces the old
	/// first-match-wins behaviour exactly as a baseline; tuning the per-decision
	/// scores into genuinely graded utilities is the follow-up work.
	enum CortexScore
	{
		SCORE_NONE               = 0, ///< Decline: do not act this cycle.

		// Existential / preemption — survival and the never-{0,0,0} rule.
		SCORE_PANIC_DEFENSE      = 10000,
		SCORE_PRODUCTION_CONTROL =  9000,
		SCORE_FEED_CAPACITY      =  8000,
		SCORE_SWARM_RECOVERY     =  7000,

		// Economy / tech build-out band.
		SCORE_SCHOOL             =  6000,
		SCORE_RACETRACK          =  5900,
		SCORE_HOSPITAL           =  5800,
		SCORE_SWIMMING_POOL      =  5700,
		SCORE_BARRACKS           =  5600,
		SCORE_BARRACKS_UPGRADE   =  5500,
		SCORE_SCHOOL_UPGRADE     =  5400,
		SCORE_RACETRACK_UPGRADE  =  5300,
		SCORE_INN_UPGRADE        =  5200,
		SCORE_HOSPITAL_UPGRADE   =  5100,

		// Second swarm — GRADED, computed in scoreSecondSwarm as
		// SCORE_SECOND_SWARM_BASE + severity*SCORE_SECOND_SWARM_STEP where severity
		// is the worst wheat-bottlenecked swarm's CORN deficit (1..5). It fires only
		// when an existing swarm is pinned at the worker cap with a draining CORN
		// buffer — the wheat catchment is the bottleneck, and a fresh swarm on a new
		// patch is the cure. Lands at 6200..6600: ABOVE the whole tech/upgrade band
		// (more valuable than another upgrade when wheat is the binding constraint)
		// but below swarm recovery (7000) and a first/healthy inn (8000).
		SCORE_SECOND_SWARM_BASE  =  6100,
		SCORE_SECOND_SWARM_STEP  =   100,
		// FAMINE RELOCATION. When the colony is established BUT starving (foodSaturated)
		// because its wheat catchment is spent, the second swarm is not a luxury upgrade
		// — it is the escape from the depletion trap (relocating the economy onto a fresh
		// patch is what lifts feedCapacity back off zero). A valid swarm candidate
		// guarantees real fresh wheat to move to, so relocation outranks the wheat-blitz
		// (SCORE_OFFENSE_BLITZ=6700, the spend-the-army-before-we-die desperation):
		// recovery beats a hail-mary when recovery is genuinely possible. Sits ABOVE the
		// blitz but still below swarm recovery (7000) and survival/production-control.
		// scoreSecondSwarm's swarmSites==0 guard makes this claim only the ONE cycle that
		// places the swarm; the standing war flag then lets the blitz resume, so the two
		// coexist (we both spend the army AND fix the economy).
		SCORE_SECOND_SWARM_FAMINE =  6800,
		// A wheat-bottlenecked inn — GRADED DOWN. When the feeding deficit is a
		// wheat-SUPPLY problem (a swarm is bottlenecked) another inn cannot be
		// stocked, so its marginal value collapses below the second-swarm score and
		// the tech band. Above retire/offense but below defense's flag work.
		SCORE_FEED_BOTTLENECKED  =  3500,

		// Wheat-blitz offense. During a famine (foodSaturated) the colony will die
		// in place if it sits still, so spending the army NOW outranks building more
		// (unfeedable) economy: above the whole economy/tech build-out band
		// (SCORE_SCHOOL=6000, second-swarm tops out ~6600) but below
		// SCORE_SWARM_RECOVERY=7000 and SCORE_FEED_CAPACITY=8000 — it never preempts
		// survival/production-control. Only the foodSaturated blitz uses this score;
		// the normal turtle-then-commit offense stays low (SCORE_OFFENSE below).
		SCORE_OFFENSE_BLITZ      =  6700,

		// Military band (repositioning standing flags; below economy by design —
		// the pre-combat emergency is SCORE_PANIC_DEFENSE, not these).
		SCORE_DEFENSE            =  4000,
		SCORE_RETIRE_FLAG        =  3000,
		SCORE_OFFENSE            =  2000,
	};

	/// A scored decision: the score (0 == decline; higher wins) and the action
	/// to emit if this decision wins the cycle. The action may be a deliberate
	/// NoOp paired with a positive score — that claims the cycle and does
	/// nothing, exactly as the old ladder did when a rung returned NoOp to halt.
	struct ScoredAction
	{
		int score;
		CortexAction action;
	};

	/// Convenience: "I decline to act this cycle" (score 0, NoOp action).
	inline ScoredAction cortexDecline()
	{
		return ScoredAction{ SCORE_NONE, makeNoOpAction() };
	}

	/// Per-cycle decision-selection trace, filled by decide() only when a caller
	/// asks for it (the optional out-param). eligibleMask has bit k set for each
	/// candidate k whose hand score > SCORE_NONE (i.e. its decline gate passed);
	/// chosen is the winning candidate's class index, or -1 when nothing was
	/// eligible (the initial NoOp held). This is the per-cycle ML training label
	/// (eligibility mask + the hand rule's choice); see DECIDE_CONTRACT.md.
	struct DecideTrace
	{
		Uint32 eligibleMask;
		int chosen;
	};

	class CortexPolicy
	{
	public:
		/// Number of features in the decide() feature vector (the ML decision-net
		/// input width). The single source of truth for the trace CSV columns and
		/// the future inference path; see docs/AI/cortex/DECIDE_CONTRACT.md for the
		/// fixed idx order. extractDecideFeatures fills exactly this many.
		static const int NUM_DECIDE_FEATURES = 48;

		CortexPolicy();

		/// Decide the next action intent from the current observation. Scores
		/// every candidate decision and returns the highest-scoring action (NoOp
		/// when none wants to act). Both engine bindings share this.
		///
		/// When `trace != nullptr` it is filled with the per-cycle eligibility mask
		/// and the winning class index (the ML training label) — a pure read-out of
		/// the decision already made. Behaviour is byte-identical to the trace ==
		/// nullptr path (the trace is determinism-neutral: no RNG, no orders, no
		/// persisted state). See DecideTrace and docs/AI/cortex/DECIDE_CONTRACT.md.
		CortexAction decide(const CortexObservation& obs, DecideTrace* trace = nullptr);

		/// Fill `features` with the 48-element decision feature vector in the EXACT
		/// idx order of docs/AI/cortex/DECIDE_CONTRACT.md. SINGLE SOURCE OF TRUTH:
		/// the trace CSV columns and the future decision-net inference path both
		/// reuse this. Pure function of the observation (raw colony state — no
		/// derived judgment booleans); reuses computeFacts for the building counts
		/// and the fillable/unfillable open-job partition.
		static void extractDecideFeatures(const CortexObservation& obs,
		                                   int features[NUM_DECIDE_FEATURES]);

		/// Worker-hauling tuning, evaluated EVERY decision cycle in PARALLEL with
		/// decide()'s single primary action — NOT as a competing decision the
		/// build/upgrade/offense scorers could starve or be delayed by. Returns an
		/// ACTION_TUNE_WORKERS action setting each tracked swarm/inn/site's
		/// maxUnitWorking, or ACTION_NOOP when nothing crosses a threshold this cycle.
		/// Keeping existing buildings fed is independent of starting new ones: the
		/// tune emits OrderModifyBuilding (a worker-count change), which need not
		/// contend for the cycle's one build/upgrade slot — the action layer drains
		/// both alongside each other, exactly like wantWheatProtection().
		CortexAction tuneWorkers(const CortexObservation& obs) const;

		/// Wheat-forbidden upkeep decision, evaluated EVERY decision cycle in
		/// PARALLEL with decide()'s single primary action — not as a competing
		/// decision the build/upgrade scorers could starve. Painting the checkerboard
		/// is area-paint (OrderAlterateForbidden), not an OrderCreate, so it need not
		/// contend for the cycle's one action slot. The policy still owns the gate:
		/// true only when the colony is not starving (never wall off wheat while the
		/// colony is dying) and the reconcile has real work (newly-revealed wheat to
		/// forbid, or wheat gone/out of view to un-forbid). The open-margin N feeds
		/// the executor from obs.wheatOpenMargin (the ML seam — a learned policy later
		/// outputs it). The action layer (AICortex::enqueueWheatForbidden) rebuilds
		/// the full ADD/DEL tile masks and emits the orders.
		bool wantWheatProtection(const CortexObservation& obs) const;

		/// Wheat-blitz lift gate, evaluated EVERY decision cycle in PARALLEL with
		/// decide() (alongside wantWheatProtection). True exactly when the wheat-blitz
		/// is active: the colony is past wheat capacity and starving (foodSaturated)
		/// with a committable army and a scouted target. When true the wheat executor
		/// runs in lift-all mode (un-forbid the WHOLE field for a one-time harvest
		/// burst to fuel the attack) instead of the steady-state checkerboard. This is
		/// a deliberate strategic-mode override, NOT a change to the reconcile invariant
		/// (which only retires paint when wheat is visibly depleted); normal protection
		/// resumes once the famine ends. Mutually exclusive with wantWheatProtection
		/// (which returns false while starving), and takes precedence when both could
		/// apply, so the executor never double-emits.
		bool wantWheatBlitzLift(const CortexObservation& obs) const;

	private:
		/// Facts derived ONCE from the (const) observation at the top of decide() and
		/// shared, unchanged, by every score helper. obs is a const input, so none
		/// of these can change during a single decide() call — caching them just avoids
		/// recomputing the same finished-building counts / ratios many times.
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
			bool economyEstablished;  ///< inn+swarm+pop established, REGARDLESS of starvation.
			bool foodSaturated;       ///< established AND starving — past what wheat can feed (famine).
			bool canExpand;

			int growWorker, growExplorer, growWarrior;
			bool panic;

			int fillableNeeded;    ///< open jobs at building levels the current workforce (HARVEST<=maxBuildLevel) can staff.
			int unfillableNeeded;  ///< open jobs at building levels above the workforce's level — only training (a school) clears these, not more workers.
		};

		/// Build the shared fact bundle from the observation (see DecideFacts).
		static DecideFacts computeFacts(const CortexObservation& obs);

		// --- decide() decision scorers ----------------------------------------
		// Each returns a ScoredAction: a CortexScore-band score plus the action it
		// would emit, or cortexDecline() (score 0) when it does not want to act this
		// cycle. They take the same (const obs, const facts) inputs and mutate
		// nothing. decide() evaluates every one and emits the highest scorer.

		/// Pre-combat panic defense (and the steady-state priority-split branch that
		/// shares its if/else-if).
		ScoredAction scorePanicDefense(const CortexObservation& obs, const DecideFacts& f) const;
		/// Production control (the swarm-ratio block). Declines while panicking.
		ScoredAction scoreProductionControl(const CortexObservation& obs, const DecideFacts& f) const;
		/// Feed capacity (inns).
		ScoredAction scoreFeedCapacity(const CortexObservation& obs, const DecideFacts& f) const;
		/// Swarm recovery (rebuild a destroyed-only swarm).
		ScoredAction scoreSwarmRecovery(const CortexObservation& obs, const DecideFacts& f) const;
		/// School (SCIENCE) — first tech building.
		ScoredAction scoreSchool(const CortexObservation& obs, const DecideFacts& f) const;
		/// Racetrack (WALKSPEED) — second tech building.
		ScoredAction scoreRacetrack(const CortexObservation& obs, const DecideFacts& f) const;
		/// Hospital (HEAL) — planned first hospital.
		ScoredAction scoreHospital(const CortexObservation& obs, const DecideFacts& f) const;
		/// Swimming pool (SWIMSPEED).
		ScoredAction scoreSwimmingPool(const CortexObservation& obs, const DecideFacts& f) const;
		/// Barracks (ATTACK) — the army pivot.
		ScoredAction scoreBarracks(const CortexObservation& obs, const DecideFacts& f) const;
		/// Barracks expand-then-upgrade.
		ScoredAction scoreBarracksUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// School upgrade.
		ScoredAction scoreSchoolUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Racetrack upgrade.
		ScoredAction scoreRacetrackUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Inn (FOOD) upgrade — spare-first, feed-safe.
		ScoredAction scoreInnUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Hospital expand + upgrade.
		ScoredAction scoreHospitalExpandUpgrade(const CortexObservation& obs, const DecideFacts& f) const;
		/// Second swarm on a freshly-discovered wheat patch.
		ScoredAction scoreSecondSwarm(const CortexObservation& obs, const DecideFacts& f) const;
		/// Defense (recall the army to a threatened building).
		ScoredAction scoreDefense(const CortexObservation& obs, const DecideFacts& f) const;
		/// Retire a purposeless war flag.
		ScoredAction scoreRetireFlag(const CortexObservation& obs, const DecideFacts& f) const;
		/// Offense (plant the war flag on the nearest known enemy).
		ScoredAction scoreOffense(const CortexObservation& obs, const DecideFacts& f) const;

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

		// --- ML decision-selection policy (DECIDE pilot) ----------------------
		// When GLOB2_CORTEX_POLICY=ml-decide and a net loads from
		// GLOB2_CORTEX_DECISION_NET, decide() selects among the eligible candidates
		// with decisionNet_'s learned utility scores instead of the hand SCORE_*
		// argmax. The hand decline gates (eligibility) are unchanged — only the
		// selection among eligible candidates is learned (DECIDE_CONTRACT.md). The
		// net is integer/I16F16 and the choice is a pure function of the observation,
		// so orders stay deterministic in lockstep — every client must load the SAME
		// blob. Loaded once in the ctor; mlDecide_ stays false (→ hand argmax) if
		// loading fails. This mode is INDEPENDENT of the worker-cap "ml" mode above:
		// a user selects exactly one via GLOB2_CORTEX_POLICY; combining is out of scope.
		bool mlDecide_;
		CortexNet decisionNet_;
	};
}
