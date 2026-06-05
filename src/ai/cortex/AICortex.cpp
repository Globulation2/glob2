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
#include <Stream.h>

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
	buildCooldownUntil = 0;
	flagCooldownUntil = 0;
	wheatOpenMargin = -1; // sentinel: drawn lazily on the first decision cycle.
}

bool AICortex::load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	this->player = player;
	stream->readEnterSection("AICortex");
	timer = stream->readUint32("timer");
	buildCooldownUntil = stream->readSint32("buildCooldownUntil");
	flagCooldownUntil = stream->readSint32("flagCooldownUntil");
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
	stream->writeSint32(buildCooldownUntil, "buildCooldownUntil");
	stream->writeSint32(flagCooldownUntil, "flagCooldownUntil");
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
			// then rejects. Suppress new builds until the in-flight one can land.
			if (obs.tick < buildCooldownUntil)
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
			buildCooldownUntil = obs.tick + BUILD_COOLDOWN_TICKS;
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
				break;
			}
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
				break;
			}
			ensureWarFlagAt(obs.defenseTarget.x, obs.defenseTarget.y, action, obs);
			break;
		}

		case Cortex::ACTION_CLEAR_FLAGS:
			// No offense/defense wanted right now — remove our war flag if any.
			clearOwnWarFlag();
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

			// Same cooldown discipline as ACTION_BUILD: an OrderConstruction takes
			// ticks to convert the building into a site, and the observation's
			// upgradableCount won't drop until then. Without the cooldown the policy
			// would re-issue the upgrade on the next decision cycle.
			if (obs.tick < buildCooldownUntil)
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
			buildCooldownUntil = obs.tick + BUILD_COOLDOWN_TICKS;
			break;
		}

		case Cortex::ACTION_PROTECT_WHEAT:
		{
			// Rebuild the ADD/DEL checkerboard masks for our wheat (the bounded
			// colony-region scan) and emit one OrderAlterateForbidden per non-empty
			// diff. No build cooldown: these are area-paint orders, not OrderCreates,
			// and the reconcile is self-correcting — a paint already in place yields
			// an empty diff next cycle, so re-deciding the same intent is free.
			Cortex::WheatReconcile wr =
				Cortex::reconcileWheatForbidden(player, action.wheatOpenMargin, /*buildMasks=*/true);
			const Map* map = &player->team->game->map;
			const Uint8 teamNumber = static_cast<Uint8>(player->team->teamNumber);
			// DEL first so freeing dead tiles never races the ADD of fresh ones.
			if (wr.del.getApplicationCount() > 0)
				orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
					teamNumber, BrushTool::MODE_DEL, &wr.del, map)));
			if (wr.add.getApplicationCount() > 0)
				orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
					teamNumber, BrushTool::MODE_ADD, &wr.add, map)));
			break;
		}

		default:
			// Unknown intent: ignore rather than emit a bogus Order.
			break;
	}
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
		Cortex::CortexAction action = policy.decide(obs);
		translateAction(action, obs);

		if (!orderQueue.empty())
		{
			shared_ptr<Order> order = orderQueue.front();
			orderQueue.pop();
			return order;
		}
	}

	return shared_ptr<Order>(new NullOrder());
}
