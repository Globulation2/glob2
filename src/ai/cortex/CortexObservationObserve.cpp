// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexObservation.h"
#include "CortexPlacement.h"
#include "CortexPlacementGeo.h"
#include "CortexWheat.h"
#include "CortexWater.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "unit/UnitConsts.h"
#include "unit/Unit.h"
#include "IntBuildingType.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "map/Map.h"
#include "Ressource.h"

namespace Cortex
{
	CortexObservation observe(Player* player, int openMargin, Uint16 offenseFlagGid)
	{
		CortexObservation obs = makeEmptyObservation();

		// Echo the seeded open margin N regardless; even an early-return (no team)
		// observation carries it, and decide() ignores invalid observations anyway.
		obs.wheatOpenMargin = openMargin;

		if (player == NULL || player->team == NULL)
			return obs; // valid stays 0 — caller treats as "no observation".

		Team* team = player->team;
		Game* game = team->game;

		obs.tick = (game != NULL) ? static_cast<Sint32>(game->stepCounter) : 0;

		// --- own economy: read straight from the latest team stat snapshot ---
		const TeamStat* stat = team->stats.getLatestStat();

		// population
		obs.totalUnit         = stat->totalUnit;
		obs.workers           = stat->numberUnitPerType[WORKER];
		obs.explorers         = stat->numberUnitPerType[EXPLORER];
		obs.warriors          = stat->numberUnitPerType[WARRIOR];
		obs.freeWorkers       = stat->isFree[WORKER];
		obs.totalFree         = stat->totalFree;
		obs.totalNeeded       = stat->totalNeeded;
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
			obs.totalNeededPerLevel[lvl] = stat->totalNeededPerLevel[lvl];

		// food / health pressure
		obs.totalBuilding     = stat->totalBuilding;
		obs.starvingUnits     = team->stats.getStarvingUnits();
		obs.needFood          = stat->needFood;
		obs.needFoodCritical  = stat->needFoodCritical;
		obs.needFoodNoInns    = stat->needFoodNoInns;
		obs.needHeal          = stat->needHeal;

		// prestige
		obs.prestige          = team->prestige;

		// --- upgrade-decision signals (Phase-2 v4) ---
		// maxBuildLevel is the highest BUILD level among our workers and is the
		// engine's own gate on whether a finished building may be upgraded: a
		// building at type->level L is upgradable only when maxBuildLevel > L.
		// C++: Team::maxBuildLevel(), team/TeamRouting.cpp:245-259.
		// Cached once here; the per-building Upgradable predicate below reuses it
		// rather than re-scanning every worker per building.
		const int maxBuildLevel = team->maxBuildLevel();
		obs.maxBuildLevel = maxBuildLevel;

		// production / food-supply: one live pass over the colony's buildings.
		// The TeamStat snapshot carries neither signal, so both are computed
		// here directly from team->myBuildings (iterated by index, never a set).
		//   feedCapacity   = units the colony's inns can feed: sum of
		//                    type->maxUnitInside over working, feeding buildings.
		//                    Mirrors AICastor's foodSum (ai/castor/Control.cpp:36-43);
		//                    the live food-supply signal that replaces the dropped
		//                    totalFood/totalFoodCapacity (TeamStat never wrote them).
		//   swarmsProducing = count of FINISHED swarms (buildingState==ALIVE, not a
		//                    site/dead) whose production ratio is nonzero, i.e.
		//                    actually producing units right now.
		//   warFlagsActive = count of our own live WAR_FLAG virtual buildings.
		//                    Virtual flags are registered in team->myBuildings too
		//                    (Game::addBuilding sets myBuildings[id]=b regardless of
		//                    isVirtual, Game_editor.cpp:261), so they show up in this
		//                    same index scan — no separate virtualBuildings pass.
		//                    Reading our OWN state is not a fog-of-war cheat.
		//   upgradableCount = per IntBuildingType, the count of FINISHED instances
		//                    that pass the full engine "Upgradable" predicate right
		//                    now. The predicate mirrors Echo's
		//                    (ai/echo/Conditions.cpp:112-129) and the GUI enable-gate
		//                    (gui/GameGUIInput.cpp:421-427): the building must be
		//                    ALIVE, not a site, at full HP, not already
		//                    upgrading/repairing, have a next level, clear the
		//                    maxBuildLevel gate, and its larger next-level footprint
		//                    must fit. Lets the policy ask "can I upgrade this type?"
		//                    without re-deriving the engine's spatial/hp predicates.
		obs.feedCapacity            = 0;
		obs.swarmsProducing         = 0;
		obs.swarmsProducingExplorer = 0;
		obs.swarmsProducingWarrior  = 0;
		obs.swarmsProducingWorker   = 0;
		obs.warFlagsActive          = 0;
		obs.enemyUnitsNearFlag      = 0;
		obs.freeWarriors            = 0;
		obs.swarmCount              = 0;
		obs.innCount                = 0;
		// Captured from our live war flag (if any) so the opponents pass below can
		// count enemy stragglers still inside its stay-range. Cortex only ever runs a
		// single flag; if more than one were live, the last seen wins (harmless).
		bool   warFlagFound = false;
		Sint32 warFlagX     = 0;
		Sint32 warFlagY     = 0;
		Sint32 warFlagRange = 0;
		// Single index pass over team->myBuildings filling the building-derived
		// signals and capturing the live war flag's footprint. Split into a helper
		// (CortexObservation.cpp) only to keep each .cpp under the file-size cap;
		// the call sits exactly where the loop ran inline, so the determinism-
		// critical iteration order is unchanged.
		observeBuildings(obs, team, game, maxBuildLevel, offenseFlagGid,
			warFlagFound, warFlagX, warFlagY, warFlagRange);

		// training / upgrade level buckets (one slice per array)
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			obs.buildLevel[lvl]               = stat->upgradeState[BUILD][lvl];
			// WALK == 3 (unit/UnitConsts.h:13). Any-type row == workers+warriors:
			// explorers have performance[WALK]==0 at every level (game/entities/Race.cpp),
			// so they never enter this bucket. Racetrack expand-vs-upgrade gate.
			obs.walkLevel[lvl]                = stat->upgradeState[WALK][lvl];
			// WARRIOR-only WALK slice: the attack-range envelope scales with the
			// wave's SLOWEST warrior (lowest occupied level), so the per-type slice
			// is needed — the any-type row above mixes in workers.
			obs.warriorWalkLevel[lvl]         = stat->upgradeStatePerType[WARRIOR][WALK][lvl];
			obs.attackSpeedLevel[lvl]         = stat->upgradeState[ATTACK_SPEED][lvl];
			// C++: ATTACK_STRENGTH == 9, unit/UnitConsts.h:22
			obs.attackStrengthLevel[lvl]      = stat->upgradeState[ATTACK_STRENGTH][lvl];
			// SWIM is 1-based in storage (index 0 == cannot swim); copied verbatim.
			obs.workerSwimLevel[lvl]          = stat->upgradeStatePerType[WORKER][SWIM][lvl];
			// WARRIOR SWIM slice, same 1-based storage — the amphibious commit gate and
			// the swim-staging hold count the swim-capable warriors from it (levels >= 1).
			obs.warriorSwimLevel[lvl]         = stat->upgradeStatePerType[WARRIOR][SWIM][lvl];
			obs.explorerMagicGroundLevel[lvl] = stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl];
		}

		// Swim-capable warriors: SWIM is 1-based in storage (index 0 == cannot swim), so
		// the swim-capable count is the sum over levels >= 1. This is the army that can
		// actually reach a water-locked target; the amphibious offense gate uses it.
		obs.swimWarriors = 0;
		for (int lvl = 1; lvl < CORTEX_UNIT_LEVELS; lvl++)
			obs.swimWarriors += obs.warriorSwimLevel[lvl];

		// full per-type, per-long-level building histogram (verbatim mirror;
		// the long-level encoding is decoded by the cortex* helpers, not here).
		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = stat->numberBuildingPerTypePerLevel[t][l];

		// --- defense triggers: our own entities currently taking fire ---
		// Reading our OWN units/buildings is not a fog cheat. underAttackTimer is
		// the engine's "this entity was shot recently" countdown; nonzero => under
		// attack right now. The building scan also picks defenseTargets[]: up to
		// CORTEX_MAX_DEFENSE_FLAGS friendly buildings taking fire, worst-first
		// (highest underAttackTimer in slot 0), each at least
		// CORTEX_DEFENSE_TARGET_SEPARATION from every earlier pick so two flags
		// never cover one assault point. Greedy K-pass selection over the index
		// scan: deterministic (strict > keeps the first-seen on timer ties), and
		// K * MAX_COUNT stays trivially cheap. Iterate by index, never a std::set.
		obs.buildingsUnderAttack = 0;
		obs.unitsUnderAttack     = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			// C++: Building::underAttackTimer (Uint8), building/Building.h:526
			if (b->underAttackTimer > 0)
				obs.buildingsUnderAttack++;
		}
		for (int k = 0; k < CORTEX_MAX_DEFENSE_FLAGS; k++)
		{
			Building* pick  = NULL;
			Uint8 pickTimer = 0;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				if (b->underAttackTimer == 0)
					continue;
				// Too close to an earlier (worse) pick: same assault point.
				// (game is non-NULL in any real match; the guard only covers the
				// degenerate no-game observation, where separation can't be measured.)
				bool nearEarlier = false;
				for (int j = 0; game != NULL && j < k; j++)
					if (game->map.warpDistMax(b->posX, b->posY,
					        obs.defenseTargets[j].x, obs.defenseTargets[j].y)
					    < CORTEX_DEFENSE_TARGET_SEPARATION)
					{
						nearEarlier = true;
						break;
					}
				if (nearEarlier)
					continue;
				// strict > so the first-seen worst wins ties (deterministic).
				if (b->underAttackTimer > pickTimer)
				{
					pick      = b;
					pickTimer = b->underAttackTimer;
				}
			}
			if (pick == NULL)
				break; // no further separated threat point.
			obs.defenseTargets[k].valid = 1;
			// C++: Building::posX/posY, building/Building.h:523
			obs.defenseTargets[k].x     = pick->posX;
			obs.defenseTargets[k].y     = pick->posY;
			obs.defenseTargets[k].score = pick->underAttackTimer;
		}
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit* u = team->myUnits[i];
			if (u == NULL)
				continue;
			// C++: Unit::underAttackTimer (Uint8), unit/Unit.h:241
			if (u->underAttackTimer > 0)
				obs.unitsUnderAttack++;
			// Free warriors: the exact pool a war flag's recruiter will draw from
			// (Building::considerUnitForWarriorFlag requires activity == ACT_RANDOM &&
			// medical == MED_FREE). A warrior already on a flag is ACT_FLAG and is never
			// poached, so this counts only the immediately-recruitable reserve.
			if (u->typeNum == WARRIOR
			 && u->activity == Unit::ACT_RANDOM && u->medical == Unit::MED_FREE)
				obs.freeWarriors++;
		}

		// --- map / global facts ---
		if (game != NULL)
		{
			obs.totalPrestige = game->totalPrestige;

			// fruitOnMap: replicate Echo::check_fruit() directly off the Map
			// (AIEcho/MapInfo::is_ressource -> Map::isRessourceTakeable) so the
			// direct binding carries no Echo dependency. Any takeable fruit
			// (CHERRY/ORANGE/PRUNE) anywhere on the map flips this on.
			Map& map = game->map;
			const int w = map.getW();
			const int h = map.getH();
			for (int x = 0; x < w && obs.fruitOnMap == 0; x++)
				for (int y = 0; y < h; y++)
					if (map.isRessourceTakeable(x, y, CHERRY)
					 || map.isRessourceTakeable(x, y, ORANGE)
					 || map.isRessourceTakeable(x, y, PRUNE))
					{
						obs.fruitOnMap = 1;
						break;
					}

			// candidate build locations for the building types the economy
			// phase reasons about. Other types keep valid==0 from the empty
			// observation. placeCandidates writes exactly CORTEX_BUILD_CANDIDATES
			// slots (zero-filling unused trailing ones).
			placeCandidates(game, team, IntBuildingType::FOOD_BUILDING,    0, obs.buildCandidates[IntBuildingType::FOOD_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SWARM_BUILDING,   0, obs.buildCandidates[IntBuildingType::SWARM_BUILDING]);
			placeCandidates(game, team, IntBuildingType::HEAL_BUILDING,    0, obs.buildCandidates[IntBuildingType::HEAL_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SCIENCE_BUILDING, 0, obs.buildCandidates[IntBuildingType::SCIENCE_BUILDING]);
			placeCandidates(game, team, IntBuildingType::WALKSPEED_BUILDING, 0, obs.buildCandidates[IntBuildingType::WALKSPEED_BUILDING]);
			placeCandidates(game, team, IntBuildingType::SWIMSPEED_BUILDING, 0, obs.buildCandidates[IntBuildingType::SWIMSPEED_BUILDING]);
			placeCandidates(game, team, IntBuildingType::ATTACK_BUILDING,  0, obs.buildCandidates[IntBuildingType::ATTACK_BUILDING]);

			// OFFENSE targets: discovered enemy buildings, nearest-first. Filled
			// ONLY from buildings we have legitimately seen (Building::seenByMask),
			// never from unfogged truth — implemented (with the same visibility
			// gating discipline as the enemy-intel pass below) by placeFlagTargets.
			placeFlagTargets(game, team, obs.flagTargets);

			// Per-target SUPPORT DISTANCE (v18): how far each offense target sits
			// from our nearest FINISHED inn — the attack-range gate's input. Food is
			// the binding support: a war party fights only as far from its inn as the
			// hunger clock allows. A forward HOSPITAL is advisory — it speeds recovery
			// and is still surfaced/built below when a finished hospital exists, but it
			// does NOT bind the envelope. Reading our OWN buildings is not a fog cheat.
			// -1 when we have no finished inn (an army with no food source projects
			// nowhere).
			for (int t = 0; t < CORTEX_FLAG_TARGETS; t++)
			{
				if (!obs.flagTargets[t].valid)
					continue;
				int innDist = -1;
				for (int i = 0; i < Building::MAX_COUNT; i++)
				{
					Building* b = team->myBuildings[i];
					if (b == NULL || b->buildingState != Building::ALIVE
					 || b->type->isBuildingSite)
						continue;
					if (b->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
						continue;
					const int d = map.warpDistMax(obs.flagTargets[t].x, obs.flagTargets[t].y,
					                              b->posX, b->posY);
					if (innDist < 0 || d < innDist)
						innDist = d;
				}
				obs.flagTargetSupportDist[t] = innDist;
			}

			// AMPHIBIOUS CAMPAIGN (v19): classify the PRIMARY target (flagTargets[0], the
			// nearest discovered enemy building) and, when the shortest path to it crosses
			// water, pick a landing zone. Two full-map BFS (three when amphibious) via the
			// CortexWater classifier — the offense-decision-path cost the design accepts.
			// The standoff set is the discovered enemy buildings the observation already
			// carries (the valid flagTargets); keeping the swimmers clear of them is why the
			// landing zone stands off. Computed only when a target exists.
			if (obs.flagTargets[0].valid)
			{
				Sint32 standoffX[CORTEX_FLAG_TARGETS];
				Sint32 standoffY[CORTEX_FLAG_TARGETS];
				int standoffCount = 0;
				for (int t = 0; t < CORTEX_FLAG_TARGETS; t++)
					if (obs.flagTargets[t].valid)
					{
						standoffX[standoffCount] = obs.flagTargets[t].x;
						standoffY[standoffCount] = obs.flagTargets[t].y;
						standoffCount++;
					}
				const Cortex::AmphibiousAssessment amp = Cortex::assessAmphibious(
					player, obs.flagTargets[0].x, obs.flagTargets[0].y,
					standoffX, standoffY, standoffCount,
					Cortex::cortexTuning().landingStandoffTiles,
					Cortex::cortexTuning().forwardRallyPathDist);
				obs.campaignAmphibious = amp.amphibious;
				obs.campaignLandDist   = amp.landDist;
				obs.campaignSwimDist   = amp.swimDist;
				obs.landingZoneValid   = amp.landingValid;
				obs.landingZoneX       = amp.landingX;
				obs.landingZoneY       = amp.landingY;
				obs.forwardRallyValid  = amp.forwardRallyValid;
				obs.forwardRallyX      = amp.forwardRallyX;
				obs.forwardRallyY      = amp.forwardRallyY;
			}

			// FORWARD-BASE candidates (v18): computed only in the state they cure —
			// we have an army, know a target, and EVERY known target is beyond the
			// attack range. The two full-map placement scans are gated to exactly
			// that state, so the steady-state observe cost is unchanged. The
			// double-order guard (forwardInnUnderway/forwardHealUnderway) is NOT
			// derived here by proximity — AICortex tracks the ordered forward site by
			// POSITION and echoes underway into the observation before decide() (the
			// flagPosture/latch echo pattern); the policy's `valid && !underway` check
			// suppresses double-ordering. Candidates are surfaced whenever the
			// out-of-range state holds.
			{
				// STAGING-POINT ANCHOR (v20): a long campaign — a forward rally or an
				// amphibious landing — stages its waves at a point the warp-distance
				// envelope never reasoned about, so the candidate anchors THERE and the
				// window shrinks to "near the staging point" (the wave must eat where it
				// masses). The staging point already stands off every discovered enemy
				// building by landingStandoffTiles, so the enemy-distance floor drops to
				// 0. Without a staging point the v18 behavior is unchanged: candidates
				// only in the every-target-out-of-envelope state, anchored on the target.
				const int range = cortexAttackRange(obs);
				const bool staged = (obs.campaignAmphibious && obs.landingZoneValid)
				                 || obs.forwardRallyValid;
				if (range > 0 && obs.warriors > 0 && obs.flagTargets[0].valid
				 && (cortexInRangeTargetSlot(obs) < 0 || staged))
				{
					int tx, ty, minD, maxD;
					if (staged)
					{
						tx = obs.campaignAmphibious ? obs.landingZoneX : obs.forwardRallyX;
						ty = obs.campaignAmphibious ? obs.landingZoneY : obs.forwardRallyY;
						minD = 0;
						maxD = CORTEX_FORWARD_STAGING_MAX_DIST;
					}
					else
					{
						tx = obs.flagTargets[0].x;
						ty = obs.flagTargets[0].y;
						minD = CORTEX_FORWARD_MIN_ENEMY_DIST;
						maxD = range - CORTEX_FORWARD_RANGE_SLACK;
					}
					placeForwardCandidate(game, team, IntBuildingType::FOOD_BUILDING,
					                      tx, ty, minD, maxD,
					                      obs.forwardInn);
					// A forward hospital is surfaced only when a finished hospital
					// already exists (advisory support; the inn binds the envelope);
					// the forward inn always leads.
					if (cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL) > 0)
						placeForwardCandidate(game, team, IntBuildingType::HEAL_BUILDING,
						                      tx, ty, minD, maxD,
						                      obs.forwardHeal);
				}
			}

			// Swim/water signals. algaeDiscovered + algaeReachable (shore-harvestable
			// algae) gate the ALGA-consuming school build/upgrade, which can fire at any
			// stage, so they are computed EVERY cycle. The land-vs-swim reach COUNTS feed
			// only the one-shot swimming-pool decision, which never re-fires once a pool
			// exists; the pool-pass flood-fill is the one non-trivial cost here, so we
			// skip it (wantSwimReach=false) when a pool is already up or building. The
			// building histogram is populated above, so the pool count is available here.
			const bool noPoolYet =
			    cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED) == 0
			 && cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED) == 0;
			const Cortex::SwimAssessment sw = Cortex::assessSwim(player, noPoolYet);
			obs.algaeDiscovered = sw.algaeDiscovered;
			obs.swimLandReach   = sw.landReach;
			obs.swimWaterReach  = sw.waterReach;
			obs.algaeReachable  = sw.algaeReachable;
		}

		// --- opponents ---
		// Fairness: the engine grants AIs unfogged access to enemy state, so we
		// must NOT copy enemy ground truth here — that would be a fog-of-war cheat
		// baked into the observation surface (see AIImplementation.h:45-48: the
		// engine does NOT fog AI reads, so gating is OUR job, and
		// docs/AI/cortex/README.md). We expose only which enemy teams exist and
		// are alive (public, shown in the UI) plus VISIBILITY-GATED intel: each
		// enemy entity is counted only if we can legitimately see it. We iterate
		// the enemy's OWN entity arrays by index (never a std::set) and gate each
		// entry — we never scan unfogged truth.
		if (game != NULL)
		{
			int slot = 0;
			for (int i = 0; i < game->teamsCount() && slot < MAX_ENEMY_SLOTS; i++)
			{
				Team* other = game->teams[i];
				if (other == NULL)
					continue;
				const bool isEnemy = (team->enemies & other->me) != 0;
				if (!isEnemy || !other->isAlive)
					continue;

				EnemySlot& es = obs.enemies[slot];
				es.active = 1;
				es.teamNumber = other->teamNumber;

				// totalBuilding: enemy buildings we have DISCOVERED. seenByMask is
				// the engine's own per-team "this team has seen this building"
				// record (in the sync checksum), so it is the correct non-cheating
				// signal. team->me is our vision bit (1<<teamNumber).
				// C++: Building::seenByMask (Uint32), building/Building.h:560
				es.totalBuilding = 0;
				for (int j = 0; j < Building::MAX_COUNT; j++)
				{
					Building* b = other->myBuildings[j];
					if (b == NULL || b->buildingState == Building::DEAD)
						continue;
					if ((b->seenByMask & team->me) != 0)
						es.totalBuilding++;
				}

				// totalUnit: enemy units whose tile is CURRENTLY in our fog-of-war
				// view. We iterate the enemy's own unit array and gate each unit on
				// FOW — we do NOT scan the whole map.
				// C++: Map::isFOWDiscovered(int x,int y,int visionMask), map/Map.h:202
				es.totalUnit = 0;
				for (int j = 0; j < Unit::MAX_COUNT; j++)
				{
					Unit* u = other->myUnits[j];
					if (u == NULL)
						continue;
					// C++: Unit::posX/posY, unit/Unit.h:220
					if (!game->map.isFOWDiscovered(u->posX, u->posY, team->me))
						continue;
					es.totalUnit++;
					// Straggler grace: visible enemy still inside our flag's stay-range.
					// Same warp-safe Chebyshev metric placeFlagTargets/ensureFlagAt use.
					if (warFlagFound
					 && game->map.warpDistMax(u->posX, u->posY, warFlagX, warFlagY) <= warFlagRange)
						obs.enemyUnitsNearFlag++;
					// Threat sizing, per defense point: visible enemy near each building
					// taking fire. defenseTargets[] is already resolved (the building scan
					// above ran first), so each count measures the assaulting force THAT
					// point's flag must match.
					for (int k = 0; k < CORTEX_MAX_DEFENSE_FLAGS; k++)
						if (obs.defenseTargets[k].valid
						 && game->map.warpDistMax(u->posX, u->posY,
						        obs.defenseTargets[k].x, obs.defenseTargets[k].y)
						    <= CORTEX_THREAT_SCAN_RADIUS)
							obs.defenseThreatCount[k]++;
					// Enemy warrior intel (the war-preparation gate): the highest
					// ATTACK_STRENGTH level among enemy warriors we can SEE this cycle.
					// Already inside the FOW gate above, so never unfogged truth.
					// C++: Unit::level[] (unit/Unit.h), ATTACK_STRENGTH == 9.
					if (u->typeNum == WARRIOR
					 && u->level[ATTACK_STRENGTH] > obs.enemyWarriorLevelVisible)
						obs.enemyWarriorLevelVisible = u->level[ATTACK_STRENGTH];
				}

				es.prestige = 0; // prestige is not a visible signal; left unfilled
				                 // to avoid a fog-of-war cheat.
				slot++;
			}
			obs.enemyCount = slot;
		}

		// --- wheat sustainability: counts-only reconcile over the colony region ---
		// The full per-tile masks are rebuilt in the action layer (which has the
		// Map to paint into); the observation carries only the cheap diff counts so
		// the pure policy (CortexPolicy::wantWheatProtection) can tell whether the
		// per-cycle wheat-forbidden pass has real work to do.
		{
			const Cortex::WheatReconcile wr =
				Cortex::reconcileWheatForbidden(player, openMargin, /*buildMasks=*/false);
			obs.wheatProtectAddCount = wr.addCount;
			obs.wheatProtectDelCount = wr.delCount;
		}

		obs.valid = 1;
		return obs;
	}
}
