// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	// --- Phase-1 economy tuning -------------------------------------------
	// Hand-picked thresholds for the v0 rules. These are AI design choices for
	// a brand-new AI (not ported engine mechanics), so they are tunable; later
	// phases / an ML policy replace this whole function. The goal of Phase 1 is
	// a colony that feeds itself, GROWS to a target size, and then HOLDS there
	// instead of overpopulating into starvation.

	/// Target colony size. The policy halts unit production at this population.
	static const int GROWTH_UNIT_MAX = 24;
	/// Sustainable population per unit of feeding capacity. AICastor's foodLock
	/// uses `unitSum >= foodSum << 1` (2x), but Castor also actively manages wheat
	/// supply (clearing flags); Cortex does not yet, so its effective carrying
	/// capacity is lower — be conservative until farming management lands.
	static const int FEED_SUSTAIN_MULT = 2;
	/// Halt production if at least this percent of the colony is actively
	/// starving (losing HP) — the reactive safety net for when feedCapacity
	/// overestimates (e.g. an inn exists but its wheat is exhausted).
	static const int STARVE_HALT_PERCENT = 6;
	/// Halt production earlier, while units are merely hungry (not yet losing
	/// HP), to catch a food shortfall before it becomes a death spiral. Mirrors
	/// Nicowar halving production on its hungry-fraction trigger.
	static const int HUNGRY_HALT_PERCENT = 20;
	/// Cap on swarms during the economy phase (more = faster repopulation).
	static const int MAX_SWARMS = 3;

	/// First valid candidate slot for `type` (the placement helper already ranks
	/// them best-first), or -1 if the observation surfaced no legal location.
	static int firstValidCandidate(const CortexObservation& obs, int type)
	{
		for (int slot = 0; slot < CORTEX_BUILD_CANDIDATES; slot++)
			if (obs.buildCandidates[type][slot].valid)
				return slot;
		return -1;
	}

	CortexPolicy::CortexPolicy()
	{
	}

	CortexAction CortexPolicy::decide(const CortexObservation& obs)
	{
		// Reject an observation built against a layout this policy wasn't
		// written for, or one that was never populated. Either way: do nothing.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		const Sint32 inns          = cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD);
		const Sint32 innSites      = cortexBuildingSites(obs, CORTEX_BUILD_FOOD);
		const Sint32 swarms        = cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM);
		const Sint32 swarmSites    = cortexBuildingSites(obs, CORTEX_BUILD_SWARM);

		// How many units the colony can actually sustain right now.
		const Sint32 sustainable   = obs.feedCapacity * FEED_SUSTAIN_MULT;
		const Sint32 starvingPct   = (obs.totalUnit > 0)
			? (obs.starvingUnits * 100 / obs.totalUnit) : 0;
		const Sint32 hungryPct     = (obs.totalUnit > 0)
			? (obs.needFood * 100 / obs.totalUnit) : 0;

		const bool atPopGoal       = (obs.totalUnit >= GROWTH_UNIT_MAX);
		const bool starving        = (starvingPct >= STARVE_HALT_PERCENT);
		const bool hungry          = (hungryPct >= HUNGRY_HALT_PERCENT);
		// feedCapacity == 0 before the first inn: don't treat the bootstrap colony
		// as "over capacity" or it would halt the workers needed to build that inn.
		const bool overCapacity    = (obs.feedCapacity > 0 && obs.totalUnit >= sustainable);
		const bool shouldGrow      = !atPopGoal && !overCapacity && !starving && !hungry;

		// --- Production control (priority 1: this is what bounds population). ---
		// The policy is pure, so it can't read swarm ratios directly — it uses
		// obs.swarmsProducing (count of finished swarms currently producing) to
		// tell whether a halt/resume order is actually needed, and the action
		// layer dedups per-swarm, so re-deciding the same intent each cycle is free.
		if (!shouldGrow && obs.swarmsProducing > 0)
			return makeSetProductionAction(0, 0, 0); // halt: hold population steady.
		if (shouldGrow && obs.swarmsProducing < swarms)
			return makeSetProductionAction(1, 0, 0); // grow: workers only in the economy phase.

		// --- Food capacity (priority 2: build inns to raise the sustainable cap). ---
		// One inn at a time. Build when there is no inn, when units are hungry with
		// nowhere to eat, or when current capacity can't yet sustain the pop goal.
		if (innSites == 0)
		{
			const bool noInnYet      = (inns == 0 && obs.totalUnit > 0);
			const bool hungryNoInn   = (obs.needFoodNoInns > 0);
			const bool capacityShort = (obs.totalUnit > 0 && sustainable < GROWTH_UNIT_MAX);
			if (noInnYet || hungryNoInn || capacityShort)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_FOOD, slot);
			}
		}

		// --- Swarms (priority 3: production capacity, only while growing). ---
		// Needs an inn first (so new units have somewhere to eat), one site at a
		// time, capped. A fresh swarm defaults to producing workers, which the
		// production-control step above will halt once the colony reaches its cap.
		if (shouldGrow && swarmSites == 0 && inns > 0 && swarms < MAX_SWARMS)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}

		return makeNoOpAction();
	}
}
