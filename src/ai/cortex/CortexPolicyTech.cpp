// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	/// Percent of a training building's eligible units that must already be MAXED at
	/// its current level (trained to level+1) before we spend on upgrading it. Below
	/// this the level still has trainees to serve and upgrading would strand them.
	/// The user's spec put this at ~50-70%; 60 is the midpoint, tunable.
	static const int UPGRADE_MAXED_PERCENT = 60;
	/// Minimum FINISHED barracks before an upgrade is allowed. The upgrade blacks a
	/// barracks out for ~hundreds-to-2000 ticks (measured ~1900) during which it
	/// trains and heals no warriors; with only one barracks that is a total army
	/// blackout. Requiring two means an upgrade always leaves one training, and the
	/// laggard-first findUpgradeTarget (AICortex.cpp) lifts them one at a time.
	static const int BARRACKS_MIN_BEFORE_UPGRADE = 2;
	/// Minimum FINISHED inns before an upgrade is allowed, so feeding never hits
	/// zero during the blackout. Paired with the post-upgrade feed-slack check below.
	static const int INN_MIN_BEFORE_UPGRADE = 2;
	/// Hospital (HEAL) expansion. We scale hospital COUNT with ARMY SIZE rather than
	/// the instantaneous needHeal: a hurt warrior out on the offense flag is fighting
	/// or dying, not queued at a hospital, so needHeal badly understates true demand.
	/// One hospital per HOSPITAL_WARRIORS_PER standing warriors keeps heal throughput
	/// (a hospital heals only 2/5/7 units at once at L0/1/2, slowly) ahead of a
	/// growing army. There is NO fixed count cap: the army-size scaling rule IS the
	/// bound (a larger army earns another hospital, a small one does not), so an army
	/// that keeps growing keeps earning sustain instead of plateauing at an arbitrary
	/// ceiling. Hospitals are not individually tracked, so unlike swarms/barracks
	/// there is no tracking-array wall either — the scaling rule alone governs count.
	/// HOSPITAL_WARRIORS_PER is a hand-picked AI design choice, tunable against the
	/// benchmark.
	static const int HOSPITAL_WARRIORS_PER = 8;

	/// Percent of `total` eligible units already trained to >= `servedLevel` on an
	/// ability, from a per-level histogram slice `dist` (an upgradeState row). A unit
	/// at >= servedLevel cannot be improved further by a building of the level that
	/// produces `servedLevel` (== buildingLevel+1), so this is the "% already maxed at
	/// the current building level" the expand-vs-upgrade gate keys on. Returns 0 for
	/// an empty pool so "no units" never reads as "all maxed".
	static int unitsServedPct(const Sint32 dist[CORTEX_UNIT_LEVELS], Sint32 total, int servedLevel)
	{
		if (total <= 0)
			return 0;
		if (servedLevel < 0)
			servedLevel = 0;
		Sint32 served = 0;
		for (int lvl = servedLevel; lvl < CORTEX_UNIT_LEVELS; lvl++)
			served += dist[lvl];
		if (served > total) // guard a transient count race (dist slightly ahead of total).
			served = total;
		return static_cast<int>(served * 100 / total);
	}

	/// Smallest feeding capacity (type->maxUnitInside) among our finished inns, or a
	/// large sentinel when none are tracked. This is the capacity an inn UPGRADE takes
	/// offline (findUpgradeTarget lifts the lowest-level == smallest inn first), so the
	/// inn-upgrade gate checks feedCapacity-minus-this against population first, to
	/// guarantee the blackout never starves the colony.
	static int smallestFinishedInnCapacity(const CortexObservation& obs)
	{
		int smallest = -1;
		for (int i = 0; i < obs.innCount; i++)
		{
			const TrackedBuilding& t = obs.trackedInns[i];
			if (!t.valid)
				continue;
			if (smallest < 0 || t.maxUnitInside < smallest)
				smallest = t.maxUnitInside;
		}
		return (smallest < 0) ? 9999 : smallest; // none tracked → force the spare-inn path.
	}

	/// First valid candidate slot for `type` (the placement helper already ranks
	/// them best-first), or -1 if the observation surfaced no legal location.
	static int firstValidCandidate(const CortexObservation& obs, int type)
	{
		for (int slot = 0; slot < CORTEX_BUILD_CANDIDATES; slot++)
			if (obs.buildCandidates[type][slot].valid)
				return slot;
		return -1;
	}

	// --- Priority 3: school (SCIENCE) — the first tech building. Trains workers'
	// HARVEST (more CORN carried per haul → fuller swarm/inn buffers, easing the
	// very supply pressure the economy lives on) and BUILD (faster construction +
	// raises team maxBuildLevel, the engine gate that unlocks every building
	// upgrade). Built once the economy is established and spare labour exists, so
	// the build crew comes off idle hands rather than off hauling.
	//
	// ALGA gate: a school costs ALGA to build at EVERY level (2/12/10), and algae is
	// harvestable only off water. If no algae is reachable from shore (a landlocked
	// map, or algae walled off by resources/deep water with no ground path) a school
	// site can never be supplied and would stall forever — so we hold the school
	// until a harvestable algae tile is found. This must NOT block the rest of the
	// build-out: the racetrack (Priority 4) is decoupled from the school below so it
	// proceeds anyway, and the engine's maxBuildLevel gate naturally holds upgrades
	// (which need a school) until one exists. Once algae is discovered the school
	// builds on the next cycle.
	ScoredAction CortexPolicy::scoreSchool(const CortexObservation& obs, const DecideFacts& f) const
	{
		// Unfillable-jobs override: when there is open work blocked purely on worker
		// training level (jobs at building levels above the workforce's HARVEST level —
		// f.unfillableNeeded > 0; building/Misc.cpp:127), the first school is no longer
		// optional. Only a school raises HARVEST/BUILD level to unlock those jobs; more
		// workers cannot. So allow building the first school even when the normal
		// canExpand gate (spare labour) would defer it — but never while starving/hungry
		// (reuse canExpand's food-trouble guard) and only with a real spare-labour or
		// blocked-work reason, so we don't pull the last haulers off a labour-tight colony.
		const bool unfillableUrges = (f.unfillableNeeded > 0 && !f.starving && !f.hungry
		 && f.school == 0 && f.schoolSites == 0);
		if (f.combatPhase && (f.canExpand || unfillableUrges) && obs.algaeReachable
		 && f.school == 0 && f.schoolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SCIENCE);
			if (slot >= 0)
				return { SCORE_SCHOOL, makeBuildAction(CORTEX_BUILD_SCIENCE, slot) };
		}
		return cortexDecline();
	}

	// --- Priority 4: racetrack (WALKSPEED) — second tech building. Trains WALK,
	// speeding every unit: shorter hauling round-trips (more economy throughput)
	// and faster army repositioning. Normally held until the school is finished so
	// HARVEST/BUILD land first — BUT the school needs ALGA, so on a map with no
	// reachable algae no school will ever come; we must not let that permanently
	// block the racetrack and everything behind it. Decouple ONLY when the school is
	// genuinely unbuildable (no reachable algae): when algae IS reachable the school
	// is coming, so keep waiting for it (Priority 3 also fires first by ordering, so
	// the racetrack never races ahead of an in-progress school on a normal map).
	ScoredAction CortexPolicy::scoreRacetrack(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.combatPhase && f.canExpand && (f.school > 0 || !obs.algaeReachable)
		 && f.race == 0 && f.raceSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_WALKSPEED);
			if (slot >= 0)
				return { SCORE_RACETRACK, makeBuildAction(CORTEX_BUILD_WALKSPEED, slot) };
		}
		return cortexDecline();
	}

	// --- Priority 5: hospital (HEAL) — survivability for the standing army.
	// The panic path also builds one reactively under attack; this is the
	// planned, non-emergency one once the economy can spare the labour.
	//
	// Gate on economyEstablished (not combatPhase): combatPhase = established AND
	// !starving, so it froze hospital provisioning the moment the colony starved —
	// exactly when a famine army most needs HP sustain. economyEstablished drops the
	// !starving freeze, so the hospital provisions proactively in peace and stays
	// available through a famine rather than vanishing on the first hungry tick.
	// Spare labour (GATE_LABOR in decide()'s gate table) replaces canExpand as the
	// staffing discipline: canExpand folds in !starving/!hungry too, which would
	// re-impose the same freeze; the spare-labour term alone keeps the build crew
	// off idle hands and never steals feeding haulers.
	// Hospitals stay at tech-band scores, so survival (blitz / relocation / feeding)
	// still outranks them during a famine — correct, since a hospital heals HP, not
	// hunger.
	ScoredAction CortexPolicy::scoreHospital(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.economyEstablished && f.heal == 0 && f.healSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
			if (slot >= 0)
				return { SCORE_HOSPITAL, makeBuildAction(CORTEX_BUILD_HEAL, slot) };
		}
		return cortexDecline();
	}

	// --- Priority 5.5: swimming pool (SWIMSPEED) — train SWIM so our workers and
	// warriors can cross water. Built when an explorer has revealed reachable ALGA
	// (a food resource that grows only on water, so harvestable only by swimmers)
	// OR when allowing swim MATERIALLY expands the colony's reachable area — a
	// water-separated wheat patch / stretch of land, or the only-or-much-shorter
	// route to a water-locked enemy. The reach test compares the bounded land-only
	// vs land+water flood-fill counts surfaced by CortexWater (swimWaterReach is
	// always >= swimLandReach; we want a pool when the swim count is more than
	// NUMER/DENOM larger). This mirrors the INTENT of AICastor's computeNeedSwim
	// (the swim-helps predicate). One pool suffices — SWIM is a team-wide trained
	// ability, not per-building capacity. Gated on the established economy + spare
	// labour like the other tech builds, so the build crew comes off idle hands.
	ScoredAction CortexPolicy::scoreSwimmingPool(const CortexObservation& obs, const DecideFacts& f) const
	{
		const Sint32 pool      = cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED);
		const Sint32 poolSites = cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED);
		const bool swimExpandsReach = (obs.swimLandReach > 0
		 && obs.swimWaterReach * CORTEX_SWIM_REACH_GAIN_DENOM
		  > obs.swimLandReach * CORTEX_SWIM_REACH_GAIN_NUMER);
		const bool wantPool = (obs.algaeDiscovered != 0 || swimExpandsReach);
		// Prerequisite: hold the swimming pool until the racetrack is actually built
		// (finished). The racetrack's placement is what defines the colony's compact
		// footprint; SWIM is a later convenience that only makes sense once that core
		// layout exists. Without a finished racetrack, hold the pool regardless of the
		// swim signals.
		const bool poolPrereq = (f.race > 0);
		// Not until the army ramp has actually begun: swimming is not mandatory for
		// workers in the opening, so we hold the pool until warriors are in the swarm
		// mix (warriors > 0, which only happens in the combat phase). This keeps the
		// pool from competing with the early economy/army build-up.
		if (f.combatPhase && f.canExpand && f.warriors > 0 && wantPool && poolPrereq
		 && pool == 0 && poolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWIMSPEED);
			if (slot >= 0)
				return { SCORE_SWIMMING_POOL, makeBuildAction(CORTEX_BUILD_SWIMSPEED, slot) };
		}
		return cortexDecline();
	}

	// --- Priority 6: barracks (ATTACK) — the army pivot. Lets produced warriors
	// train to attack level 1 and get healed between fights. One is enough.
	ScoredAction CortexPolicy::scoreBarracks(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.combatPhase && f.barracks == 0 && f.barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return { SCORE_BARRACKS, makeBuildAction(CORTEX_BUILD_ATTACK, slot) };
		}
		return cortexDecline();
	}

	// --- Priorities 6.3-6.8: unified EXPAND-vs-UPGRADE ladder. ----------------
	// For each training/feeding class we decide, from two signals, whether the
	// current building level still has work (keep it / expand for redundancy) or is
	// mostly done and worth UPGRADING:
	//   • "% of eligible units already maxed at the current level" (unitsServedPct
	//     over the matching upgradeState slice, vs the matching unit pool), and
	//   • spare labour (canExpand) to pay for the build/teardown.
	// obs.upgradableCount[T] already encodes the FULL engine Upgradable predicate
	// (finished, full HP, not already upgrading, has a higher level, the larger
	// footprint fits, and crucially maxBuildLevel > level — so a nonzero count means
	// a school has lifted our BUILD skill and a class-T building can actually go up a
	// level). cortexBuildingsUpgrading(T) guards against stacking two upgrades of the
	// same class. The action layer's findUpgradeTarget picks the laggard (lowest
	// level) instance, so with two buildings it lifts them one at a time.

	// --- Priority 6.3 + 6.5: barracks (ATTACK) — the unit-strength lever. ---
	// Warriors train ATTACK_SPEED+ATTACK_STRENGTH (in parallel) to barracksLevel+1.
	// EXPAND first: an upgrade blacks a barracks out for ~hundreds-to-2000 ticks,
	// during which it trains and heals no warriors — with one barracks that is a
	// total army blackout (the measured ~1900-tick defect this ladder fixes). So we
	// require a SECOND barracks before upgrading; the new one keeps training the
	// warrior stream while the laggard upgrades. Gated on canExpand so the build/
	// teardown comes off idle hands, never off hauling or army production.
	ScoredAction CortexPolicy::scoreBarracksUpgrade(const CortexObservation& obs, const DecideFacts& f) const
	{
		const Sint32 barracksLevel = cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK);
		const int attackMaxedPct   = unitsServedPct(obs.attackStrengthLevel, f.warriors, barracksLevel + 1);
		const bool barracksUpgradeWanted = f.combatPhase && f.canExpand
		 && f.barracks >= 1
		 && obs.upgradableCount[CORTEX_BUILD_ATTACK] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK) == 0
		 && attackMaxedPct >= UPGRADE_MAXED_PERCENT;
		if (barracksUpgradeWanted && f.barracks < BARRACKS_MIN_BEFORE_UPGRADE && f.barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return { SCORE_BARRACKS_UPGRADE, makeBuildAction(CORTEX_BUILD_ATTACK, slot) };
		}
		if (barracksUpgradeWanted && f.barracks >= BARRACKS_MIN_BEFORE_UPGRADE)
			return { SCORE_BARRACKS_UPGRADE, makeUpgradeAction(CORTEX_BUILD_ATTACK) };
		return cortexDecline();
	}

	// --- Priority 6.6: school (SCIENCE) upgrade. ---
	// Workers train BUILD+HARVEST (in parallel) to schoolLevel+1; buildLevel[] is
	// the worker BUILD distribution (only workers have BUILD performance), and
	// because the school upgrades both abilities in one visit HARVEST tracks it, so
	// the BUILD slice alone gates both. Single-instance: a school blackout only
	// pauses worker tech (maxBuildLevel, already earned, does NOT drop), it strands
	// no army and starves no one — and the maxed gate means few workers are waiting.
	// A school UPGRADE consumes ALGA too (12 at L1, 10 at L2), so like the build it
	// needs reachable algae — gate on algaeReachable so an upgrade is never started
	// against a site that can no longer be supplied (e.g. the shoreline algae has
	// since been depleted).
	ScoredAction CortexPolicy::scoreSchoolUpgrade(const CortexObservation& obs, const DecideFacts& f) const
	{
		const Sint32 schoolLevel = cortexMaxFinishedLevel(obs, CORTEX_BUILD_SCIENCE);
		const int buildMaxedPct  = unitsServedPct(obs.buildLevel, obs.workers, schoolLevel + 1);
		// Unfillable-jobs override on the spare-labour gate ONLY: when open work is
		// blocked purely on worker training level (f.unfillableNeeded > 0; jobs at
		// building levels above the workforce's HARVEST level — building/Misc.cpp:127),
		// a school upgrade raises that level and is the only thing that unlocks the
		// jobs. So let unfillable demand stand in for canExpand's spare-labour check —
		// but never while starving/hungry (reuse canExpand's food-trouble guard), and
		// keep EVERY engine-permission check (upgradableCount encodes maxBuildLevel >
		// level + footprint fit; cortexBuildingsUpgrading guards stacking; algaeReachable
		// is the supply requirement) and the maxed-trainees gate exactly as-is — this
		// only relaxes spare labour, never the engine's upgrade permission.
		const bool sparedByUnfillable = (f.unfillableNeeded > 0 && !f.starving && !f.hungry);
		if (f.combatPhase && (f.canExpand || sparedByUnfillable) && f.school >= 1 && obs.algaeReachable
		 && obs.upgradableCount[CORTEX_BUILD_SCIENCE] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_SCIENCE) == 0
		 && buildMaxedPct >= UPGRADE_MAXED_PERCENT)
			return { SCORE_SCHOOL_UPGRADE, makeUpgradeAction(CORTEX_BUILD_SCIENCE) };
		return cortexDecline();
	}

	// --- Priority 6.7: racetrack (WALKSPEED) upgrade. ---
	// Workers AND warriors train WALK to raceLevel+1; walkLevel[] sums both (the
	// racetrack's eligible pool), explorers excluded (zero WALK performance).
	// Single-instance: a racetrack blackout only leaves units at their current
	// speed for the window — no capability loss.
	ScoredAction CortexPolicy::scoreRacetrackUpgrade(const CortexObservation& obs, const DecideFacts& f) const
	{
		const Sint32 raceLevel  = cortexMaxFinishedLevel(obs, CORTEX_BUILD_WALKSPEED);
		const int walkMaxedPct  = unitsServedPct(obs.walkLevel, obs.workers + f.warriors, raceLevel + 1);
		if (f.combatPhase && f.canExpand && f.race >= 1
		 && obs.upgradableCount[CORTEX_BUILD_WALKSPEED] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_WALKSPEED) == 0
		 && walkMaxedPct >= UPGRADE_MAXED_PERCENT)
			return { SCORE_RACETRACK_UPGRADE, makeUpgradeAction(CORTEX_BUILD_WALKSPEED) };
		return cortexDecline();
	}

	// --- Priority 6.8: inn (FOOD) upgrade — spare-first, feed-safe. ---
	// An inn is a feeding building, not a trainer, so there is no "% maxed" signal;
	// the gate is purely feed safety. Upgrading raises maxUnitInside (4→7→17) and
	// speeds feeding, but the blackout removes that inn's whole feeding capacity. We
	// upgrade only with (a) a redundant inn so feeding never hits zero, and (b)
	// enough capacity left over (feedCapacity minus the inn we'd take offline) to
	// still feed the population through the blackout. Feed-led growth (Priority 2)
	// keeps feedCapacity ≈ population, so that slack rarely exists — when an upgrade
	// is wanted but unsafe we build ONE spare inn first to create it (the added inn
	// is at the current max level, ≥ the lowest-level inn we'd upgrade, so one spare
	// suffices). Priority 2 remains the backstop if growth erodes the slack mid-blackout.
	// The finished-first-inn precondition (there must be an inn to upgrade) is
	// GATE_BOOTSTRAP in decide()'s gate table.
	ScoredAction CortexPolicy::scoreInnUpgrade(const CortexObservation& obs, const DecideFacts& f) const
	{
		const bool innUpgradeWanted = f.combatPhase && f.canExpand
		 && obs.upgradableCount[CORTEX_BUILD_FOOD] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_FOOD) == 0;
		if (innUpgradeWanted)
		{
			const int lostCapacity = smallestFinishedInnCapacity(obs);
			const bool feedSlackOk = (obs.feedCapacity - lostCapacity) >= obs.totalUnit;
			const bool innRedundant = (f.inns >= INN_MIN_BEFORE_UPGRADE);
			if (innRedundant && feedSlackOk)
				return { SCORE_INN_UPGRADE, makeUpgradeAction(CORTEX_BUILD_FOOD) };
			// Not safe yet: build one spare inn to create the redundancy / slack.
			if (f.innSites == 0)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return { SCORE_INN_UPGRADE, makeBuildAction(CORTEX_BUILD_FOOD, slot) };
			}
		}
		return cortexDecline();
	}

	// --- Priority 6.9: hospital (HEAL) expand + upgrade. ---
	// More hospitals AND higher-level ones both grow army sustain: a level-L
	// hospital heals maxUnitInside units at once (2/5/7 at L0/1/2) and faster per
	// unit (30/18/6 ticks), so an upgrade is a big jump on both axes.
	//   EXPAND: one hospital per HOSPITAL_WARRIORS_PER standing warriors, with NO
	//     fixed count cap — the army-size scaling rule IS the bound, so a growing
	//     army keeps earning sustain instead of plateauing at an arbitrary ceiling.
	//     The first hospital is Priority 5 / the panic path; this grows the count as
	//     the army grows. (Army-scaled, not needHeal-scaled — see the constant:
	//     wounded warriors out on the flag never queue to heal.)
	//   UPGRADE: only ever upgrade a hospital when a SECOND finished hospital
	//     exists to cover healing (heal >= 2). An upgrade turns the building into
	//     a construction site — a heal blackout for that hospital — so upgrading
	//     the only one would leave the army with nowhere to heal. With a redundant
	//     hospital, the one-at-a-time guard (cortexBuildingsUpgrading == 0) keeps
	//     the other finished and available throughout. This guarantees that once a
	//     hospital is built, at least one stays available to heal units.
	//
	// Gate on economyEstablished + spare labour (GATE_LABOR in decide()'s gate table,
	// not combatPhase + canExpand), same reasoning as scoreHospital: combatPhase's
	// !starving term froze the count exactly when a famine army grew, so the army
	// outran its heal capacity right when it most needed sustain. economyEstablished
	// provisions proactively through peace and famine; spare labour is the staffing
	// discipline without re-importing the !starving freeze that canExpand folds in.
	// Tech-band scores keep survival ahead of hospitals during a famine — correct,
	// since a hospital heals HP, not hunger.
	ScoredAction CortexPolicy::scoreHospitalExpandUpgrade(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.economyEstablished && f.heal >= 1 && f.healSites == 0
		 && f.warriors >= f.heal * HOSPITAL_WARRIORS_PER)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
			if (slot >= 0)
				return { SCORE_HOSPITAL_UPGRADE, makeBuildAction(CORTEX_BUILD_HEAL, slot) };
		}
		if (f.economyEstablished && f.heal >= 2
		 && obs.upgradableCount[CORTEX_BUILD_HEAL] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_HEAL) == 0)
			return { SCORE_HOSPITAL_UPGRADE, makeUpgradeAction(CORTEX_BUILD_HEAL) };
		return cortexDecline();
	}
}
