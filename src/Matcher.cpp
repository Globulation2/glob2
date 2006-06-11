/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at nct at ysagoon dot com or
  nuage at ysagoon dot com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Matcher.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"
#include <vector>

//! A result of the matching, ready for sorting
class MatchResult
{
public:
	Uint32 score; //!< the bigger the score variable is, the worst the matching is
	Unit *unit;
	Building *building;
	MatcherConsts::Activity activity;
	
	bool operator < (const MatchResult &other) const
	{
		return score < other.score;
	}
	
	MatchResult(Uint32 score, Unit *unit, Building *building, MatcherConsts::Activity activity) :
		score(score),
		unit(unit),
		building(building),
		activity(activity)
	{
	}
};

//! Return true if ground unit can reach building with its hungryness and write dist. Return false otherwise
bool Matcher::canReachGround(Unit *unit, Building *building, Uint32 *dist, const bool ignoreHungryness)
{
	// can we reach the building ? (fast test to avoid gradiant call)
	int airDist2 = map->warpDistSquare(unit->posX, unit->posY, building->posX, building->posY);
	int maxDist;
	if (ignoreHungryness)
		maxDist = unit->stepsLeftUntilDead;
	else
		maxDist = unit->stepsLeftUntilHungry;
	int maxDist2 = maxDist * maxDist;
	if (airDist2 >= maxDist2)
		return false; // we can't reach this building, it is far too far for the unit's hungryness
	
	// can we reach the building ? (better test using gradient)
	int gradientDist;
	if (!map->buildingAvailable(building, unit->performance[SWIM], unit->posX, unit->posY, &gradientDist))
		return false; // we can't reach this building, it is unreachable
	if (gradientDist >= maxDist)
		return false; // we can't reach this building, it is too far for the unit's hungryness
	
	*dist = gradientDist;
	return true;
}

//! Return true if air unit can reach building with its hungryness and write dist. Return false otherwise
bool Matcher::canReachAir(Unit *unit, Building *building, Uint32 *dist, const bool ignoreHungryness)
{
	int airDist2 = map->warpDistSquare(unit->posX, unit->posY, building->posX, building->posY);
	int maxDist;
	if (ignoreHungryness)
		maxDist = unit->stepsLeftUntilDead;
	else
		maxDist = unit->stepsLeftUntilHungry;
	int maxDist2 = maxDist * maxDist;
	if (airDist2 >= maxDist2)
		return false; // we can't reach this building, it is far too far for the unit's hungryness
		
	*dist = 1 + (Uint32)sqrt(airDist2);
	return true;
}

//! Return true if unit can reach building with its hungryness and write dist. Return false otherwise
bool Matcher::canReach(Unit *unit, Building *building, Uint32 *dist, const bool ignoreHungryness)
{
	if (unit->performance[FLY] > 0)
		return canReachAir(unit, building, dist, ignoreHungryness);
	else
		return canReachGround(unit, building, dist, ignoreHungryness);
}

//! Constructor, get direct access pointer to map from team
Matcher::Matcher(Team *team) :
	team(team)
{
	map = team->map;
}

//! Match all free units vs all buildings to globally allocate at once
void Matcher::matchFree()
{
	enum BuildingTypePriorities
	{
		BTP_INN_LOW_CORN  = 0x01000000,
		BTP_FLAG          = 0x02000000,
		BTP_BUILDING      = 0x03000000,
		BTP_UPGRADE       = 0x04000000,
		BTP_INN_HIGH_CORN = 0x05000000,
		BTP_SMALL_HEAL    = 0x06000000,
	};
	
	// ask buildings to compute their needs
	for (std::list<Building *>::iterator fillableIt = fillable.begin(); fillableIt != fillable.end(); ++fillableIt)
		(*fillableIt)->computeWishedRessources();
	
	std::vector<MatchResult> matchResults;
	
	// copy fillable so that we remember which buildings were present after matching,
	// in order to be able to call Building::allocateNewlyFillingUnitsToResources()
	std::vector<Building *> fillableCopy;	
	std::copy(fillable.begin(); fillable.end(); std::back_inserter(fillableCopy));
	
	// free workers
	for (std::list<Unit *>::iterator workerIt = freeWorkers.begin(); workerIt != freeWorkers.end(); ++workerIt)
	{
		Unit *unit = *workerIt;
		assert(unit->attachedBuilding == NULL);
		
		// for optimisation, precompute some variables (inside unit)
		unit->numberOfStepsLeftUntilHungry();
		unit->computeMinDistToResources();
		
		// compute matches for fillable
		if (!unit->allResourcesAreTooFar)
			for (std::list<Building *>::iterator fillableIt = fillable.begin(); fillableIt != fillable.end(); ++fillableIt)
			{
				Building *building = *fillableIt;
				Uint32 dist;
				if (!canReachGround(unit, building, &dist, false))
					continue;
			
				// we compute sub-score based on average dist to resource with penality when resource is not accessible
				bool noResourceMatch = true;
				Uint32 resourceScoreModifier = 0;
				Uint32 wishedResourceSum = 0;
				for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
				{
					wishedResourceSum += building->wishedResources[ri];
					if ((building->wishedResources[ri] > 0) && (unit->minDistToResource[ri] >= 0))
					{
						noResourceMatch = false;
						resourceScoreModifier += (unit->minDistToResource[ri] << 6) / building->wishedResources[ri];
					}
					else
					{
						// resource is not accessible
						resourceScoreModifier += ((map->getW() + map->getH()) << 5) / building->wishedResources[ri];
					}
				}
				if (noResourceMatch)
					continue; // there is nothing we can bring that this building needs.
				if (building->unitsWorking.size() >= wishedResourceSum)
					continue; // there are already enough units working on this building
				resourceScoreModifier *= wishedResourceSum;
				
				// compute score
				Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
				if (building->type->foodable)
				{
					// if building is filled at less than 50 % (remember that wishedResources is doubled), increase its priority
					if (building->wishedResources[CORN] > building->type->maxRessource[CORN])
						negativeScore += BTP_INN_LOW_CORN;
					else
						negativeScore += BTP_INN_HIGH_CORN;
				}
				else if (building->type->fillable)
				{
					negativeScore += BTP_BUILDING;
				}
				else
					abort();
				negativeScore += building->matchPriority;
				negativeScore += resourceScoreModifier;
				
				matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_FILLING));
			}
		
		// compute matches for clearing flag
		for (std::list<Building *>::iterator zonableWorkersIt = zonableWorkers.begin(); zonableWorkersIt != zonableWorkers.end(); ++zonableWorkersIt)
		{
			Building *building = *zonableWorkersIt;
			Uint32 dist;
			if (!canReachGround(unit, building, &dist, false))
				continue;
			
			// compute score
			Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
			negativeScore += BTP_FLAG;
			negativeScore += building->matchPriority;
			matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_FLAG));
		}
	}
	
	// free explorers
	for (std::list<Unit *>::iterator explorerIt = freeExplorers.begin(); explorerIt != freeExplorers.end(); ++explorerIt)
	{
		Unit *unit = *explorerIt;
		assert(unit->attachedBuilding == NULL);
		
		// for optimisation, precompute some variables (inside unit)
		unit->numberOfStepsLeftUntilHungry();
		
		// compute matches for exploration flag
		for (std::list<Building *>::iterator zonableExplorersIt = zonableExplorers.begin(); zonableExplorersIt != zonableExplorers.end(); ++zonableExplorersIt)
		{
			Building *building = *zonableExplorersIt;
			Uint32 dist;
			if (!canReachAir(unit, building, &dist, false))
				continue;
			
			// compute score
			Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
			negativeScore += BTP_FLAG;
			negativeScore += building->matchPriority;
			matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_FLAG));
		}
	}
	
	// free warriors
	for (std::list<Unit *>::iterator warriorIt = freeWarriors.begin(); warriorIt != freeWarriors.end(); ++warriorIt)
	{
		Unit *unit = *warriorIt;
		assert(unit->attachedBuilding == NULL);
		
		// for optimisation, precompute some variables (inside unit)
		unit->numberOfStepsLeftUntilHungry();
		
		// compute matches for war flag
		for (std::list<Building *>::iterator zonableWarriorsIt = zonableWarriors.begin(); zonableWarriorsIt != zonableWarriors.end(); ++zonableWarriorsIt)
		{
			Building *building = *zonableWarriorsIt;
			Uint32 dist;
			if (!canReachGround(unit, building, &dist, false))
				continue;
			
			// compute score
			Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
			negativeScore += BTP_FLAG;
			negativeScore += building->matchPriority;
			matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_FLAG));
		}
	}
	
	// all free units
	for (std::list<Unit *>::iterator unitIt = freeUnits.begin(); unitIt != freeUnits.end(); ++unitIt)
	{
		Unit *unit = *unitIt;
		assert(unit->attachedBuilding == NULL);
		
		// compute matches for upgrades
		for (int ability = 0; ability < NB_ABILITY; ability++)
		{
			if (!unit->canLearn[ability])
				continue;
			
			Sint32 currentLevel = unit->level[ability];
			for (std::list<Building *>::iterator upgradeIt = upgrade[ability].begin(); upgradeIt != upgrade[ability].end(); ++upgradeIt)
			{
				Building *building = *upgradeIt;
				Uint32 dist;
				if ((currentLevel < building->type->level) && canReach(unit, building, &dist, false))
				{
					Uint32 negativeScore = (dist << 8); // there is no technical need of this 8 bits shift yet
					negativeScore += BTP_UPGRADE;
					negativeScore += building->matchPriority;
					matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_UPGRADING));
				}
			}
		}
		
		// compute matches for slighlty damaged units
		// If the unit is damaged more than 10%, and has no job, then it can go to an hospital.
		// We did add the 10% triger since the explorer can damage units quite often.
		if (unit->hp + (performance[HP]/10) < unit->performance[HP])
		{
			unit->numberOfStepsLeftUntilDead();
			
			for (std::list<Building *>::iterator canHealUnitIt = canHealUnit.begin(); canHealUnitIt != canHealUnit.end(); ++canHealUnitIt)
			{
				Building *building = *canHealUnitIt;
				Uint32 dist;
				if (!canReach(unit, building, &dist, true))
					continue;
				
				// compute score
				Uint32 negativeScore = ((dist << 8) * unit->hp) / unit->performance[HP];
				negativeScore += BTP_SMALL_HEAL;
				negativeScore += building->matchPriority;
				matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_DAMAGED));
			}
		}
	}
	
	// sort matches
	std::stable_sort(matchResults.begin(), matchResults.end());
	
	// apply matches
	/* Note:
		when a building has the requested number of unit, it will leave the lists,
		but it can still exist in the not yet handeled matchResults. This is not
		a problem because for each match building::unitsWorking and
		building::unitsInside are tested vs building::maxUnitWorking and
		building::maxUnitInside
	*/
	for (std::vector<MatchResult>::iterator matchResultsIt = matchResults.begin(); matchResultsIt != matchResults.end(); ++matchResultsIt)
	{
		const MatchResult &match = *matchResultsIt;
		
		// skip already allocated units
		if (match.unit->attachedBuilding)
			continue;
		
		// skip buildings full of units. list.size() is not O(1), this might be optimised
		if ((match.activity == MatcherConsts::A_FILLING) || (match.activity == MatcherConsts::A_FLAG))
		{
			if ((Sint32)match.building->unitsWorking.size() == match.building->maxUnitWorking)
				continue;
			
			match.building->addUnitWorking(match.unit);
			match.unit->matchSuccess(match.building, match.activity);
		}
		// skip buildings full of units. list.size() is not O(1), this might be optimised
		else if (match.activity == MatcherConsts::A_UPGRADING)
		{
			if ((Sint32)match.building->unitsInside.size() == match.building->maxUnitInside)
				continue;
			
			match.building->addUnitInside(match.unit);
			match.unit->matchSuccess(match.building, match.activity);
		}
		else
			abort();
	}
	
	// request buildings to allocate newly matched units
	for (std::vector<Building *>::iterator fillableIt = fillableCopy.begin(); fillableIt != fillableCopy.end(); ++fillableIt)
		(*fillableIt)->allocateNewlyFillingUnitsToResources();
}

//! Match all hungry units vs all available inns to globally allocate at once
void Matcher::matchHungry()
{
	Game *game = team->game;
	std::vector<MatchResult> matchResults;
	
	for (std::list<Unit *>::iterator unitIt = hungryUnits.begin(); unitIt != hungryUnits.end(); ++unitIt)
	{
		Unit *unit = *unitIt;
		assert(unit->attachedBuilding == NULL);
		
		// for optimisation, precompute some variables (inside unit)
		unit->numberOfStepsLeftUntilDead();
		
		for (int teamId = 0; teamId < game->session.numberOfTeam; teamId++)
		{
			Team *team = game->teams[teamId];
			
			if ((team->sharedVisionFood & this->team->me) == 0)
				continue; // This team doesn't share its inns with me.
			if ((team->allies & this->team->me) != 0)
				continue; // This team is allied, then it doesn't steal our units.
			
			// We prefer to go to our own team (but fruits are stronger)
			bool enemyTeam = (team->enemies & this->team->me) != 0;
			Uint32 teamScoreModifier;
			if (enemyTeam)
				teamScoreModifier = 1 << 16;
			else
				teamScoreModifier = 0 << 16;
			
			for (std::list<Building *>::iterator canFeedUnitIt = canFeedUnit.begin(); canFeedUnitIt != canFeedUnit.end(); ++canFeedUnitIt)
			{
				Building *building = *canFeedUnitIt;
				Uint32 dist;
				if (!canReach(unit, building, &dist, true))
					continue;
				
				// compute score
				Uint32 happynessScoreModifier = building->availableHappynessLevel(enemyTeam) << 17;
				Uint32 negativeScore = dist + teamScoreModifier + happynessScoreModifier;
				
				matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_HUNGRY));
			}
		}
	}
	
	// sort matches
	std::stable_sort(matchResults.begin(), matchResults.end());
	
	// apply matches
	/* Note:
		when a building has the maximum number of unit inside, it will leave
		the lists, but it can still exist in the not yet handeled matchResults.
		This is not a problem because for each match building::unitsInside are
		tested vs building::maxUnitInside.
	*/
	for (std::vector<MatchResult>::iterator matchResultsIt = matchResults.begin(); matchResultsIt != matchResults.end(); ++matchResultsIt)
	{
		const MatchResult &match = *matchResultsIt;
		
		// skip already allocated units
		if (match.unit->attachedBuilding)
			continue;
		
		match.building->addUnitInside(match.unit);
		match.unit->matchSuccess(match.building, match.activity);
	}
}

//! Match all damaged units vs all available hospitals to globally allocate at once
void Matcher::matchDamaged()
{
	std::vector<MatchResult> matchResults;
	
	for (std::list<Unit *>::iterator unitIt = damagedUnits.begin(); unitIt != damagedUnits.end(); ++unitIt)
	{
		Unit *unit = *unitIt;
		assert(unit->attachedBuilding == NULL);
		
		// for optimisation, precompute some variables (inside unit)
		unit->numberOfStepsLeftUntilDead();
		
		for (std::list<Building *>::iterator canHealUnitIt = canHealUnit.begin(); canHealUnitIt != canHealUnit.end(); ++canHealUnitIt)
		{
			Building *building = *canHealUnitIt;
			Uint32 dist;
			if (!canReach(unit, building, &dist, true))
				continue;
			
			// compute score
			Uint32 negativeScore = dist;
			negativeScore += building->matchPriority;
			matchResults.push_back(MatchResult(negativeScore, unit, building, MatcherConsts::A_DAMAGED));
		}
	}
	
	// sort matches
	std::stable_sort(matchResults.begin(), matchResults.end());
	
	// apply matches
	/* Note:
		when a building has the maximum number of unit inside, it will leave
		the lists, but it can still exist in the not yet handeled matchResults.
		This is not a problem because for each match building::unitsInside are
		tested vs building::maxUnitInside.
	*/
	for (std::vector<MatchResult>::iterator matchResultsIt = matchResults.begin(); matchResultsIt != matchResults.end(); ++matchResultsIt)
	{
		const MatchResult &match = *matchResultsIt;
		
		// skip already matched units
		if (match.unit->attachedBuilding)
			continue;
		
		match.building->addUnitInside(match.unit);
		match.unit->matchSuccess(match.building, match.activity);
	}
}

//! Match all free units vs all buildings to globally allocate units at once.
void Matcher::match()
{
	matchFree();
	matchHungry();
	matchDamaged();
}

//! Insert a unit to the free units list. Caller is trusted, unit state is not rechecked.
void Matcher::addFreeUnit(Unit *unit)
{
	switch (unit->typeNum)
	{
		case WORKER:
		freeWorkers.push_back(unit);
		break;
		
		case EXPLORER:
		freeExplorers.push_back(unit);
		break;
		
		case WARRIOR:
		freeWarriors.push_back(unit);
		break;
		
		default:
		abort();
		break;
	}
	freeUnits.push_back(unit);
}

//! Insert a unit to the hungry units list. Caller is trusted, unit state is not rechecked.
void Matcher::addHungryUnit(Unit *unit)
{
	hungryUnits.push_back(unit);
}

//! Insert a unit to the damaged units list. Caller is trusted, unit state is not rechecked.
void Matcher::addDamagedUnit(Unit *unit)
{
	damagedUnits.push_back(unit);
}

//! Remove a unit from some lists. The unit matcher state is checked to see which list the unit should be remvoed from.
void Matcher::removeUnit(Unit *unit)
{
	switch (unit->matcherState)
	{
		case S_NONE:
		break;
		
		case S_HUNGRY:
		hungryUnits.remove(unit);
		break;
		
		case S_DAMAGED:
		damagedUnits.remove(unit);
		break;
		
		case S_FREE:
		{
			switch (unit->typeNum)
			{
				case WORKER:
				freeWorkers.remove(unit);
				break;
				
				case EXPLORER:
				freeExplorers.remove(unit);
				break;
				
				case WARRIOR:
				freeWarriors.remove(unit);
				break;
				
				default:
				abort();
				break;
			}
			freeUnits.remove(unit);
		}
		break;
		
		default:
		abort();
		break;
	}
}


