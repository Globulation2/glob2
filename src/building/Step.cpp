// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "FetchApportionment.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Order.h"
#include "field/AirPathfind.h"
#include "field/TerrainMovementCosts.h"

namespace
{
	/// Weight of the harvest level in a material-fetching candidate's ranking key. The
	/// harvest level dominates the comparison; the walk level breaks ties.
	constexpr int HARVEST_LEVEL_WEIGHT = 10;

	/// Tiles of detour a candidate pays for turning up with a material this
	/// building cannot take. Whatever it carries is lost the moment it harvests
	/// again, so an empty-handed unit this much further away is the better hire,
	/// and a building that does want the cargo gets its chance at the unit. A
	/// price, not a veto: past this margin the loaded unit is still hired.
	constexpr int CARRIED_MATERIAL_PENALTY_TILES = 5;

	/// Composite "experience" key used to rank material-carrying candidates;
	/// higher is preferred.
	int bringMaterialsLevel(const Unit* unit)
	{
		return unit->workerLevel() * HARVEST_LEVEL_WEIGHT + unit->level[WALK];
	}
}

void Building::step(void)
{
	if (type->isBuildingSite && (siteCompletionPending || type->useTeamMaterials)) updateBuildingSite();
	computeWishedMaterials(wishedMaterials);
	if (((owner->game->stepCounter + gid) & 255) == 0)
		freeIdleGradients();

	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
}


void Building::setRecordFailingUnits(bool on)
{
	recordFailingUnits=on;
	if(!on)
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
			unitsFailingByReason[i].clear();
}

void Building::noteUnitFailing(Unit* unit, UnitCantWorkReason reason)
{
	unitsFailingRequirements[reason] += 1;
	// "Not available" is every busy unit of the colony: counted, never marked.
	if(recordFailingUnits && reason!=UnitNotAvailable)
		unitsFailingByReason[reason].push_back(unit->gid);
}

void Building::resetFailureTallies()
{
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
		unitsFailingByReason[i].clear();
	}
}

namespace
{
// Healthy stock hunger deltas fit in 32 bits. Preserve widened subtraction and
// exact signed division for extreme cached or authored values.
int remainingFoodTime(const Unit& unit)
{
    if (unit.hungriness<=0) return INT_MAX;
    const Sint64 delta=Sint64(unit.hungry)-unit.trigHungry;
    if (delta>=INT_MIN && delta<=INT_MAX)
        return int(delta)/unit.hungriness;
    return int(std::clamp<Sint64>(delta/unit.hungriness,INT_MIN,INT_MAX));
}

// Return the already-computed food budget to the material and carrying scans.
// Keep the original member entry point below for existing callers and fixtures.
bool considerBuildingCandidate(Building& building, Unit* unit, int* distBuilding,
                               int airDistance, int& timeLeft)
{
	if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		building.noteUnitFailing(unit, Building::UnitNotAvailable);
		return false;
	}
	if(!building.canUnitWorkHere(unit))
	{
		building.noteUnitFailing(unit, Building::UnitTooLowLevel);
		return false;
	}

	timeLeft=remainingFoodTime(*unit);
	bool accessible=true;
    if(unit->performance[FLY] && airDistance>=0) {
        *distBuilding=airDistance;accessible=airDistance!=INT_MAX;
    } else if(unit->performance[FLY]) {
        const Map& map=*building.owner->map;
        field::AirDistanceField routes(map.getW(),map.getH(),building.posX,building.posY,
            [&map](int x,int y){return map.terrainPropertiesAt(x,y).flyable;},
            [&map](int x,int y){return map.cellRule(map.coordToIndex(x,y)).airCost;},
            map.hasAirTerrainConstraints(),field::AirDistanceDirection::ToDestination);
        if(routes.enabled()) {
            const auto cost=routes.costTo(unit->posX,unit->posY);
            accessible=cost!=decltype(routes)::unreachable;
            *distBuilding=accessible?int((cost+GRADIENT_STEP-1)/GRADIENT_STEP):INT_MAX;
        } else *distBuilding=building.owner->map->warpDistMax(unit->posX,unit->posY,building.posX,building.posY);
    } else accessible=building.owner->map->buildingAvailable(&building,unit->swimClass(),unit->posX,unit->posY,distBuilding,BuildingRoute::Footprint);
    if(!accessible)
	{
		building.noteUnitFailing(unit, Building::UnitCantAccessBuilding);
		return false;
	}
	if((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && *distBuilding>1) || *distBuilding >= timeLeft)
	{
		building.noteUnitFailing(unit, Building::UnitTooFarFromBuilding);
		return false;
	}
	return true;
}

}

bool Building::considerUnitForBuilding(Unit* unit, int* distBuilding, int airDistance)
{
    int timeLeft;
    return considerBuildingCandidate(*this,unit,distBuilding,airDistance,timeLeft);
}

bool Building::considerUnitForMaterial(Unit* unit, int wantedMaterial, int* dist, int airDistance)
{
	if (unit->hasCapability(UnitRuntimeTraits::ExtendedCargo)
		&& !unit->hasDeliverableCargo(*this,wantedMaterial) && !unit->canCarryMaterial(wantedMaterial))
		return false;
	int distBuilding=0, timeLeft;
	if(!considerBuildingCandidate(*this,unit,&distBuilding,airDistance,timeLeft))
		return false;

	int distMaterial = 0;
	Sint32 materialX=0,materialY=0;
	if(!(unit->performance[FLY] ? unit->findMaterialDestination(wantedMaterial,&materialX,&materialY,&distMaterial,fetchesFromMarkets(),this) : owner->map->materialAvailableSlot(owner->teamNumber,wantedMaterial,unit->swimClass(),unit->posX,unit->posY,&distMaterial,fetchesFromMarkets(),this)))
	{
		if(wantedMaterial<HAPPINESS_BASE || wantedMaterial>=HAPPINESS_BASE+HAPPINESS_COUNT)
			noteUnitFailing(unit, UnitCantAccessResource);
		else
			noteUnitFailing(unit, UnitCantAccessFruit);
		return false;
	}
	if((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distMaterial>1) || distMaterial >= timeLeft)
	{
		if(wantedMaterial<HAPPINESS_BASE || wantedMaterial>=HAPPINESS_BASE+HAPPINESS_COUNT)
			noteUnitFailing(unit, UnitTooFarFromResource);
		else
			noteUnitFailing(unit, UnitTooFarFromFruit);
		return false;
	}

	// Score by the whole job, estimating the carry leg rather than reaching
	// for the building distance alone: a unit standing at the building carries
	// as far as it walked out, and one standing at the resource carries the
	// building distance.
	const int wholeTrip = distMaterial + std::max(distBuilding, distMaterial);
	*dist = wholeTrip<<Q8_FIXED_POINT_SHIFT;
	return true;
}

int Building::gatherBringMaterialsCandidates(BringMaterialsCandidate* candidates, int wantedMaterial, const int* airDistances)
{
	// The tallies count units, and the same unit is offered every material the
	// building tries to staff, so start each scan from zero: what the info panel
	// ends up showing is one coherent pass, for the last material attempted.
	resetFailureTallies();

	int count=0;
	for(Unit* unit : owner->liveUnits.entries())
	{
		if(!unit->hasCapability(UnitRuntimeTraits::Transport) || !unit->performance[HARVEST] || !unit->performance[BUILD])
			continue;
		if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
			continue;

		int dist;
		if(considerUnitForMaterial(unit, wantedMaterial, &dist,airDistances?airDistances[Unit::GIDtoID(unit->gid)]:-1))
		{
			candidates[count].unit = unit;
			candidates[count++].distance = dist;
		}
	}
	return count;
}


void Building::fetchApportionment(int targets[MaterialSlotCount], int served[MaterialSlotCount]) const
{
	for(int r=0; r<MaterialSlotCount; ++r)
	{
		int multiplier = type->materialMultiplier[r];
		targets[r] = (materialDeliveryTarget(r)+multiplier-1)/multiplier;
		const int missing=materialDeliveryNeed(r);
		served[r] = targets[r]-(missing+multiplier-1)/multiplier;
	}
	for(std::list<Unit *>::const_iterator ui=unitsWorking.begin(); ui!=unitsWorking.end(); ++ui)
	{
		int purpose = (*ui)->destinationPurpose;
		if(purpose>=0 && purpose<MaterialSlotCount)
			served[purpose]++;
	}
}


bool Building::wantsAnotherDelivery(int r, const int* targets, const int* served)
{
	// neededMaterial covers the physical room for one more delivery, which the
	// delivery counts round away from when maxMaterials is not a whole number of
	// deliveries. served covers the units already on their way.
	return neededMaterial(r)>0 && served[r]<targets[r];
}

void Building::selectUnitCarryingWantedMaterial(const int* targets, const int* served, BringMaterialsSelection& sel, const int* airDistances)
{
	for(Unit* unit : owner->liveUnits.entries())
	{
		if(!unit->hasCapability(UnitRuntimeTraits::Transport) || !unit->performance[BUILD])
			continue;
		if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
			continue;

		int r=unit->carriedMaterial;
		if (unit->hasCapability(UnitRuntimeTraits::ExtendedCargo)) {
			r=-1;
			for (unsigned material=0;material<MaterialCount;++material)
				if (unit->hasDeliverableCargo(*this,material) && wantsAnotherDelivery(material,targets,served)) {
					r=material;
					break;
				}
		}
		if(r<0 || !wantsAnotherDelivery(r, targets, served))
			continue;
		int distBuilding, timeLeft;
		if(!considerBuildingCandidate(*this,unit,&distBuilding,airDistances?airDistances[Unit::GIDtoID(unit->gid)]:-1,timeLeft))
			continue;

		int value=distBuilding-(timeLeft>>1);
		int level = bringMaterialsLevel(unit);
		// Every carrying candidate has its destinationPurpose set to the
		// material it carries, not only the one finally chosen.
		unit->destinationPurpose=r;
		if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
		{
			sel.minValue=value;
			sel.maxLevel=level;
			sel.choosen=unit;
		}
	}
}

void Building::selectFetcher(const BringMaterialsCandidate* candidates, int count, int wantedMaterial, BringMaterialsSelection& sel)
{
	for(int n=0; n<count; ++n)
	{
		Unit* unit=candidates[n].unit;

		// A unit already carrying what is wanted is a delivery, not a fetch, and
		// selectUnitCarryingWantedMaterial has first refusal on it.
		int carried=unit->carriedMaterial;
		if(carried==wantedMaterial)
			continue;

		int value=candidates[n].distance;
		if(carried>=0)
			value += CARRIED_MATERIAL_PENALTY_TILES<<Q8_FIXED_POINT_SHIFT;
		int level = bringMaterialsLevel(unit);
		if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
		{
			sel.minValue=value;
			sel.maxLevel=level;
			sel.choosen=unit;
			unit->destinationPurpose=wantedMaterial;
		}
	}
}


int Building::workRoleTarget(int role) const
{
	bool active[4]{};
	for (int r=0; r<MaterialCount; ++r)
		active[0] |= materialDeliveryNeed(r)>0;
	int count=active[0];
	for (unsigned job=0;job<3;++job) count+=active[job+1]=runtime->attractsRole(job);
	if (!count || !active[role+1]) return 0;
	int rank=0;
	for (int i=0; i<role+1; ++i) rank+=active[i];
	return desiredMaxUnitWorking/count + (rank < desiredMaxUnitWorking%count);
}

bool Building::subscribeWorkStep()
{
	const bool attracts=runtime->attractionRoles!=0;
	if (!attracts) return subscribeToBringMaterialsStep();
	const bool recruitmentRound=subscriptionWorkingTimer>=32;
	bool hired=subscribeToBringMaterialsStep();
	// Fill fair delivery quota before the attraction round can borrow it.
	if (recruitmentRound) while (subscribeToBringMaterialsStep()) hired=true;
	hired=subscribeForFlagingStep() || hired;
	// Attraction exhausted its eligible candidates; unused seats may deliver.
	if (recruitmentRound) while (subscribeToBringMaterialsStep(true)) hired=true;
	return hired;
}

bool Building::subscribeToBringMaterialsStep(bool borrowUnused)
{
	resetFailureTallies();
	if (buildingState==DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringMaterialsStep()...\n", gid);

	bool hired=false;
	int delivering=0;
	for (const Unit* unit : unitsWorking) delivering += unit->activity == Unit::ACT_FILLING;
	if ((Sint32)unitsWorking.size()<desiredMaxUnitWorking && (borrowUnused || delivering<workRoleTarget(-1)))
	{
		int targets[MaterialSlotCount];
		int served[MaterialSlotCount];
		fetchApportionment(targets, served);

        std::array<int,Unit::MAX_COUNT> flyerDistances;
        const int* airDistances=nullptr;
        if(type->runtimeFlyingCarriers) {
            flyerDistances.fill(-1);airDistances=flyerDistances.data();
            const Map& map=*owner->map;
            field::AirDistanceField routes(map.getW(),map.getH(),posX,posY,
                [&map](int x,int y){return map.terrainPropertiesAt(x,y).flyable;},
                [&map](int x,int y){return map.cellRule(map.coordToIndex(x,y)).airCost;},
                map.hasAirTerrainConstraints(),field::AirDistanceDirection::ToDestination);
            for(const auto* unit:owner->liveUnits.entries())if(unit->performance[FLY]) {
                int distance=owner->map->warpDistMax(unit->posX,unit->posY,posX,posY);
                if(routes.enabled()) {
                    const auto cost=routes.costTo(unit->posX,unit->posY);
                    distance=cost==decltype(routes)::unreachable?INT_MAX:int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
                }
                flyerDistances[Unit::GIDtoID(unit->gid)]=distance;
            }
        }
		BringMaterialsSelection sel;
		sel.maxLevel = -1;
		sel.minValue = INT_MAX;
		sel.choosen = NULL;

		// A unit already holding something we want delivers without a fetch trip,
		// so it is taken ahead of the apportionment, which only directs the units
		// we still have to send out. It is subscription-aware in its own right, so
		// it cannot oversubscribe a material either.
		selectUnitCarryingWantedMaterial(targets, served, sel,airDistances);

		// Otherwise staff the material whose subscriptions sit furthest below its
		// share of the building's targets, falling to the next one whenever no
		// unit can actually be hired for it.
		if (sel.choosen==NULL)
		{
			int order[MaterialSlotCount];
			int wanted = FetchApportionment::rank(targets, served, MaterialSlotCount, order);
			for(int i=0; i<wanted && sel.choosen==NULL; ++i)
			{
				int r = order[i];
				if(!wantsAnotherDelivery(r, targets, served))
					continue;
				BringMaterialsCandidate candidates[Unit::MAX_COUNT];
				const int count=gatherBringMaterialsCandidates(candidates, r,airDistances);
				selectFetcher(candidates, count, r, sel);
			}
		}

		if (sel.choosen)
		{
			unitsWorking.push_back(sel.choosen);
			sel.choosen->subscriptionSuccess(this, false);
			owner->swapTask(sel.choosen);
			hired=true;
		}
	}

	updateCallLists();

	if (verbose)
		printf(" ...done\n");
	return hired;
}

bool Building::considerUnitForExplorerFlag(Unit* unit, int* dist, int terrainDistance)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!canUnitWorkHere(unit,true,1))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	int timeLeft = remainingFoodTime(*unit);
    if(!unit->performance[FLY]) {
        int distance=0;
        if(!owner->map->buildingAvailable(this,unit->swimClass(),unit->posX,unit->posY,&distance,BuildingRoute::Combat)) {
            noteUnitFailing(unit,UnitCantAccessBuilding);return false;
        }
        terrainDistance=distance;
    }

	if (terrainDistance == INT_MAX)
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if (terrainDistance >= 0)
	{
		if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && terrainDistance>0) || terrainDistance > timeLeft)
		{
			noteUnitFailing(unit, UnitTooFarFromBuilding);
			return false;
		}
		*dist = int(std::min(std::int64_t(INT_MAX),std::int64_t(terrainDistance)*terrainDistance));
		return true;
	}
	// warpDistSquare returns squared Euclidean distance, so timeLeft is
	// squared here to keep the comparison in the same units. Worker/warrior
	// flags compare against Map::buildingAvailable (linear gradient
	// distance) and must NOT square — see considerUnitForWorkerFlag.
	const Sint64 timeLeftSquared = Sint64(timeLeft)*timeLeft;
	int directdist = owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
	if (timeLeftSquared < directdist)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	*dist = directdist;
	return true;
}

bool Building::considerUnitForWorkerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!canUnitWorkHere(unit,true,0))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	if(unit->performance[FLY]) {
        Sint32 x,y;int distance;
        if(!unit->findAirClearingDestination(this,&x,&y,&distance)){noteUnitFailing(unit,UnitCantAccessResource);return false;}
        if(unit->hungriness>0 && distance>=remainingFoodTime(*unit)){noteUnitFailing(unit,UnitTooFarFromBuilding);return false;}
        *dist=distance;return true;
    }
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringMaterialsStep uses the same pairing.
	int timeLeft = remainingFoodTime(*unit);
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding, BuildingRoute::Clearing))
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distBuilding>0) || distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	if (anyResourceToClear[unit->swimClass()==SWIM_CLASS_COUNT-1 ? 2 : int(canSwim)] == 2)
	{
		noteUnitFailing(unit, UnitCantAccessResource);
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::considerUnitForWarriorFlag(Unit* unit, int* dist, int terrainDistance)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!canUnitWorkHere(unit,true,2))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	if (unit->movement == Unit::MOV_ATTACKING_TARGET)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringMaterialsStep uses the same pairing.
	int timeLeft = remainingFoodTime(*unit);
	if (unit->performance[FLY]) distBuilding=terrainDistance>=0 ? terrainDistance : owner->map->warpDistMax(unit->posX,unit->posY,posX,posY);
    else if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding, BuildingRoute::Combat))
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distBuilding>0) || distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::subscribeForFlagingStep()
{
	if (buildingState==DEAD)
	{
		resetFailureTallies();
		return false;
	}

	bool hired=false;
	subscriptionWorkingTimer++;
	if (subscriptionWorkingTimer>32)
	{
		// Reset stale failure counts for the case where the while loop below
		// doesn't run (building already fully staffed). When the loop does run,
		// this is overwritten by the per-iteration reset on iteration 1.
		resetFailureTallies();
		// One reverse search serves every explorer candidate and all hiring
		// iterations. Ignore temporary flyer occupancy, as building selection
		// does; individual steering resolves it. Uniform maps allocate nothing.
		const Map& map = *owner->map;
		field::AirDistanceField airRoutes(map.getW(),map.getH(),posX,posY,
			[&map](int x,int y) { return map.terrainPropertiesAt(x,y).flyable; },
			[&map](int x,int y) { return map.cellRule(map.coordToIndex(x,y)).airCost; },
			type->runtimeFlyingAttractions && Sint32(unitsWorking.size())<desiredMaxUnitWorking && map.hasAirTerrainConstraints(),
			field::AirDistanceDirection::ToDestination);
		while (((Sint32)unitsWorking.size()<desiredMaxUnitWorking))
		{
			// Per-iteration reset: the same Unit::MAX_COUNT array is rescanned
			// each iteration (already-hired units are filtered via
			// attachedBuilding==this); without this, the same failing units
			// would be counted N times across N iterations.
			resetFailureTallies();

            // A hybrid may be eligible for multiple jobs, but receives one
            // assignment and consumes one building seat after selection.
            Unit* possibleUnits[Unit::MAX_COUNT]{};
            Uint8 possibleJobs[Unit::MAX_COUNT]{};
            int distances[3][Unit::MAX_COUNT]{};
            for (int n=0;n<Unit::MAX_COUNT;++n) {
                Unit* unit=owner->myUnits[n];
                if (!unit || unit->attachedBuilding==this) continue;
                const auto& interaction=runtime->interaction(unit->typeNum);
                int travelDistance=-1;
                if(unit->performance[FLY] && airRoutes.enabled()){const auto cost=airRoutes.costTo(unit->posX,unit->posY);travelDistance=cost==decltype(airRoutes)::unreachable ? INT_MAX : int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);}
                if (interaction.has(BuildingUnitInteraction::Explore)) {
                    if (considerUnitForExplorerFlag(unit,&distances[1][n],travelDistance)) possibleJobs[n]|=2;
                }
                if (interaction.has(BuildingUnitInteraction::Clear) && considerUnitForWorkerFlag(unit,&distances[0][n])) possibleJobs[n]|=1;
                if (interaction.has(BuildingUnitInteraction::Defend) && considerUnitForWarriorFlag(unit,&distances[2][n],travelDistance)) possibleJobs[n]|=4;
                if (possibleJobs[n]) possibleUnits[n]=unit;
            }

			int assigned[3]{};
			for (const Unit* unit : unitsWorking)
				if (unit->activity == Unit::ACT_FLAG) {
                    const int job=int(unit->jobPurpose)-int(UnitJobPurpose::Clear);
                    if (job>=0 && job<3) ++assigned[job];
                }
			Unit* choosen=nullptr;
			int chosenCount=INT_MAX;
            int chosenJob=-1;
			// Choose the least staffed eligible attraction role, then use that
			// role's established ranking among its candidate units.
			for (int pass=0; pass<2 && !choosen; ++pass)
			for (int role=0; role<3; ++role)
			{
				if (!runtime->attractsRole(role) || (!pass && assigned[role] >= workRoleTarget(role))) continue;
				Unit* best=nullptr;
				Sint64 bestLevel=role == 2 ? INT64_MIN : INT64_MAX;
				Sint64 bestValue=INT64_MAX;
				for (int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if (!unit || !(possibleJobs[n]&(1u<<role))) continue;
					Sint64 timeLeft=(unit->hungriness>0 ? (Sint64(unit->hungry)-(role == 0 ? unit->trigHungry : 0))/unit->hungriness : INT_MAX);
					Sint64 hp=(Sint64(unit->hp)*16)/std::max(1,unit->runtimeTraits().flagRankingHealth);
					if (role == 1) { timeLeft=std::clamp<Sint64>(timeLeft,-1000000000,1000000000); timeLeft*=timeLeft; hp=std::clamp<Sint64>(hp,-1000000000,1000000000); hp*=hp; }
					const Sint64 value=distances[role][n]-(role == 0 ? 1 : 2)*(timeLeft+hp);
					const Sint64 level=role == 0 ? unit->workerLevel() : role == 1 ? unit->level[MAGIC_ATTACK_GROUND] : Sint64(unit->performance[ATTACK_SPEED])*unit->getRealAttackStrength();
					if ((role == 2 ? level>bestLevel : level<bestLevel) || (level==bestLevel && value<bestValue))
					{ best=unit; bestLevel=level; bestValue=value; }
				}
				if (best && assigned[role]<chosenCount)
				{ choosen=best; chosenCount=assigned[role]; chosenJob=role; }
			}

			if (choosen)
			{
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess(this,false,true,static_cast<UnitJobPurpose>(int(UnitJobPurpose::Clear)+chosenJob));
				hired=true;
			}
			else
				break;
		}

		updateCallLists();

		subscriptionWorkingTimer=0;
	}
	return hired;
}


void Building::subscribeUnitForInside(Unit* unit)
{
	if (!canOfferService(unit, unit->destinationPurpose)) { unit->standardRandomActivity(); return; }
	reserveService(unit);
	unitsInside.push_back(unit);
	unit->subscriptionSuccess(this, true);
	updateCallLists();
}
