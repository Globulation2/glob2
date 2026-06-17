// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "AIImplementation.h"
#include "CortexTypes.h"
#include "CortexPolicy.h"

#include <cstdio>
#include <map>
#include <memory>
#include <queue>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;
class Building;

// AICortex — variant A of the parent-class spike (docs/AI/cortex/NEXT.md open
// question #1). Subclasses AIImplementation DIRECTLY, owning the full
// observation -> policy -> action pipeline with no Echo framework in between.
//
// The engine constraint is one Order per getOrder() call. The action layer
// therefore translates one CortexAction into a *sequence* of Orders pushed onto
// orderQueue; getOrder() pops one per tick and returns NullOrder when the queue
// is empty. The policy is consulted on a slow cadence (OBSERVE_INTERVAL ticks),
// not every tick — cheap now, and the right shape for paying NN inference cost
// only on decision cycles later.

class AICortex : public AIImplementation
{
public:
	explicit AICortex(Player* player);
	AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor);
	~AICortex();

	bool load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor);
	void save(GAGCore::OutputStream* stream);

	std::shared_ptr<Order> getOrder(void);

private:
	/// Ticks between policy invocations. The observation/policy run at this
	/// cadence; Order emission stays at tick rate via the queue. 25 ticks = 1
	/// second at the engine's 40 ms tick.
	static const int OBSERVE_INTERVAL = 25;

	/// Worker count forced onto the pre-placed starting swarm on the first decision
	/// cycle. The map gives the initial swarm an arbitrary maxUnitWorking; jump it
	/// straight to a productive hauler count so the early worker economy ramps at
	/// once instead of crawling up one hauler per cycle through the ±1 worker-tuning
	/// loop. One-shot (see swarmKickstarted); tuning takes over from this baseline.
	static const int SWARM_START_WORKERS = 4;

	/// Ticks to suppress a new build order OF THE SAME TYPE after issuing one, so
	/// the in-flight OrderCreate has time to execute and show up as a building site
	/// before the policy decides again (otherwise it re-issues duplicates the engine
	/// drops). Applied per-type via buildCooldownUntil[], so the cooldown bridges only
	/// the issue->visible-site window for that one type and never stalls placing a
	/// different building while units are free.
	static const int BUILD_COOLDOWN_TICKS = 250;

	/// Safety-net lifetime of the pendingUpgradeType guard (below). An issued
	/// upgrade is normally cleared the moment it becomes a visible construction
	/// site; this only bounds the rare case where the upgrade order is silently
	/// dropped or reverted (cancelConstruction) and never becomes a site, so the
	/// guard would otherwise block that type forever. Generously larger than the
	/// invisible issue->site window (a few hundred ticks at most).
	static const int UPGRADE_PENDING_TIMEOUT_TICKS = 2000;

	/// Offense-hold hysteresis (the thrash damper). Once we commit an offensive
	/// war flag we hold that posture for at least this many ticks; within the
	/// hold window a DEFENSE recall is IGNORED unless the base threat is "serious"
	/// (>= DEFENSE_SERIOUS_BUILDINGS buildings taking fire at once). This stops the
	/// single flag oscillating between the enemy base and home every decision cycle
	/// whenever a lone harasser pokes the colony — the measured failure mode where
	/// the army never advanced and melted short of the objective. Sized as several
	/// OBSERVE_INTERVAL cycles so the flag actually reaches and engages the enemy
	/// before any minor-harassment recall is even considered.
	///
	/// This constant is used ONLY here, to (re-)arm offenseHoldUntil when a war flag
	/// is placed — an execution side-effect the action layer owns. The hold-vs-recall
	/// DECISION that consults the resulting offenseHoldUntil now lives in the policy
	/// (CortexPolicy::scoreDefense), which reads it through the observation; the policy
	/// stays pure and never sees this constant.
	static const int OFFENSE_HOLD_TICKS = 600;
	/// Offense WAVE PIPELINE (the "keep the army moving as cohesive waves, no downtime"
	/// design). Cortex runs up to MAX_OFFENSE_FLAGS offense war flags at once. At any
	/// moment AT MOST ONE is MUSTERING — planted at the home rally point, at NORMAL
	/// priority so freshly-trained warriors flow into it and gather as a mass — while the
	/// others are MARCHING on the enemy at LOW priority (they keep their mustered cohort
	/// but never pull solo replacements: the engine never poaches a warrior already bound
	/// to a flag, so a marching wave stays intact and a new wave forms behind it). The
	/// moment a mustering wave is "near full" it marches and a fresh musterer is spawned,
	/// so pressure is continuous and never collapses to a single all-or-nothing push.
	/// One flag deploys at most CORTEX_MAX_FLAG_UNITS (engine cap); the pipeline is how
	/// Cortex fields a large army instead of leaving it idle behind a 20-unit cap.
	static const int MAX_OFFENSE_FLAGS = 3;
	/// A muster lasts at most this many ticks: once it elapses the wave marches with
	/// whatever has gathered, so a slow/scattered army never stalls the offense forever.
	static const int OFFENSE_MUSTER_TIMEOUT_TICKS = 500;
	/// A wave is "massed enough to march" once the warriors gathered at its rally flag
	/// reach this fraction (num/den) of the flag's requested summon count — "near full",
	/// not exactly full, since a unit is always dying or being born. Floored at 1.
	static const int OFFENSE_MUSTER_READY_NUM = 3;
	static const int OFFENSE_MUSTER_READY_DEN = 4;
	/// A MARCHING wave is retired (its slot freed for a fresh muster) once the warriors
	/// still present around its flag fall to or below this — the cohort is spent, so we
	/// recycle the slot rather than leave a near-empty flag dribbling at the enemy.
	static const int OFFENSE_WAVE_SPENT_WARRIORS = 2;
	/// How many of our buildings must be under attack AT ONCE for a defensive
	/// recall to override an in-progress offense hold. A single transient hit
	/// (1 building) is "harassment" and does not break the push; multiple
	/// buildings under fire is a real base assault that earns the recall.
	/// Single-sourced in CortexTypes.h (the hold-vs-recall decision now lives in
	/// the policy, which reads it from there); aliased here so the action layer's
	/// established name keeps working and the value can never drift.
	static const int DEFENSE_SERIOUS_BUILDINGS = Cortex::CORTEX_DEFENSE_SERIOUS_BUILDINGS;

	/// Flag posture the action layer last committed (RAM-only hysteresis state).
	/// Aliases Cortex::CortexFlagPosture (single source of truth) so the value
	/// stored in flagPosture / echoed into the observation is identical everywhere.
	enum FlagPosture {
		POSTURE_NONE    = Cortex::CORTEX_POSTURE_NONE,
		POSTURE_OFFENSE = Cortex::CORTEX_POSTURE_OFFENSE,
		POSTURE_DEFENSE = Cortex::CORTEX_POSTURE_DEFENSE
	};

	/// One offense wave in the pipeline. `gid` is its live WAR_FLAG (NOGBID == empty
	/// slot); `musterUntil` is the muster-timeout tick while the wave is MUSTERING at the
	/// rally (> 0), and 0 once it has MARCHED onto the enemy. `createCooldown` gates
	/// re-issuing this slot's OrderCreate until the flag registers (an OrderCreate takes
	/// several ticks to land). Iterated by array index — deterministic, never a set.
	struct OffenseWave {
		Uint16 gid;
		Sint32 musterUntil;
		Sint32 createCooldown;
	};

	void init(Player* player);

	/// Action layer (direct binding): translate an action intent into zero or
	/// more engine Orders, appended to orderQueue. NoOp queues nothing. The
	/// observation is passed alongside because an ACTION_BUILD only carries a
	/// candidate-slot index — the (x, y) lives in obs.buildCandidates. Dispatches
	/// to one per-action-kind helper below; each helper owns that kind's cooldown
	/// gates, state mutations, dedup checks, and order-push sequence.
	void translateAction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);

	// --- Per-action-kind translate helpers --------------------------------
	// One method per ACTION_* kind. Each appends zero or more Orders to
	// orderQueue and mutates only the state the original inline case mutated
	// (buildCooldownUntil[], pendingUpgradeType/Until, flagPosture,
	// offenseHoldUntil). The order in which orders are pushed is preserved
	// byte-for-byte from the pre-decomposition switch.
	void translateActionBuild(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionSetProduction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionPlaceWarFlag(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionPlaceDefenseFlag(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionClearFlags();
	void translateActionUpgradeBuilding(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionTuneWorkers(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);
	void translateActionSetPriority(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);

	/// Shared "decode GID → verify building → dedup → push OrderModifyBuilding"
	/// loop used by translateActionTuneWorkers for all three building sets
	/// (swarms, inns, construction sites). `tracked`/`count` is the observed
	/// building array (TrackedBuilding or TrackedSite — both expose valid, gid,
	/// maxUnitWorking); `desiredArr[i]` is the requested maxUnitWorking for
	/// tracked[i] (-1 == leave unchanged). `maxClamp >= 0` clamps the request to
	/// that ceiling before the dedup check (the sites path; pass -1 for the
	/// swarm/inn paths, which do not clamp). `accept` is the per-set guard that
	/// confirms the decoded Building is still the kind we observed (finished swarm,
	/// finished inn, or live construction site) before issuing the order. On each
	/// accepted change it mirrors the engine executor locally (b->maxUnitWorking =
	/// desired; b->update()) and pushes one OrderModifyBuilding — identical to the
	/// original three inline loops, in the same index order.
	template <typename Tracked, typename Accept>
	void applyWorkerCounts(const Tracked* tracked, int count, const Sint32* desiredArr,
	                       int maxClamp, Accept accept);

	/// Wheat-forbidden executor, run EVERY decision cycle in parallel with
	/// translateAction (gated by CortexPolicy::wantWheatProtection) — not as a
	/// competing action. Rebuilds the full ADD/DEL checkerboard tile masks over our
	/// wheat (a bounded colony-region scan, RNG-free) at the per-game open-margin
	/// wheatOpenMargin and appends one OrderAlterateForbidden per non-empty diff
	/// (DEL before ADD). A single order carries the whole diff, so all newly-revealed
	/// wheat is fenced in one cycle. Self-correcting: an already-painted diff is empty
	/// next cycle, so re-running each cycle is free when there is no new work.
	void enqueueWheatForbidden(const Cortex::CortexObservation& obs, bool liftAll = false);

	/// Resolve a tracked flag gid to its live ALIVE WAR_FLAG building, or NULL if the
	/// gid is unset (NOGBID) or the flag no longer exists (died / was deleted).
	/// Scans team->virtualBuildings (a list, deterministic insertion order — never a
	/// std::set).
	Building* findFlagByGid(Uint16 gid) const;

	/// True if `gid` is currently owned by ANY tracked flag (the defense flag or any
	/// offense wave). Used by rediscoverFlag so a newly-landed flag is never double-
	/// claimed by two slots.
	bool isOwnedGid(Uint16 gid) const;

	/// After an OrderCreate lands (it takes several ticks), claim the new WAR_FLAG into
	/// `gid`: the live WAR_FLAG nearest (tx, ty) whose gid is not already owned by
	/// another tracked flag. Returns the claimed building (and stores its gid in `gid`),
	/// or NULL if none has appeared yet. Position-matching disambiguates concurrently
	/// created flags (their targets are far apart).
	Building* rediscoverFlag(Uint16& gid, int tx, int ty);

	/// Ensure the flag tracked by `gid` sits at (tx, ty) with the given summon count,
	/// minLevel, and engine priority: create it if absent (respecting `cooldown` and the
	/// virtual-building room check), else move it when far and reconcile
	/// count/minLevel/priority. Operates on a gid/cooldown REFERENCE so any flag (defense
	/// or any offense wave) is managed independently. Appends Orders to orderQueue
	/// (mirroring each engine executor locally so the dedup won't re-fire).
	void ensureFlagAt(Uint16& gid, Sint32& cooldown, int tx, int ty, int radius, int count,
	                  int minLevel, int priority, const Cortex::CortexObservation& obs);

	/// Remove the flag tracked by `gid` (OrderDelete) if it exists, and reset `gid` to
	/// NOGBID. Deleting a flag releases its committed warriors back to the free pool.
	void clearOneFlag(Uint16& gid);

	/// Tear down every offense wave (delete each live flag, clear all slots). Used to
	/// recall the whole army (a serious defense) or to stand the offense down.
	void clearAllOffenseFlags();

	/// Drive the offense WAVE PIPELINE one decision cycle toward (targetX, targetY) (the
	/// enemy building to assault), each flag with stay-range `radius`: reconcile every
	/// live wave (muster->march, retire spent marchers) and, if a slot is free and spare
	/// warriors exist, (keep) mustering the next wave at the home rally. `warriors` is the
	/// colony's current warrior count (whether there is anything left to muster). See
	/// MAX_OFFENSE_FLAGS and the OffenseWave doc.
	void manageOffenseWaves(int targetX, int targetY, int radius, int warriors,
	                        const Cortex::CortexObservation& obs);

	/// Home RALLY point for the muster-then-march offense: the colony's heart (its
	/// first/primary swarm, falling back to the first alive building). A single valid
	/// colony tile — no centroid/map-wrap math — where each offense wave gathers before
	/// marching out as one. Returns false (no buildings) so the caller can skip mustering.
	bool computeRallyPoint(int& rx, int& ry) const;

	/// Tear down a stale DEFENSE flag once the assault is over (no building under
	/// attack). Runs every decision cycle in getOrder() — in parallel with the action
	/// ladder, NOT gated on winning it — because scoreDefense DECLINES when there is no
	/// threat, so the ladder would never pick a teardown action. Cheap and idempotent
	/// (a no-op once the flag is already gone).
	void reconcileStaleDefenseFlag(const Cortex::CortexObservation& obs);

	/// Find the single best finished instance of `buildingType` (an
	/// IntBuildingType shortTypeNum) to upgrade to its next level, or NULL if no
	/// instance currently passes the full engine Upgradable predicate. Scans
	/// team->myBuildings by ARRAY INDEX (never a std::set) and ranks eligible
	/// instances deterministically — improving on Nicowar's random pick. See the
	/// .cpp for the predicate and the bottleneck ranking.
	Building* findUpgradeTarget(int buildingType) const;

	Player* player;

	Cortex::CortexPolicy policy;
	int timer;

	/// Game tick before which translateAction refuses to issue another build
	/// (see BUILD_COOLDOWN_TICKS), indexed by building type. 0 = no build of that
	/// type pending. Per-type (not a single global gate) so issuing one building
	/// only suppresses re-issuing the SAME type while its OrderCreate is in flight
	/// — a different type can be placed on the very next decision cycle the moment
	/// spare labour exists, instead of waiting out a colony-wide build cooldown.
	/// The duplicate-build the cooldown prevents is inherently per-type (the policy
	/// only ever re-issues the same intent), so a per-type window loses no safety.
	int buildCooldownUntil[Cortex::CORTEX_BUILDING_TYPES];

	/// Guard against stacking two upgrades of the SAME building class while the
	/// first is still converting. An issued upgrade (OrderConstruction) does not
	/// appear in the observation as a site for a while — the building first evicts
	/// its trainees and waits for the larger footprint to clear
	/// (building/Construction.cpp:394-423) — so without this guard the policy,
	/// still seeing the pre-upgrade count finished and none upgrading, re-issues a
	/// SECOND upgrade and blacks out the whole class at once (measured: both
	/// barracks offline simultaneously). Holds the shortTypeNum of an upgrade we
	/// issued that has not yet become a visible site; -1 == none pending. Cleared
	/// the cycle the upgrade becomes visible (the policy's own
	/// cortexBuildingsUpgrading / finished-count gates take over then) or after
	/// UPGRADE_PENDING_TIMEOUT_TICKS. Serialized for lockstep determinism (it
	/// gates order emission, like buildCooldownUntil).
	int pendingUpgradeType;
	/// Safety-net expiry tick for pendingUpgradeType. 0 when nothing is pending.
	int pendingUpgradeUntil;

	/// Create-cooldown for the single DEFENSE flag: the tick before which ensureFlagAt
	/// refuses to re-issue its OrderCreate (a virtual flag takes several ticks to
	/// register before the new gid can be claimed). Each offense wave carries its OWN
	/// createCooldown in OffenseWave, so a pending offense create never stalls a
	/// time-critical defensive recall. 0 = no defense create pending.
	int defenseCooldown;

	/// The offense WAVE PIPELINE: up to MAX_OFFENSE_FLAGS concurrent offense flags, each
	/// MUSTERING at the rally then MARCHING on the enemy (see OffenseWave / the pipeline
	/// constants). Plus the single persistent DEFENSE flag's gid (NOGBID == none), which
	/// recalls the home reserve. Tracked by gid (not a bare WAR_FLAG scan) because
	/// several WAR_FLAGs coexist and must be told apart. Serialized for lockstep
	/// determinism alongside flagPosture.
	OffenseWave offenseWaves[MAX_OFFENSE_FLAGS];
	Uint16 defenseFlagGid;

	/// Offense-hold hysteresis state (RAM-only, persisted symmetrically). The posture
	/// the flags are currently committed to, and the tick until which an OFFENSE
	/// commitment is protected from a minor-harassment defensive recall. AICortex OWNS
	/// this state: it is MUTATED only here, as an execution side-effect when flags are
	/// (re)placed. The hold-vs-recall DECISION lives in the policy
	/// (CortexPolicy::scoreDefense), which READS these through the observation
	/// (obs.flagPosture / obs.offenseHoldUntil, echoed each cycle before decide()).
	/// flagPosture is one of FlagPosture; offenseHoldUntil == 0 means no hold is active.
	int flagPosture;
	int offenseHoldUntil;

	/// Per-game wheat open-margin N: the first N rows of wheat nearest the harvest
	/// source stay unpainted; the checkerboard starts at depth N+1. Drawn ONCE via
	/// syncRand on the first decision cycle (sentinel -1 = not yet drawn) and then
	/// persisted (NOT redrawn on load) so same-seed replays stay byte-identical.
	/// The draw consumes one syncRand() → it shifts the shared RNG stream, so this
	/// is replay-relevant (validated against the deterministic harness).
	Sint32 wheatOpenMargin;

	/// Per-inn post-build settle clock: maps an inn's Building::gid to the game tick
	/// at which Cortex FIRST observed it finished. The decision cycle stamps
	/// obs.trackedInns[i].ticksSinceFinished = now - firstSeen so the policy's
	/// worker-tuning loop can hold a fresh inn at its as-built worker count for
	/// CORTEX_INN_TUNE_DELAY_TICKS (it starts at an empty 0/10 buffer and must not be
	/// worker-spiked before its first haulers fill it). Keyed by gid (deterministic);
	/// never iterated to PRODUCE an order, only for keyed lookup/insert/prune, so
	/// std::map order does not affect lockstep. RAM-only (NOT serialized): it rebuilds
	/// identically from the same seed on a continuous run, and all clients reload a
	/// save together and re-stamp in lockstep, so a reload merely re-arms the settle
	/// window uniformly — no desync. (Cortex has no persisted saves to preserve yet.)
	std::map<Uint16, Sint32> innFinishedTick;

	/// One-shot guard for the start-of-game swarm worker kickstart
	/// (SWARM_START_WORKERS): set once the starting swarm has been jumped to its
	/// baseline hauler count. RAM-only — reloading a (hypothetical) save merely
	/// re-kickstarts uniformly on every client, and Cortex has no persisted saves.
	bool swarmKickstarted;

	/// Orders awaiting emission, one popped per getOrder() call.
	std::queue<std::shared_ptr<Order> > orderQueue;

	/// DIAGNOSTIC ONLY (not serialized, never read by the policy, emits no Order).
	/// One-shot guard so the under-attack state dump fires only the FIRST decision
	/// cycle on which the colony is taking fire. Gated behind the CORTEX_DUMP_ATTACK
	/// env var; pure read of the observation + ground-truth Game state to stderr, so
	/// it cannot perturb the sync stream. RAM-only like orderQueue.
	bool attackDumped;
	/// Print the under-attack characterization (scouting / economy / timing / enemy)
	/// to stderr. Diagnostic; does not touch RNG, orders, or any persisted state.
	void dumpAttackState(const Cortex::CortexObservation& obs) const;

	/// TRAINING TRACE (gated, not serialized, never read by the policy, emits no
	/// Order). When GLOB2_CORTEX_TRACE=<prefix> is set, every decision cycle appends
	/// one CSV row per valid tracked swarm to <prefix>.team<N>.csv — the (state,
	/// hand-action) pairs the ML pilot trains on (see docs/AI/cortex/PILOT.md). Pure
	/// read of the observation + the already-computed worker-tune action; opening and
	/// writing a file touches no RNG/order/sync state, so the lockstep stream is
	/// unaffected. Lazily opened on first use, closed in the destructor; RAM-only
	/// handle. `tune` is the action returned by CortexPolicy::tuneWorkers this cycle.
	/// GLOB2_CORTEX_TRACE MUST be an ABSOLUTE path: glob2 chdir()s to its resource
	/// directory at startup (Glob2.cpp), so a relative path would resolve there, not in
	/// the launch directory. The open is attempted once (traceOpenAttempted); on failure
	/// it warns to stderr and disables the trace instead of retrying every cycle.
	std::FILE* traceFile;
	bool traceOpenAttempted;
	void dumpWorkerTrace(const Cortex::CortexObservation& obs,
	                     const Cortex::CortexAction& tune);

	/// DECISION-SELECTION TRACE for the decide() ML pilot (docs/AI/cortex/
	/// DECIDE_CONTRACT.md). When GLOB2_CORTEX_DECIDE_TRACE=<abs prefix> is set,
	/// every decision cycle appends ONE CSV row per AI instance to
	/// <prefix>.team<N>.csv: tick, team, the 48 decision features (in
	/// DECIDE_CONTRACT idx order, via CortexPolicy::extractDecideFeatures), the
	/// per-cycle eligibility bitmask, and the chosen class index. The training
	/// (state, eligible-mask, chosen) tuples for the utility-score net. Pure read
	/// of the observation + the DecideTrace decide() already produced; opening and
	/// writing a file touches no RNG/order/sync state, so the lockstep stream is
	/// unaffected. SEPARATE file handle + open-attempt guard from the worker trace
	/// (they are distinct CSVs with distinct schemas). GLOB2_CORTEX_DECIDE_TRACE
	/// MUST be an ABSOLUTE path (glob2 chdir()s at startup); on open failure it
	/// warns once and disables rather than retrying every cycle.
	std::FILE* decideTraceFile;
	bool decideTraceOpenAttempted;
	void dumpDecideTrace(const Cortex::CortexObservation& obs,
	                     const Cortex::DecideTrace& trace);
};
