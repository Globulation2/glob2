// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AICortex.h"
#include "CortexObservation.h"
#include "CortexWheat.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "Settings.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
#include "unit/UnitConsts.h"
#include "Game.h"
#include "map/Map.h"
#include "Brush.h"
#include "Utilities.h"
#include "TeamStat.h"
#include <Stream.h>
#include <iostream>
#include <cstdlib>

using std::shared_ptr;

// Map distance (warp-safe) beyond which an existing war flag is recalled to a
// new target rather than left in place. Small slack so a flag already sitting on
// (or right next to) the target isn't pointlessly re-ordered every cycle.
static const int FLAG_MOVE_THRESHOLD = 3;

AICortex::AICortex(Player* player)
{
	init(player);
}

AICortex::AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	init(player);
	load(stream, player, versionMinor);
}

AICortex::~AICortex()
{
}

void AICortex::init(Player* player)
{
	this->player = player;
	timer = 0;
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
		buildCooldownUntil[t] = 0; // per-type build cooldown; none pending at start.
	pendingUpgradeType = -1; // no upgrade in flight.
	pendingUpgradeUntil = 0;
	flagCooldownUntil = 0;
	flagPosture = POSTURE_NONE;
	offenseHoldUntil = 0;
	wheatOpenMargin = -1; // sentinel: drawn lazily on the first decision cycle.
	attackDumped = false; // diagnostic one-shot; never serialized.
	innFinishedTick.clear(); // RAM-only inn settle clock; rebuilt as inns are seen.
	swarmKickstarted = false; // start-of-game swarm worker kickstart not yet done.
}

bool AICortex::load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	this->player = player;
	stream->readEnterSection("AICortex");
	timer = stream->readUint32("timer");
	stream->readEnterSection("buildCooldownUntil");
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
	{
		stream->readEnterSection(t);
		buildCooldownUntil[t] = stream->readSint32("buildCooldownUntil");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	pendingUpgradeType = stream->readSint32("pendingUpgradeType");
	pendingUpgradeUntil = stream->readSint32("pendingUpgradeUntil");
	flagCooldownUntil = stream->readSint32("flagCooldownUntil");
	flagPosture = stream->readSint32("flagPosture");
	offenseHoldUntil = stream->readSint32("offenseHoldUntil");
	// Persisted, NOT redrawn on load: re-drawing would consume a fresh syncRand on
	// every load and desync replays. -1 means a pre-wheat save (or a game that has
	// not reached its first decision cycle yet) — getOrder draws it next cycle.
	wheatOpenMargin = stream->readSint32("wheatOpenMargin");
	stream->readLeaveSection();
	// orderQueue is transient working state, not persisted; it refills on the
	// next decision cycle after load.
	return true;
}

void AICortex::save(GAGCore::OutputStream* stream)
{
	stream->writeEnterSection("AICortex");
	stream->writeUint32(timer, "timer");
	stream->writeEnterSection("buildCooldownUntil");
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
	{
		stream->writeEnterSection(t);
		stream->writeSint32(buildCooldownUntil[t], "buildCooldownUntil");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeSint32(pendingUpgradeType, "pendingUpgradeType");
	stream->writeSint32(pendingUpgradeUntil, "pendingUpgradeUntil");
	stream->writeSint32(flagCooldownUntil, "flagCooldownUntil");
	stream->writeSint32(flagPosture, "flagPosture");
	stream->writeSint32(offenseHoldUntil, "offenseHoldUntil");
	stream->writeSint32(wheatOpenMargin, "wheatOpenMargin");
	stream->writeLeaveSection();
}

Building* AICortex::findOwnWarFlag() const
{
	// War flags are VIRTUAL buildings: they live in team->virtualBuildings, not
	// team->myBuildings (which holds only real, map-footprint buildings). The
	// container is a std::list — iterate it in insertion order (deterministic);
	// never a std::set. Cortex keeps at most one war flag, so return the first.
	Team* team = player->team;
	for (Building* b : team->virtualBuildings)
	{
		if (b
		    && b->type->shortTypeNum == IntBuildingType::WAR_FLAG
		    && b->buildingState == Building::ALIVE)
			return b;
	}
	return NULL;
}

void AICortex::ensureWarFlagAt(int tx, int ty, const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Clamp the discrete action params to Cortex's own bounds (the engine clamps
	// nothing on its own). radius == flag unitStayRange, count == warriors summoned.
	int radius = action.flagRadius;
	if (radius < 1)
		radius = 1;
	else if (radius > Cortex::CORTEX_MAX_FLAG_RADIUS)
		radius = Cortex::CORTEX_MAX_FLAG_RADIUS;
	int count = action.unitCount;
	if (count < 0)
		count = 0;
	else if (count > Cortex::CORTEX_MAX_FLAG_UNITS)
		count = Cortex::CORTEX_MAX_FLAG_UNITS;

	Game* game = player->team->game;

	Building* existing = findOwnWarFlag();
	if (existing == NULL)
	{
		// No flag yet: create one. An OrderCreate takes several ticks to execute
		// and register the virtual building; without a cooldown the policy re-issues
		// the create before the flag appears. This is the flag's OWN cooldown, NOT
		// the build cooldown — a queued economy build must never delay a flag (and
		// especially not a defensive recall) by up to BUILD_COOLDOWN_TICKS.
		if (obs.tick < flagCooldownUntil)
			return;

		// Resolve WAR_FLAG the VIRTUAL way: flags have no building-site variant, so
		// pass isBuildingSite==false. (The economy path passes true, which returns
		// -1 for flags — that is exactly why the build path skips them.)
		const std::string& name = IntBuildingType::reverseConversionMap[IntBuildingType::WAR_FLAG];
		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
		if (typeNum < 0)
			return;

		// Virtual-building room gate: for a flag this just checks no other own-flag
		// occupies the tile and ignores fog (checkFow defaulted true is harmless here).
		BuildingType* bt = globalContainer->buildingsTypes.get(typeNum);
		if (!game->checkRoomForBuilding(tx, ty, bt, player->team->teamNumber))
			return;

		// 7-arg OrderCreate: the trailing flagRadius sets the flag's unitStayRange at
		// execution (Game_orders.cpp executeCreate). For a flag, unitWorking ==
		// unitWorkingFuture == warriors summoned, so both are `count`.
		orderQueue.push(shared_ptr<Order>(new OrderCreate(
			player->team->teamNumber, tx, ty, typeNum,
			count, count, radius)));
		flagCooldownUntil = obs.tick + BUILD_COOLDOWN_TICKS;
		return;
	}

	// Flag already exists: move it only if it is far from the target; otherwise
	// leave it where it is. We intentionally do NOT re-issue radius/count changes
	// here for v1 — keeping the per-cycle work minimal (retargeting radius/count
	// for an in-place flag is deferred).
	int dist = game->map.warpDistMax(existing->posX, existing->posY, tx, ty);
	if (dist > FLAG_MOVE_THRESHOLD)
		orderQueue.push(shared_ptr<Order>(new OrderMoveFlag(existing->gid, tx, ty, false)));
}

void AICortex::clearOwnWarFlag()
{
	Building* existing = findOwnWarFlag();
	if (existing)
		orderQueue.push(shared_ptr<Order>(new OrderDelete(existing->gid)));
}

Building* AICortex::findUpgradeTarget(int buildingType) const
{
	// Scan our real buildings by ARRAY INDEX (myBuildings, never team->upgrade
	// or any std::set) so the selection is lockstep-deterministic. We keep the
	// single best instance whose b->type->shortTypeNum == buildingType and that
	// passes the FULL engine Upgradable predicate — the same seven conditions the
	// observation's upgradableCount uses (CortexTypes.h:182-189), which are in
	// turn exactly what Building::launchConstruction's UPGRADE branch and the GUI
	// upgrade gate require:
	//   - buildingState == ALIVE                          (C++: Construction.cpp:95)
	//   - !type->isBuildingSite                           (C++: Construction.cpp:95)
	//   - hp == type->hpMax  (else launchConstruction REPAIRS, not upgrades)
	//                                                      (C++: Construction.cpp:97-108)
	//   - constructionResultState == NO_CONSTRUCTION (not already up/repairing)
	//   - type->nextLevel != BUILDING_LEVEL_NONE (not already at max level)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:424)
	//   - team->maxBuildLevel() > type->level             (C++: GameGUIInput.cpp:426)
	//   - isHardSpaceForBuildingSite(UPGRADE) (larger next-level footprint fits)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:425)
	// If ANY condition fails the OrderConstruction would be silently dropped, so
	// only a fully-eligible instance is worth targeting.
	Team* team = player->team;
	const int maxBuildLevel = team->maxBuildLevel(); // C++: team/TeamRouting.cpp:245-259

	Building* best = NULL;
	int bestLevel = 0;
	std::size_t bestDemand = 0;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = team->myBuildings[i];
		if (b == NULL)
			continue;
		if (b->type->shortTypeNum != buildingType)
			continue;
		// C++: Building::launchConstruction, building/Construction.cpp:93-108.
		if (b->buildingState != Building::ALIVE)
			continue;
		if (b->type->isBuildingSite)
			continue;
		if (b->type->nextLevel == BUILDING_LEVEL_NONE)
			continue;
		if (b->hp != b->type->hpMax)
			continue; // hp < hpMax would launch a REPAIR; > can't happen.
		if (b->constructionResultState != Building::NO_CONSTRUCTION)
			continue;
		if (maxBuildLevel <= b->type->level) // C++: GameGUIInput.cpp:426 (> level)
			continue;
		if (!b->isHardSpaceForBuildingSite(Building::UPGRADE)) // C++: building/Building.h:200
			continue;

		// Bottleneck ranking (deterministic — we deliberately do NOT mimic
		// Nicowar's `syncRand() % buildings.size()` random pick from
		// ai/nicowar/Upgrade.cpp:141). Pick, in order:
		//   (1) LOWEST type->level — lift the most-behind building first, so the
		//       colony's weakest variant catches up before already-strong ones.
		//   (2) tie -> HIGHEST demand == unitsInside.size(): a building at capacity
		//       has units queued inside it (training/healing/feeding), so it is the
		//       real production bottleneck whose upgrade pays off most. unitsInside
		//       is a std::list, so we read .size().
		//   (3) tie -> first scan order (lowest array index, ~lowest gid) as the
		//       final fully-deterministic tie-break. No rand()/syncRand() is needed
		//       since (1)+(2)+(3) totally order the candidates; if a future tie-break
		//       beyond index were ever wanted it must use syncRand(), never rand().
		const std::size_t demand = b->unitsInside.size();
		bool better;
		if (best == NULL)
			better = true;
		else if (b->type->level != bestLevel)
			better = (b->type->level < bestLevel);
		else if (demand != bestDemand)
			better = (demand > bestDemand);
		else
			better = false; // equal rank -> keep the earlier (lower-index) instance.
		if (better)
		{
			best = b;
			bestLevel = b->type->level;
			bestDemand = demand;
		}
	}
	return best;
}

void AICortex::translateAction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	switch (action.kind)
	{
		case Cortex::ACTION_NOOP:
			// Nothing to enqueue.
			break;

		case Cortex::ACTION_BUILD:
		{
			const int type = action.buildingType;
			const int slot = action.locationSlot;
			if (type < 0 || type >= Cortex::CORTEX_BUILDING_TYPES)
				break;
			if (slot < 0 || slot >= Cortex::CORTEX_BUILD_CANDIDATES)
				break;

			// An issued OrderCreate takes several ticks to execute and register
			// as a building site, which is longer than one decision cycle. Without
			// a cooldown the policy re-issues the same build on the next cycle
			// (the site isn't visible yet), stacking duplicate orders the engine
			// then rejects. Suppress new builds OF THIS TYPE until the in-flight one
			// can land; a different type is free to be placed this cycle.
			if (obs.tick < buildCooldownUntil[type])
				break;

			const Cortex::BuildCandidate& cand = obs.buildCandidates[type][slot];
			if (!cand.valid)
				break; // policy chose a stale/empty slot; drop rather than misbuild.

			// Resolve the long building-site type id for a fresh (level 0)
			// building, exactly as the GUI/Echo build path does.
			const std::string& name = IntBuildingType::reverseConversionMap[type];
			Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, true);
			if (typeNum < 0)
				break; // no buildable site type (e.g. a virtual/flag type) — skip.

			// Worker counts from the engine's canonical defaults: column 0 is the
			// construction-site assignment, column 1 the finished-building one.
			const int unitWorking       = globalContainer->settings.defaultUnitsAssigned[type][0];
			const int unitWorkingFuture = globalContainer->settings.defaultUnitsAssigned[type][1];

			orderQueue.push(shared_ptr<Order>(new OrderCreate(
				player->team->teamNumber, cand.x, cand.y, typeNum,
				unitWorking, unitWorkingFuture)));
			buildCooldownUntil[type] = obs.tick + BUILD_COOLDOWN_TICKS;
			break;
		}

		case Cortex::ACTION_SET_PRODUCTION:
		{
			// Clamp the target defensively to [0, CORTEX_MAX_RATIO]; the engine
			// writes ratios verbatim, so it's the action layer's job to bound them.
			Sint32 target[NB_UNIT_TYPE];
			for (int t = 0; t < NB_UNIT_TYPE; t++)
			{
				int r = action.productionRatio[t];
				target[t] = (r < 0) ? 0 : (r > Cortex::CORTEX_MAX_RATIO ? Cortex::CORTEX_MAX_RATIO : r);
			}

			// Iterate buildings by array index (never std::set) for lockstep
			// determinism. Enqueue one OrderModifySwarm per finished swarm whose
			// current ratio differs from the target; getOrder() drains orderQueue
			// one order per tick, so this naturally retargets one swarm per tick.
			Team* team = player->team;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (!b)
					continue;
				if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
					continue;
				if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
					continue; // finished swarm only — not a site, a swarm under upgrade, or dead.
				                  // (mirrors executeModifySwarm's unitProductionTime guard.)

				// Dedup: only emit when this swarm's current ratio differs from
				// the target, so a steady-state policy that re-issues the same
				// ACTION_SET_PRODUCTION every cycle doesn't spam redundant orders.
				bool differs = false;
				for (int t = 0; t < NB_UNIT_TYPE; t++)
					if (b->ratio[t] != target[t])
					{
						differs = true;
						break;
					}
				if (!differs)
					continue;

				orderQueue.push(shared_ptr<Order>(new OrderModifySwarm(b->gid, target)));
			}
			break;
		}

		case Cortex::ACTION_PLACE_WAR_FLAG:
		{
			// Offense: ensure our single war flag sits on the chosen enemy-building
			// target. An out-of-range or invalid slot means "no target" — clear.
			const int slot = action.locationSlot;
			if (slot < 0 || slot >= Cortex::CORTEX_FLAG_TARGETS
			    || !obs.flagTargets[slot].valid)
			{
				clearOwnWarFlag();
				flagPosture = POSTURE_NONE;
				offenseHoldUntil = 0;
				break;
			}
			// Commit (or re-commit) the offense push and (re)arm the hold window so
			// the flag is protected from a minor-harassment defensive recall while it
			// advances on and engages the enemy. Re-arming each offense cycle keeps
			// the push alive as long as the policy keeps choosing offense.
			flagPosture = POSTURE_OFFENSE;
			offenseHoldUntil = obs.tick + OFFENSE_HOLD_TICKS;
			const Cortex::BuildCandidate& target = obs.flagTargets[slot];
			ensureWarFlagAt(target.x, target.y, action, obs);
			break;
		}

		case Cortex::ACTION_PLACE_DEFENSE_FLAG:
		{
			// Defense: ensure our single war flag sits on the under-fire spot. No
			// valid defense target means nothing is under attack — clear.
			if (!obs.defenseTarget.valid)
			{
				clearOwnWarFlag();
				flagPosture = POSTURE_NONE;
				offenseHoldUntil = 0;
				break;
			}

			// THRASH HYSTERESIS: if we are mid-offense-push (POSTURE_OFFENSE and still
			// inside the hold window) and the base threat is merely harassment — fewer
			// than DEFENSE_SERIOUS_BUILDINGS of our buildings taking fire at once — do
			// NOT recall. Leaving the offense flag where it is (a war flag is a standing
			// building, so it keeps summoning) lets the army actually reach and break
			// the enemy line instead of oscillating home every decision cycle. A real
			// base assault (>= DEFENSE_SERIOUS_BUILDINGS under fire) still earns the
			// recall and ends the offense hold.
			const bool seriousThreat = (obs.buildingsUnderAttack >= DEFENSE_SERIOUS_BUILDINGS);
			if (flagPosture == POSTURE_OFFENSE
			 && obs.tick < offenseHoldUntil
			 && !seriousThreat)
				break; // hold the offense; ignore the minor-harassment recall.

			flagPosture = POSTURE_DEFENSE;
			offenseHoldUntil = 0;
			ensureWarFlagAt(obs.defenseTarget.x, obs.defenseTarget.y, action, obs);
			break;
		}

		case Cortex::ACTION_CLEAR_FLAGS:
			// No offense/defense wanted right now — remove our war flag if any.
			clearOwnWarFlag();
			flagPosture = POSTURE_NONE;
			offenseHoldUntil = 0;
			break;

		case Cortex::ACTION_UPGRADE_BUILDING:
		{
			// Upgrade ONE finished instance of buildingType to its next level via
			// the engine upgrade order (OrderConstruction, Order.h:148-169 / body
			// OrderBuilding.cpp:138-144 — it carries the building's gid plus the two
			// worker counts; the engine derives the target level from type->nextLevel
			// in Building::launchConstruction, building/Construction.cpp:93-148).
			const int type = action.buildingType;
			if (type < 0 || type >= Cortex::CORTEX_BUILDING_TYPES)
				break;

			// One upgrade of a given class in flight at a time. A prior upgrade of
			// this type is still converting (issued but not yet a visible site), so
			// the observation still shows it finished — issuing a second now would
			// black out the whole class (e.g. both barracks offline at once). The
			// guard is released (in getOrder) the cycle the site becomes visible.
			if (pendingUpgradeType == type)
				break;

			// Same cooldown discipline as ACTION_BUILD: an OrderConstruction takes
			// ticks to convert the building into a site, and the observation's
			// upgradableCount won't drop until then. Without the cooldown the policy
			// would re-issue the upgrade on the next decision cycle.
			if (obs.tick < buildCooldownUntil[type])
				break;

			Building* b = findUpgradeTarget(type);
			if (!b)
				break; // no instance currently passes the full Upgradable predicate.

			// Worker counts from the engine's canonical defaults, mirroring the GUI
			// upgrade path (gui/GameGUIInput.cpp:409-427). There typeNum =
			// building->typeNum + 1 (the level-(L+1) construction SITE variant) and
			// unitWorking = getDefaultAssignedUnits(typeNum) /
			// unitWorkingFuture = getDefaultAssignedUnits(typeNum + 1) (the level-(L+1)
			// FINISHED variant). Per GameGUIDefaultAssignManager.cpp:14-29, a
			// (shortType, level, site) variant maps to defaultUnitsAssigned column
			// level*2 and a finished variant to column level*2 + 1
			// (Settings.h:73, defaultUnitsAssigned[NB_BUILDING][6]). With
			// targetLevel = type->level + 1 the GUI's two lookups are therefore
			// exactly columns [targetLevel*2] (site assign) and [targetLevel*2 + 1]
			// (finished assign) — equivalent, so we read them directly.
			const int targetLevel = b->type->level + 1;
			const int siteCol     = targetLevel * 2;
			const int finishedCol = targetLevel * 2 + 1;
			// Guard the column indices defensively (defaultUnitsAssigned has 6 cols,
			// levels 0..2; an upgradable building always has nextLevel so targetLevel
			// is 1 or 2 and both columns are in range, but clamp rather than trust it).
			if (siteCol < 0 || finishedCol < 0
			 || siteCol >= 6 || finishedCol >= 6)
				break;
			const int unitWorking       = globalContainer->settings.defaultUnitsAssigned[type][siteCol];
			// NOTE: for some types (e.g. ATTACK_BUILDING) the finished-building
			// columns (1,3,5) are 0 in Settings::resetDefaultUnitsAssigned — that is
			// the engine default, NOT a missing value. The GUI upgrade path passes the
			// same 0 here; launchConstruction uses the site column (unitWorking) as the
			// meaningful crew. Do not "fix" this to a nonzero default.
			const int unitWorkingFuture = globalContainer->settings.defaultUnitsAssigned[type][finishedCol];

			orderQueue.push(shared_ptr<Order>(new OrderConstruction(b->gid, unitWorking, unitWorkingFuture)));
			buildCooldownUntil[type] = obs.tick + BUILD_COOLDOWN_TICKS;
			// Mark this class's upgrade in flight until it shows up as a site (or
			// the safety timeout), so the policy can't stack a second one meanwhile.
			pendingUpgradeType = type;
			pendingUpgradeUntil = obs.tick + UPGRADE_PENDING_TIMEOUT_TICKS;
			break;
		}

		case Cortex::ACTION_TUNE_WORKERS:
		{
			// Apply the per-building desired worker counts (maxUnitWorking) to our
			// tracked swarms and inns. The policy nudges each building +/-1 per cycle
			// inside a deadband — these are small, frequent adjustments. We dedup
			// against the building's current maxUnitWorking so a steady-state policy
			// that re-decides the same target each cycle doesn't flood the order queue.
			// Mirror AICastor's pattern (Control.cpp:229-271): set b->maxUnitWorking
			// locally AND emit the order so the AI's own view updates immediately and
			// the dedup won't re-fire next cycle. This is deterministic — every client
			// runs the identical AI and queues the identical order in the same tick.
			Team* team = player->team;

			// --- swarms ---
			for (int i = 0; i < obs.swarmCount; i++)
			{
				const Cortex::TrackedBuilding& tb = obs.trackedSwarms[i];
				const int desired = action.swarmWorkers[i];

				// Skip: invalid slot, "leave unchanged" sentinel, or already at target.
				if (!tb.valid)
					continue;
				if (desired < 0)
					continue;
				if (desired == tb.maxUnitWorking)
					continue; // DEDUP: current state already matches; don't re-emit.

				// Resolve gid → Building* via the canonical decode
				// (Building::GIDtoID, inherited from BuildingUtils). This is an O(1)
				// array-index lookup — not a linear scan — so it is both deterministic
				// and cheap. GIDtoID returns the per-team array slot; GIDtoTeam is not
				// needed here because trackedSwarms only contains our own buildings
				// (filled from team->myBuildings in CortexTypes.h observe()).
				const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
				Building* b = team->myBuildings[bid];
				if (!b)
					continue;
				// Verify the building is still the finished swarm we observed — it
				// could have been destroyed, replaced, or started an upgrade between
				// the observation and now. Only a finished, alive swarm accepts
				// OrderModifyBuilding (mirrors executeModifyBuilding's guard).
				if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
					continue;
				if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
					continue;

				// Update the AI's local view immediately (AICastor pattern) so the
				// dedup won't re-trigger on the next cycle before the order executes.
				b->maxUnitWorking = desired;
				b->update();
				orderQueue.push(shared_ptr<Order>(new OrderModifyBuilding(b->gid, desired)));
			}

			// --- inns (FOOD_BUILDING) ---
			for (int i = 0; i < obs.innCount; i++)
			{
				const Cortex::TrackedBuilding& tb = obs.trackedInns[i];
				const int desired = action.innWorkers[i];

				if (!tb.valid)
					continue;
				if (desired < 0)
					continue;
				if (desired == tb.maxUnitWorking)
					continue; // DEDUP.

				const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
				Building* b = team->myBuildings[bid];
				if (!b)
					continue;
				if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
					continue;
				if (b->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
					continue;

				b->maxUnitWorking = desired;
				b->update();
				orderQueue.push(shared_ptr<Order>(new OrderModifyBuilding(b->gid, desired)));
			}

			// --- construction sites (pour idle workers into in-progress builds) ---
			for (int i = 0; i < obs.siteCount; i++)
			{
				const Cortex::TrackedSite& ts = obs.trackedSites[i];
				int desired = action.siteWorkers[i];

				if (!ts.valid)
					continue;
				if (desired < 0)
					continue;
				// Engine ceiling: executeModifyBuilding asserts the request is
				// <= MAX_BUILDING_WORKER_REQUEST. The policy already clamps, but guard
				// here too so no tune value can ever abort the game.
				if (desired > Cortex::CORTEX_MAX_BUILDING_WORKERS)
					desired = Cortex::CORTEX_MAX_BUILDING_WORKERS;
				if (desired == ts.maxUnitWorking)
					continue; // DEDUP.

				const int bid = Building::GIDtoID(static_cast<Uint16>(ts.gid));
				Building* b = team->myBuildings[bid];
				if (!b)
					continue;
				// Must still be a live construction site (it may have finished, been
				// destroyed, or otherwise changed between observation and now). A
				// finished building is no longer isBuildingSite, so this naturally
				// stops us writing a site cap onto a just-completed building.
				if (b->buildingState != Building::ALIVE || !b->type->isBuildingSite)
					continue;

				b->maxUnitWorking = desired;
				b->update();
				orderQueue.push(shared_ptr<Order>(new OrderModifyBuilding(b->gid, desired)));
			}
			break;
		}

		case Cortex::ACTION_SET_PRIORITY:
		{
			// Set tracked-swarm engine priority: the FIRST/primary swarm to
			// priorityTarget, every other swarm to priorityRest (-1/0/+1 each). Steady
			// state keeps the primary swarm HIGH so it wins worker contention; the
			// panic defense raises every swarm to HIGH (target == rest). Dedup against
			// each swarm's current Building::priority, and mirror the engine executor
			// (Game_orders.cpp:476-484 executeChangePriority sets b->priority then
			// b->updateCallLists()) locally so the AI's view updates immediately and
			// the dedup won't re-fire before the order executes. Deterministic:
			// identical AI + identical queued order on every client.
			Team* team = player->team;
			bool seenFirstSwarm = false;
			for (int i = 0; i < obs.swarmCount; i++)
			{
				const Cortex::TrackedBuilding& tb = obs.trackedSwarms[i];
				if (!tb.valid)
					continue;
				// First valid tracked swarm gets priorityTarget; the rest priorityRest.
				const int target = seenFirstSwarm ? action.priorityRest
				                                  : action.priorityTarget;
				seenFirstSwarm = true;
				if (target < Cortex::CORTEX_PRIORITY_LOW || target > Cortex::CORTEX_PRIORITY_HIGH)
					continue; // not a valid priority (e.g. CORTEX_PRIORITY_NONE).
				if (tb.priority == target)
					continue; // DEDUP: already at the target priority.

				const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
				Building* b = team->myBuildings[bid];
				if (!b)
					continue;
				if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
					continue;
				if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
					continue;

				b->priority = target;
				b->updateCallLists();
				orderQueue.push(shared_ptr<Order>(new OrderChangePriority(b->gid, target)));
			}
			break;
		}

		default:
			// Unknown intent: ignore rather than emit a bogus Order.
			break;
	}
}

void AICortex::enqueueWheatForbidden(const Cortex::CortexObservation& obs)
{
	(void)obs; // reserved: the gate already ran in CortexPolicy::wantWheatProtection.

	// Rebuild the ADD/DEL checkerboard masks for our wheat (the bounded colony-region
	// scan, RNG-free) at the per-game open-margin and emit one OrderAlterateForbidden
	// per non-empty diff. No build cooldown: these are area-paint orders, not
	// OrderCreates, and the reconcile is self-correcting — a paint already in place
	// yields an empty diff next cycle, so re-running it every cycle is free. The
	// margin is the AICortex member (the seeded per-game N), == obs.wheatOpenMargin.
	Cortex::WheatReconcile wr =
		Cortex::reconcileWheatForbidden(player, wheatOpenMargin, /*buildMasks=*/true);
	const Map* map = &player->team->game->map;
	const Uint8 teamNumber = static_cast<Uint8>(player->team->teamNumber);
	// DEL first so freeing dead tiles never races the ADD of fresh ones.
	if (wr.del.getApplicationCount() > 0)
		orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
			teamNumber, BrushTool::MODE_DEL, &wr.del, map)));
	if (wr.add.getApplicationCount() > 0)
		orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
			teamNumber, BrushTool::MODE_ADD, &wr.add, map)));
}

void AICortex::dumpAttackState(const Cortex::CortexObservation& obs) const
{
	using namespace Cortex;
	using std::cerr;
	Team* team = player->team;
	Game* game = team->game;
	const int me = team->teamNumber;

	cerr << "CORTEX_DUMP ==== first-under-attack snapshot ====\n";
	cerr << "CORTEX_DUMP team=" << me << " tick=" << obs.tick
	     << " (~" << (obs.tick / 25) << "s)"
	     << " buildingsUnderAttack=" << obs.buildingsUnderAttack
	     << " unitsUnderAttack=" << obs.unitsUnderAttack << "\n";

	// --- economy ---
	cerr << "CORTEX_DUMP ECON totalUnit=" << obs.totalUnit
	     << " workers=" << obs.workers
	     << " explorers=" << obs.explorers
	     << " warriors=" << obs.warriors
	     << " freeWorkers=" << obs.freeWorkers
	     << " feedCapacity=" << obs.feedCapacity
	     << " needFoodCrit=" << obs.needFoodCritical
	     << " starving=" << obs.starvingUnits
	     << " needHeal=" << obs.needHeal << "\n";

	// --- buildings (finished / sites) ---
	cerr << "CORTEX_DUMP BUILD totalBuilding=" << obs.totalBuilding
	     << " swarms=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_SWARM) << "site)"
	     << " inns=" << cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_FOOD) << "site)"
	     << " hospital=" << cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_HEAL) << "site)"
	     << " barracks=" << cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_ATTACK) << "site)"
	     << " school=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE) << "site)\n";

	// --- production / combat readiness ---
	cerr << "CORTEX_DUMP PROD swarmCount=" << obs.swarmCount
	     << " innCount=" << obs.innCount
	     << " swarmsProducing=" << obs.swarmsProducing
	     << " producingWarrior=" << obs.swarmsProducingWarrior
	     << " producingExplorer=" << obs.swarmsProducingExplorer
	     << " producingWorker=" << obs.swarmsProducingWorker
	     << " maxBuildLevel=" << obs.maxBuildLevel
	     << " warFlagsActive=" << obs.warFlagsActive << "\n";

	// per-swarm CORN buffer / workers — shows whether the economy loop has stalled.
	for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
	{
		const TrackedBuilding& s = obs.trackedSwarms[i];
		if (!s.valid) continue;
		cerr << "CORTEX_DUMP   swarm[" << i << "] corn=" << s.corn << "/" << s.maxCorn
		     << " maxUnitWorking=" << s.maxUnitWorking
		     << " inside=" << s.unitsInside
		     << " priority=" << s.priority
		     << " nearestWheat=" << s.nearestWheatDist << "\n";
	}

	// --- placement: can the policy even site a new inn / swarm right now? ---
	{
		const int types[2] = { CORTEX_BUILD_FOOD, CORTEX_BUILD_SWARM };
		const char* names[2] = { "inn", "swarm" };
		for (int ti = 0; ti < 2; ti++)
		{
			int valid = 0, bestWheat = -1, bx = -1, by = -1;
			for (int c = 0; c < CORTEX_BUILD_CANDIDATES; c++)
			{
				const BuildCandidate& cand = obs.buildCandidates[types[ti]][c];
				if (!cand.valid) continue;
				if (valid == 0) { bestWheat = cand.wheatDist; bx = cand.x; by = cand.y; }
				valid++;
			}
			cerr << "CORTEX_DUMP PLACE " << names[ti] << " validCandidates=" << valid
			     << " bestWheatDist=" << bestWheat
			     << " at=(" << bx << "," << by << ")\n";
		}
	}

	// --- scouting: what Cortex has actually discovered (FOW-gated) ---
	int flagTargetsValid = 0;
	for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
		if (obs.flagTargets[i].valid) flagTargetsValid++;
	cerr << "CORTEX_DUMP SCOUT flagTargets(seen enemy buildings)=" << flagTargetsValid
	     << " enemyCount=" << obs.enemyCount << "\n";
	for (int i = 0; i < MAX_ENEMY_SLOTS; i++)
	{
		const EnemySlot& e = obs.enemies[i];
		if (!e.active) continue;
		cerr << "CORTEX_DUMP   enemy team=" << e.teamNumber
		     << " discoveredBuildings=" << e.totalBuilding
		     << " visibleUnits(inOurFOW)=" << e.totalUnit << "\n";
	}

	// --- ground truth (diagnostic only; never fed to the policy) ---
	for (int t = 0; t < game->teamsCount(); t++)
	{
		Team* et = game->teams[t];
		if (!et || et->teamNumber == me) continue;
		const TeamStat* es = et->stats.getLatestStat();
		if (!es) continue;
		int as0 = es->upgradeState[ATTACK_STRENGTH][0];
		int as1 = es->upgradeState[ATTACK_STRENGTH][1];
		int as2 = es->upgradeState[ATTACK_STRENGTH][2];
		int as3 = es->upgradeState[ATTACK_STRENGTH][3];
		int sp0 = es->upgradeState[ATTACK_SPEED][0];
		int sp1 = es->upgradeState[ATTACK_SPEED][1];
		int sp2 = es->upgradeState[ATTACK_SPEED][2];
		int sp3 = es->upgradeState[ATTACK_SPEED][3];
		cerr << "CORTEX_DUMP TRUTH enemy team=" << et->teamNumber
		     << " totalUnit=" << es->totalUnit
		     << " warriors=" << es->numberUnitPerType[WARRIOR]
		     << " workers=" << es->numberUnitPerType[WORKER]
		     << " totalBuilding=" << es->totalBuilding
		     << " atkStrengthLvls=[" << as0 << "," << as1 << "," << as2 << "," << as3 << "]"
		     << " atkSpeedLvls=[" << sp0 << "," << sp1 << "," << sp2 << "," << sp3 << "]\n";
	}
	cerr << "CORTEX_DUMP ==== end snapshot ====" << std::endl;
}

shared_ptr<Order> AICortex::getOrder(void)
{
	// Drain any Orders queued by a prior decision cycle, one per tick.
	if (!orderQueue.empty())
	{
		shared_ptr<Order> order = orderQueue.front();
		orderQueue.pop();
		return order;
	}

	// Run the observation -> policy -> action pipeline on a slow cadence.
	timer++;
	if ((timer % OBSERVE_INTERVAL) == 0)
	{
		// Draw the per-game wheat open-margin N exactly once, lazily, on the first
		// decision cycle — not in the constructor — so the sync RNG is live and the
		// draw lands at the same point in the shared stream on every client (all
		// clients run getOrder in lockstep). syncRand(), NEVER rand(): this value
		// must be identical across machines. The draw shifts the shared RNG stream,
		// so it is replay-relevant (validated against the deterministic harness).
		if (wheatOpenMargin < 0)
		{
			const int span = Cortex::WHEAT_OPEN_MARGIN_MAX - Cortex::WHEAT_OPEN_MARGIN_MIN + 1;
			wheatOpenMargin = Cortex::WHEAT_OPEN_MARGIN_MIN
			                + static_cast<int>(syncRand() % span);
		}

		Cortex::CortexObservation obs = Cortex::observe(player, wheatOpenMargin);

		// Stamp each tracked inn's post-build settle clock. The first cycle we see an
		// inn finished we record obs.tick; thereafter ticksSinceFinished is the age,
		// which the policy uses to suppress worker-tuning during the settle window.
		// Prune gids no longer present so a long game's map stays bounded (an inn that
		// died, or upgraded into a site and dropped out of the observation, is forgotten
		// — if it reappears finished it settles afresh, which is the intended behaviour).
		{
			std::map<Uint16, Sint32> stillAlive;
			for (int i = 0; i < obs.innCount; i++)
			{
				Cortex::TrackedBuilding& t = obs.trackedInns[i];
				if (!t.valid || t.gid < 0)
					continue;
				const Uint16 gid = static_cast<Uint16>(t.gid);
				std::map<Uint16, Sint32>::iterator it = innFinishedTick.find(gid);
				const Sint32 firstSeen = (it != innFinishedTick.end()) ? it->second
				                                                       : obs.tick;
				stillAlive[gid] = firstSeen;
				t.ticksSinceFinished = obs.tick - firstSeen;
			}
			innFinishedTick.swap(stillAlive);
		}

		// Start-of-game swarm kickstart: jump the pre-placed starting swarm straight
		// to SWARM_START_WORKERS haulers the first cycle we see it, so the early
		// worker economy ramps immediately instead of crawling up one hauler per
		// cycle from the map's arbitrary initial maxUnitWorking. One-shot. We mirror
		// the change into obs as well (and the engine executor pattern: local write +
		// Order) so the policy's worker-tuning loop tunes FROM this baseline this same
		// cycle rather than fighting it. trackedSwarms[0] is the primary/starting
		// swarm (observe fills by array index, lowest first).
		if (!swarmKickstarted && obs.swarmCount > 0 && obs.trackedSwarms[0].valid
		 && obs.trackedSwarms[0].maxUnitWorking != SWARM_START_WORKERS)
		{
			Cortex::TrackedBuilding& t0 = obs.trackedSwarms[0];
			const int bid = Building::GIDtoID(static_cast<Uint16>(t0.gid));
			Building* b = player->team->myBuildings[bid];
			if (b && b->buildingState == Building::ALIVE && !b->type->isBuildingSite
			 && b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
			{
				b->maxUnitWorking = SWARM_START_WORKERS;
				b->update();
				orderQueue.push(shared_ptr<Order>(
					new OrderModifyBuilding(b->gid, SWARM_START_WORKERS)));
				t0.maxUnitWorking = SWARM_START_WORKERS;
				swarmKickstarted = true;
			}
		}

		// DIAGNOSTIC (gated): compact per-decision-cycle econ trace, to watch the
		// economy ramp. Pure read → stderr; no RNG/order/state touched.
		if (getenv("CORTEX_DUMP_PERIODIC"))
		{
			using namespace Cortex;
			std::cerr << "CORTEX_TRACE t=" << obs.tick
			          << " u=" << obs.totalUnit
			          << " W=" << obs.workers << " E=" << obs.explorers << " A=" << obs.warriors
			          << " freeW=" << obs.freeWorkers
			          << " swarm=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SWARM) << "s"
			          << " pool=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED) << "s"
			          << " algae=" << obs.algaeDiscovered
			          << " reach=" << obs.swimLandReach << "/" << obs.swimWaterReach
			          << " race=" << cortexFinishedBuildings(obs, CORTEX_BUILD_WALKSPEED)
			          << " swarmCand=" << (obs.buildCandidates[CORTEX_BUILD_SWARM][0].valid ? 1 : 0)
			          << " inn=" << cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_FOOD) << "s"
			          << " brk=" << cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_ATTACK) << "s"
			          << " sch=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE) << "s"
			          << " hosp=" << cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_HEAL) << "s"
			          << " hospUpg=" << cortexBuildingsUpgrading(obs, CORTEX_BUILD_HEAL)
			          << " needHeal=" << obs.needHeal
			          << " feedCap=" << obs.feedCapacity
			          << " prod=" << obs.swarmsProducing << "/" << obs.swarmCount
			          << " maxBuildLvl=" << obs.maxBuildLevel
			          << " brkLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK)
			          << " schLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_SCIENCE)
			          << " innLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_FOOD)
			          << " upgBrk=" << obs.upgradableCount[CORTEX_BUILD_ATTACK]
			          << " brkUpgrading=" << cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK)
			          << " underAtk=" << (obs.buildingsUnderAttack + obs.unitsUnderAttack)
			          << "\n";
		}

		// DIAGNOSTIC (gated, one-shot): characterize the game state the first cycle
		// the colony is under attack. Pure read → stderr; no RNG, no order, no
		// persisted state touched, so the sync stream is unaffected.
		if (!attackDumped && getenv("CORTEX_DUMP_ATTACK")
		    && (obs.buildingsUnderAttack > 0 || obs.unitsUnderAttack > 0))
		{
			dumpAttackState(obs);
			attackDumped = true;
		}

		// Release the one-upgrade-in-flight guard once the issued upgrade is visible
		// as a construction site (the policy's own cortexBuildingsUpgrading /
		// finished-count gates take over from here) or the safety timeout lapses.
		if (pendingUpgradeType >= 0
		 && (Cortex::cortexBuildingsUpgrading(obs, pendingUpgradeType) > 0
		     || obs.tick >= pendingUpgradeUntil))
		{
			pendingUpgradeType = -1;
			pendingUpgradeUntil = 0;
		}

		Cortex::CortexAction action = policy.decide(obs);
		translateAction(action, obs);

		// Worker-hauling tuning (swarms / inns / construction sites) runs EVERY
		// decision cycle, in PARALLEL with the primary action above — it emits
		// OrderModifyBuilding worker-count changes, not an OrderCreate competing for
		// the build/upgrade ladder's single action slot, so keeping existing
		// buildings fed never preempts nor waits behind a build decision (and vice
		// versa). translateAction queues its OrderModifyBuildings alongside whatever
		// the primary action queued; they drain one-per-tick over the ticks until the
		// next decision cycle, so both go out. ACTION_NOOP (steady state, buffers in
		// the deadband) enqueues nothing.
		Cortex::CortexAction tune = policy.tuneWorkers(obs);
		translateAction(tune, obs);

		// Wheat-forbidden upkeep runs EVERY decision cycle, in PARALLEL with the
		// primary action above — it is not an ACTION_* the build/upgrade/offense
		// ladder could starve, nor does it consume the cycle's single action slot.
		// The policy still owns whether to paint (starving gate + real diff); when it
		// says yes we enqueue the full ADD/DEL paint here, alongside whatever orders
		// translateAction queued. They drain one-per-tick over the many ticks until
		// the next decision cycle, so both go out — they no longer compete for a turn.
		if (policy.wantWheatProtection(obs))
			enqueueWheatForbidden(obs);

		if (!orderQueue.empty())
		{
			shared_ptr<Order> order = orderQueue.front();
			orderQueue.pop();
			return order;
		}
	}

	return shared_ptr<Order>(new NullOrder());
}
