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
	void enqueueWheatForbidden(const Cortex::CortexObservation& obs);

	/// Find our team's single live WAR_FLAG virtual building, or NULL if none.
	/// Cortex keeps at most one war flag and does NOT persist its gid; it is
	/// re-found each decision cycle by scanning team->virtualBuildings (a list,
	/// iterated in deterministic insertion order — never a std::set).
	Building* findOwnWarFlag() const;

	/// Ensure our single war flag sits at map tile (tx, ty): create it there if
	/// we have none (respecting the flag cooldown and the virtual-building room
	/// check), or move the existing one if it is far from the target. radius/count
	/// come from the action (clamped). Appends at most one Order to orderQueue.
	void ensureWarFlagAt(int tx, int ty, const Cortex::CortexAction& action, const Cortex::CortexObservation& obs);

	/// Remove our war flag (OrderDelete) if one exists.
	void clearOwnWarFlag();

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

	/// Game tick before which ensureWarFlagAt refuses to create another war flag.
	/// Same BUILD_COOLDOWN_TICKS latency (an OrderCreate for a virtual flag also
	/// takes several ticks to register before findOwnWarFlag can see it), but a
	/// SEPARATE timer from buildCooldownUntil: a queued economy build must never
	/// stall a time-critical flag placement — above all a defensive recall when the
	/// base is under attack — by up to BUILD_COOLDOWN_TICKS. 0 = no flag create
	/// pending.
	int flagCooldownUntil;

	/// Offense-hold hysteresis state (RAM-only, persisted symmetrically). The
	/// posture the flag is currently committed to, and the tick until which an
	/// OFFENSE commitment is protected from a minor-harassment defensive recall.
	/// AICortex OWNS this state: it is MUTATED only here, as an execution side-effect
	/// when a flag is actually (re)placed (translateActionPlaceWarFlag re-arms the
	/// hold; the defense/clear helpers reset it). The hold-vs-recall DECISION lives in
	/// the policy (CortexPolicy::scoreDefense), which READS these values through the
	/// observation (obs.flagPosture / obs.offenseHoldUntil, echoed each cycle in
	/// getOrder() before decide()). flagPosture is one of FlagPosture; offenseHoldUntil
	/// == 0 means no hold is active.
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
};
