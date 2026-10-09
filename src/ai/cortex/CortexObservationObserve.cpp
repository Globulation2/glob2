#include "CortexSnapshotQueries.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "Material.h"
#include <PerformanceTelemetry.h>
#include "CortexObservation.h"
#include "CortexPlacement.h"
#include "CortexPolicy.h"
#include "CortexPlacementGeo.h"
#include "CortexFoodSources.h"
#include "CortexWater.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "TeamStat.h"
#include "unit/UnitConsts.h"
#include "unit/Unit.h"
#include "CortexBuildings.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "map/Map.h"
#include "Ressource.h"

namespace Cortex
{
	CortexObservation observeWorld(MersenneTwister& random, const AIEngine::AIWorldView* game, const AIEngine::TeamView* team, QueryScratch& scratch, const PlanningIntent& intents, std::ostream* diagnostics, int openMargin, Uint16 offenseFlagGid)
	{
		PERF_SCOPE_TIME(AIObserve);
		CortexObservation obs = makeEmptyObservation();

		// Runtime the seeded open margin N regardless; even an early-return (no team)
		// observation carries it, and decide() ignores invalid observations anyway.
		obs.wheatOpenMargin = openMargin;

		if (!game || !team)
			return obs; // valid stays 0 — caller treats as "no observation".



		// Capabilities belong to the effective match header, not saved policy state.
		// Include them in the observation so scorers, facts and ML masks agree.
		obs.upgradesDisabled=game->rules.upgradesDisabled;
		obs.hungerDisabled=game->rules.hungerDisabled;
		obs.combatDisabled=game->rules.peaceful;
		obs.growthDisabled=game->rules.resourceGrowthDisabled;

		obs.tick = (game != NULL) ? static_cast<Sint32>(game->tick) : 0;

		// --- own economy: read straight from the latest team stat snapshot ---
		const TeamStat* stat = &team->statistics;

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
		obs.starvingUnits     = team->starving;
		obs.needFood          = stat->needFood;
		obs.needFoodCritical  = stat->needFoodCritical;
		obs.needFoodNoInns    = stat->needFoodNoInns;
		obs.needHeal          = stat->needHeal;

		// prestige
		obs.prestige          = team->prestige;

		// --- upgrade-decision signals (Phase-2 v4) ---
		// maxBuildLevel is the highest construction qualification among workers
		// able to build. Upgrade eligibility compares it with the target
		// descriptor's requiredWorkerLevel, independently of display level.
		// C++: Team::maxBuildLevel(), team/TeamRouting.cpp:245-259.
		// Cached once here; the per-building Upgradable predicate below reuses it
		// rather than re-scanning every worker per building.
		const int maxBuildLevel = Cortex::maxBuildLevel(*game,*team);
		obs.maxBuildLevel = maxBuildLevel;

		// production / food-supply: one live pass over the colony's buildings.
		// The TeamStat snapshot carries neither signal, so both are computed
		// here directly from game->buildingSlots(team->number) (iterated by index, never a set).
		//   feedCapacity   = units the colony's inns can feed: sum of
		//                    type->maxUnitInside over working, feeding buildings.
		//                    Mirrors AICastor's foodSum (ai/castor/Control.cpp:36-43);
		//                    the live food-supply signal that replaces the dropped
		//                    totalFood/totalFoodCapacity (TeamStat never wrote them).
		//   swarmsProducing = count of FINISHED swarms (buildingState==ALIVE, not a
		//                    site/dead) whose production ratio is nonzero, i.e.
		//                    actually producing units right now.
		//   warFlagsActive = count of our own live WAR_FLAG virtual buildings.
		//                    Virtual flags are registered in game->buildingSlots(team->number) too
		//                    (Game::addBuilding sets myBuildings[id]=b regardless of
		//                    isVirtual, Game_editor.cpp:261), so they show up in this
		//                    same index scan — no separate virtualBuildings pass.
		//                    Reading our OWN state is not a fog-of-war cheat.
		//   upgradableCount = per semantic role, the count of FINISHED instances
		//                    that pass the full engine "Upgradable" predicate right
		//                    now. The predicate mirrors Runtime's
		//                    (ai/shared_runtime/Conditions.cpp:112-129) and the GUI enable-gate
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
		// Single index pass over game->buildingSlots(team->number) filling the building-derived
		// signals and capturing the live war flag's footprint. Split into a helper
		// (CortexObservation.cpp) only to keep each .cpp under the file-size cap;
		// the call sits exactly where the loop ran inline, so the determinism-
		// critical iteration order is unchanged.
		observeBuildings(obs, team, game, intents, maxBuildLevel, offenseFlagGid,
			warFlagFound, warFlagX, warFlagY, warFlagRange);

		// training / upgrade level buckets (one slice per array)
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			obs.buildLevel[lvl] = stat->workersByConstructionLevel[lvl];
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
		for (int i = 0; i < ::Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView* b = game->buildingSlots(team->number)[i];
			if (b == NULL || b->buildingState == ::Building::DEAD)
				continue;
			// C++: Building::underAttackTimer (Uint8), building/Building.h:526
			if (b->underAttackTimer > 0)
				obs.buildingsUnderAttack++;
		}
		for (int k = 0; k < CORTEX_MAX_DEFENSE_FLAGS; k++)
		{
			const AIEngine::BuildingView* pick  = NULL;
			Uint8 pickTimer = 0;
			for (int i = 0; i < ::Building::MAX_COUNT; i++)
			{
				const AIEngine::BuildingView* b = game->buildingSlots(team->number)[i];
				if (b == NULL || b->buildingState == ::Building::DEAD)
					continue;
				if (b->underAttackTimer == 0)
					continue;
				// Too close to an earlier (worse) pick: same assault point.
				// (game is non-NULL in any real match; the guard only covers the
				// degenerate no-game observation, where separation can't be measured.)
				bool nearEarlier = false;
				for (int j = 0; game != NULL && j < k; j++)
					if (warpDistMax(*game,plannedX(intents,*b), plannedY(intents,*b),
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
			obs.defenseTargets[k].x     = plannedX(intents,*pick);
			obs.defenseTargets[k].y     = plannedY(intents,*pick);
			obs.defenseTargets[k].score = pick->underAttackTimer;
		}
		for (int i = 0; i < ::Unit::MAX_COUNT; i++)
		{
			const AIEngine::UnitView* u = game->unitSlots(team->number)[i];
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
			 && u->activity == ::Unit::ACT_RANDOM && u->medical == ::Unit::MED_FREE)
				obs.freeWarriors++;
		}

		// --- map / global facts ---
		if (game != NULL)
		{
			obs.totalPrestige = game->totalPrestige;

			// fruitOnMap: replicate Runtime::check_fruit() directly off the Map
			// (AISharedRuntime/MapInfo::is_resource -> Map::isResourceTakeable) so the
			// direct binding carries no Runtime dependency. Any takeable fruit
			// (CHERRY/ORANGE/PRUNE) anywhere on the map flips this on.
			const auto& map = *game;
			const int w = map.width;
			const int h = map.height;
			// This is an existence query; row order follows the map storage.
			for (int y = 0; y < h && obs.fruitOnMap == 0; y++)
				for (int x = 0; x < w; x++)
					if (MapState::hasMaterial(map.state(),map.tileIndex(x,y),MaterialId::Cherries)
					 || MapState::hasMaterial(map.state(),map.tileIndex(x,y),MaterialId::Oranges)
					 || MapState::hasMaterial(map.state(),map.tileIndex(x,y),MaterialId::Prunes))
					{
						obs.fruitOnMap = 1;
						break;
					}

			// candidate build locations for the building types the economy
			// phase reasons about. Other types keep valid==0 from the empty
			// observation. placeCandidates writes exactly CORTEX_BUILD_CANDIDATES
			// slots (zero-filling unused trailing ones).
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_FOOD,    0, obs.buildCandidates[Cortex::CORTEX_BUILD_FOOD], -1, maxBuildLevel);
            for (const auto& project:game->buildProjects) {
                if(project.teamNumber!=team->number)continue;
                const auto* type=catalogType(*game,project.typeNum);
                if(type->isBuildingSite)type=catalogType(*game,type->nextLevel);
                obs.productionPlannedMask|=type->semantics.production.enabledUnitMask;
            }
            Sint32 productionTargets[CORTEX_UNIT_TYPES];
            CortexPolicy::productionTargets(obs, productionTargets);
            auto productionChoice = selectBuilding(*game,*team,CORTEX_BUILD_SWARM,WORKER,maxBuildLevel);
            for (int unit=0;unit<CORTEX_UNIT_TYPES;++unit) {
                if (!productionTargets[unit] || (obs.productionPlannedMask & (1u<<unit))) continue;
                const auto candidate=selectBuilding(*game,*team,CORTEX_BUILD_SWARM,unit,maxBuildLevel);
                if(candidate.placementType<0) continue;
                obs.productionMissingMask|=1u<<unit;
                if(obs.productionPlacementType<0) {
                    productionChoice=candidate;
                    obs.productionPlacementType=candidate.placementType;
                }
            }
            obs.productionPlacementType=productionChoice.placementType;
            if(productionChoice.placementType>=0)
                placeCandidates(random, game,team,scratch,intents,CORTEX_BUILD_SWARM,0,obs.buildCandidates[CORTEX_BUILD_SWARM],productionChoice.placementType,maxBuildLevel);
            for(int id=0;id<::Building::MAX_COUNT;++id) {
                const auto* building=game->buildingSlots(team->number)[id];
                if(!building || building->buildingState!=::Building::ALIVE || buildingType(*game,*building)->isBuildingSite)continue;
                const auto mask=buildingType(*game,*building)->semantics.production.enabledUnitMask;
                if(!mask)continue;
                for(int unit=0;unit<CORTEX_UNIT_TYPES;++unit)
                    // Strategy changes output presence, not the exact positive weight.
                    // Recipe masking also lets a deliberately paused specialist settle.
                    obs.productionNeedsRetune |= (plannedRatio(intents,*building,unit)>0) !=
                        bool((mask&(1u<<unit)) && productionTargets[unit]>0);
            }
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_HEAL,    0, obs.buildCandidates[Cortex::CORTEX_BUILD_HEAL], -1, maxBuildLevel);
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_SCIENCE, 0, obs.buildCandidates[Cortex::CORTEX_BUILD_SCIENCE], -1, maxBuildLevel);
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_WALKSPEED, 0, obs.buildCandidates[Cortex::CORTEX_BUILD_WALKSPEED], -1, maxBuildLevel);
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_SWIMSPEED, 0, obs.buildCandidates[Cortex::CORTEX_BUILD_SWIMSPEED], -1, maxBuildLevel);
			placeCandidates(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_ATTACK,  0, obs.buildCandidates[Cortex::CORTEX_BUILD_ATTACK], -1, maxBuildLevel);

			// OFFENSE targets: discovered enemy buildings, nearest-first. Filled
			// ONLY from buildings we have legitimately seen (Building::seenByMask),
			// never from unfogged truth — implemented (with the same visibility
			// gating discipline as the enemy-intel pass below) by placeFlagTargets.
			placeFlagTargetsWorld(random, game, team, intents, obs.flagTargets, obs.flagTargetTeam);

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
				for (int i = 0; i < ::Building::MAX_COUNT; i++)
				{
					const AIEngine::BuildingView* b = game->buildingSlots(team->number)[i];
					if (b == NULL || b->buildingState != ::Building::ALIVE
					 || buildingType(*game,*b)->isBuildingSite)
						continue;
					if (!Cortex::servesRole(*game, *buildingType(*game,*b), Cortex::CORTEX_BUILD_FOOD))
						continue;
					const int d = warpDistMax(map,obs.flagTargets[t].x, obs.flagTargets[t].y,
					                              plannedX(intents,*b), plannedY(intents,*b));
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
				const Cortex::AmphibiousAssessment amp = Cortex::assessAmphibiousWorld(
                    game,team,scratch,intents,diagnostics, obs.flagTargets[0].x, obs.flagTargets[0].y,
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
			// POSITION and runtimees underway into the observation before decide() (the
			// flagPosture/latch runtime pattern); the policy's `valid && !underway` check
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
					placeForwardCandidate(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_FOOD,
					                      tx, ty, minD, maxD,
					                      obs.forwardInn, maxBuildLevel);
					// A forward hospital is surfaced only when a finished hospital
					// already exists (advisory support; the inn binds the envelope);
					// the forward inn always leads.
					if (cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL) > 0)
						placeForwardCandidate(random, game, team, scratch, intents, Cortex::CORTEX_BUILD_HEAL,
						                      tx, ty, minD, maxD,
						                      obs.forwardHeal, maxBuildLevel);
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
			const Cortex::SwimAssessment sw = Cortex::assessSwimWorld(game,team,scratch,intents,diagnostics, noPoolYet);
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
			for (int i = 0; i < game->teams.size() && slot < MAX_ENEMY_SLOTS; i++)
			{
				const AIEngine::TeamView* other = &game->teams[i];
				if (other == NULL)
					continue;
				const bool isEnemy = (team->enemies & other->mask) != 0;
				if (!isEnemy || !other->alive)
					continue;

				EnemySlot& es = obs.enemies[slot];
				es.active = 1;
				es.teamNumber = other->number;

				// totalBuilding: enemy buildings we have DISCOVERED. seenByMask is
				// the engine's own per-team "this team has seen this building"
				// record (in the sync checksum), so it is the correct non-cheating
				// signal. team->mask is our vision bit (1<<teamNumber).
				// C++: Building::seenByMask (Uint32), building/Building.h:560
				es.totalBuilding = 0;
				for (int j = 0; j < ::Building::MAX_COUNT; j++)
				{
					const AIEngine::BuildingView* b = game->buildingSlots(other->number)[j];
					if (b == NULL || b->buildingState == ::Building::DEAD)
						continue;
					if ((b->seenByMask & team->mask) != 0)
						es.totalBuilding++;
				}

				// totalUnit: enemy units whose tile is CURRENTLY in our fog-of-war
				// view. We iterate the enemy's own unit array and gate each unit on
				// FOW — we do NOT scan the whole map.
				// C++: Map::isFOWDiscovered(int x,int y,int visionMask), map/Map.h:202
				es.totalUnit = 0;
				for (int j = 0; j < ::Unit::MAX_COUNT; j++)
				{
					const AIEngine::UnitView* u = game->unitSlots(other->number)[j];
					if (u == NULL)
						continue;
					// C++: Unit::posX/posY, unit/Unit.h:220
					if (!isFOWDiscovered(*game,u->posX, u->posY, team->mask))
						continue;
					es.totalUnit++;
					// Straggler grace: visible enemy still inside our flag's stay-range.
					// Same warp-safe Chebyshev metric placeFlagTargets/ensureFlagAt use.
					if (warFlagFound
					 && warpDistMax(*game,u->posX, u->posY, warFlagX, warFlagY) <= warFlagRange)
						obs.enemyUnitsNearFlag++;
					// Threat sizing, per defense point: visible enemy near each building
					// taking fire. defenseTargets[] is already resolved (the building scan
					// above ran first), so each count measures the assaulting force THAT
					// point's flag must match.
					for (int k = 0; k < CORTEX_MAX_DEFENSE_FLAGS; k++)
						if (obs.defenseTargets[k].valid
						 && warpDistMax(*game,u->posX, u->posY,
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

		// --- food sustainability: counts-only reconcile over the colony region ---
		// The full per-tile masks are rebuilt in the action layer (which has the
		// Map to paint into); the observation carries only the cheap diff counts so
		// the pure policy (CortexPolicy::wantFoodSourceProtection) can tell whether the
		// per-cycle wheat-forbidden pass has real work to do.
		const bool farms = game->farmAreasEnabled
			&& !game->rules.resourceGrowthDisabled;
		const Cortex::FoodSourceReconcile wr = Cortex::reconcileFoodSourcesForbiddenWorld(
            game,team,scratch,intents,diagnostics, openMargin, /*buildMasks=*/false, /*liftAll=*/false, farms);
		obs.wheatProtectAddCount = wr.addCount;
		obs.wheatProtectDelCount = wr.delCount;
		if (farms)
			obs.wheatProtectDelCount += Cortex::reconcileFoodSourcesForbiddenWorld(
                game,team,scratch,intents,diagnostics, openMargin, /*buildMasks=*/false, /*liftAll=*/true).delCount;

		obs.valid = 1;
		return obs;
	}
}

namespace Cortex {
CortexObservation observe(MersenneTwister& random, ::Player* player, int margin, Uint16 gid)
{
    if(!player || !player->team)return makeEmptyObservation();
    const auto view=AIEngine::AIWorldView::capture(*player->game, AIEngine::AIWorldView::captureCatalog(*player->game));
    QueryScratch scratch; PlanningIntent intents;
    return observeWorld(random, view.get(),&view->teams[player->teamNumber],scratch,intents,nullptr,margin,gid);
}
void observeBuildings(CortexObservation& observation, ::Team* team, ::Game* game,
    int level,Uint16 gid,bool& found,Sint32& x,Sint32& y,Sint32& range)
{
    const auto view=AIEngine::AIWorldView::capture(*game, AIEngine::AIWorldView::captureCatalog(*game));
    QueryScratch scratch; PlanningIntent intents;
    observeBuildings(observation,&view->teams[team->teamNumber],view.get(),intents,level,gid,found,x,y,range);
}
}
