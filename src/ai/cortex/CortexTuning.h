// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexConstants.h"

namespace Cortex
{
	/// Tunable policy knobs for the offline knob search (see
	/// docs/AI/cortex/DEVLOG.md, rank-gate tuning). Loaded ONCE per process from
	/// the file named by GLOB2_CORTEX_TUNING (an ABSOLUTE path — glob2 chdir()s at
	/// startup), before the first decision cycle, so every per-tick decision stays
	/// a pure, deterministic function of the observation. In lockstep multiplayer
	/// every client must load the SAME file (same deployment rule as the ML nets);
	/// for the headless benchmark it is one process. The DEFAULTS REPRODUCE THE
	/// COMMITTED BEHAVIOR EXACTLY — with the env var unset (or a file that only
	/// restates defaults) the binary is replay-identical to the untuned build.
	///
	/// File format: one "key value" pair per line, keys named exactly like the
	/// fields below; '#' starts a comment. Any unknown key, unparsable/out-of-range
	/// value, duplicate key, or unreadable file ABORTS the process — a search
	/// config that fails to apply must fail loudly, not silently run defaults.
	struct CortexTuning
	{
		// --- second-swarm trigger face (when "wants a fresh patch" fires) -----
		/// CORN level below which a cap-pinned swarm counts as DRAINING (the
		/// expansion-trigger use of CORTEX_SWARM_CORN_ADD_LO, split from the
		/// hauler-tuning use, which stays on the constant). Also the severity
		/// scale's top: field-depleted severity == this value.
		int expandCornLo = CORTEX_SWARM_CORN_ADD_LO;
		/// The swarm hauler cap: tuneWorkers' early/mid-game maxUnitWorking
		/// ceiling AND the "pinned at the cap" test of the capped-draining face
		/// (one knob on purpose — "capped" always means the operative cap).
		int swarmWorkerCap = CORTEX_SWARM_WORKER_CAP;
		/// CORN level at which tuneWorkers releases a hauler. Governs the
		/// cap-latch: on thin-wheat maps the buffer never reaches the release
		/// line, so maxUnitWorking sticks at the cap and the capped-draining face
		/// fires on every production-cycle corn dip.
		int swarmCornRemHi = CORTEX_SWARM_CORN_REM_HI;
		/// Harvestable-wheat tile count below which a catchment is DEAD: the
		/// field-depleted trigger face and tuneWorkers' wheat-starved single-
		/// hauler clamp (shared, as in the committed code).
		int wheatStarvedTiles = CORTEX_SWARM_WHEAT_STARVED_TILES;
		/// Wheat-abundance veto on the capped-draining face: when > 0, a swarm
		/// with harvestableWheatNearby >= this many tiles is NOT capped-draining —
		/// a low corn buffer beside plentiful wheat is production-cycle noise, not
		/// a spent catchment (the Muka seed-1 misfire: corn=3 with 47 tiles).
		/// 0 = veto disabled (committed behavior).
		int expandWheatVeto = 0;
		/// Consecutive decision cycles (~25 ticks each) anySwarmWantsFreshPatch
		/// must hold before the second-swarm scorer may fire. 1 = fire on the
		/// first cycle (committed behavior); higher values debounce transient
		/// corn-buffer dips that self-heal within a few cycles.
		int expandDebounceCycles = 1;

		// --- second-swarm ranking (when it wins the cycle) --------------------
		/// Kept in lockstep with CortexScore::SCORE_SECOND_SWARM_BASE/_STEP by a
		/// static_assert in CortexPolicyEconomy.cpp.
		int scoreSecondSwarmBase = 6100;
		int scoreSecondSwarmStep = 100;
		/// Minimum fresh-patch severity (1..expandCornLo) the worst swarm must
		/// reach before the scorer fires at all; expandCornLo = field-depleted
		/// only. 1 = any severity (committed behavior).
		int expandSeverityFloor = 1;

		// --- combat envelope (attack range / war preparation) -----------------
		/// Base attack range in Chebyshev tiles: an offensive commit requires a
		/// known target within this distance of our nearest FINISHED inn (maxed
		/// with the nearest finished hospital when one exists) — a war party
		/// fights only as far from food/healing as the hunger clock allows. When
		/// every target is out of range Cortex builds a FORWARD inn/hospital
		/// toward the front instead of attacking and melting (scoreForwardBase);
		/// if no legal forward site exists the gate is waived. <= 0 disables the
		/// gate entirely (pre-v18 behavior).
		int attackRangeBase = 32;
		/// Range added per WALK level of the wave's SLOWEST warrior (racetrack
		/// training): faster marchers survive longer supply round-trips.
		int attackRangePerWalkLevel = 8;
		/// War-preparation level match: when 1, the normal offense commit counts
		/// only warriors whose ATTACK_STRENGTH level >= the highest enemy-warrior
		/// level ever observed (FOW-gated, latched), capped by what our barracks
		/// can currently train (finished barracks level + 1) so the gate creates
		/// training pressure instead of a deadlock. The famine blitz ignores it.
		/// 0 disables (raw-count commit, pre-v18 behavior).
		int warPrepLevelMatch = 1;
		/// Grace window (ticks) the attack-range gate may BIND — the army wants to
		/// attack and every known target is out of the support envelope — before the
		/// gate is WAIVED and the offense commits out-of-envelope anyway (pre-v18
		/// behavior) while the forward base keeps building. Prevents a "possible" but
		/// never-ordered forward base from holding the gate shut forever. 0 = never
		/// waive (the gate binds indefinitely while a forward base could cure it).
		int attackRangeGraceTicks = 2400;
		/// When 1, the attack-range gate is waived IMMEDIATELY (no grace wait) while
		/// AT MOST ONE enemy building is discovered: the gate first binds at first
		/// contact (flag targets ARE discovered buildings), and against a
		/// still-unscouted colony that lone out-of-envelope data point is no basis
		/// for holding the first strike through the grace window. Once the enemy
		/// base is mapped (>= 2 buildings discovered) the normal bind-then-grace
		/// behavior applies. 0 disables (always wait out the grace).
		int attackRangeUnscoutedWaiver = 1;

		// --- production-mix worker-target tiers (the expansion-tax side) ------
		/// Divisor D in mid = base + (needs - base)/D: how far past the hauler
		/// floor the worker base grows before production flips to pure warriors.
		/// Construction-site jobs inflate `needs`, so D also sets how hard an
		/// in-flight expansion delays the army ramp.
		int tierMidDiv = 2;
		/// Worker slice of the worker-dominant middle tier's production ratio.
		int workerRatioTier2 = CORTEX_MAX_RATIO / 2;
	};

	/// The process-wide tuning, loaded from GLOB2_CORTEX_TUNING on first use
	/// (defaults when unset). Never changes after that first call.
	const CortexTuning& cortexTuning();
}
