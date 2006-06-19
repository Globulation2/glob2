#include "Matcher.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include "Map.h"
#include <vector>

//! A result of the matching, ready for sorting
class MatchResult
{
public:
	Uint32 score;
	Unit *unit;
	Building *building;
	enum Type
	{
		MT_WORKING,
		MT_INSIDE,
	} type;
	
	bool operator < (const MatchResult &other) const
	{
		return score < other.score;
	}
	
	MatchResult(Uint32 score, Unit *unit, Building *building) :
		score(score),
		unit(unit),
		building(building)
	{
	}
};

//! Return true if unit can reach building with its hungryness and write dist. Return false otherwise
bool Matcher::canReach(Unit *unit, Building *building, Uint32 *dist)
{
	// can we reach the building ? (fast test to avoid gradiant call)
	int approxDist2 = map->warpDistSquare(unit->posX, unit->posY, building->posX, building->posY);
	int maxDist = unit->stepsLeftUntilHungry;
	int maxDist2 = maxDist * maxDist;
	if (approxDist2 >= maxDist2)
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

//! Constructor, get direct access pointer to map from team
Matcher::Matcher(Team *team) :
	team(team)
{
	map = team->map;
}

//! Match all free workers vs all buildings to globally allocate workers at once
void Matcher::matchWorkers()
{
	enum BuildingTypePriorities
	{
		BTP_INN      = 0x01000000,
		BTP_FLAG     = 0x02000000,
		BTP_BUILDING = 0x03000000,
		BTP_UPGRADE  = 0x04000000,
	};
	
	// ask buildings to compute their needs
	for (std::list<Building *>::iterator fillableIt = fillable.begin(); fillableIt != fillable.end(); ++fillableIt)
		(*fillableIt)->computeWishedRessources();
	
	std::vector<MatchResult> matchResults;
	
	for (std::list<Unit *>::iterator workerIt = workers.begin(); workerIt != workers.end(); ++workerIt)
	{
		Unit *unit = *workerIt;
		unit->numberOfStepsLeftUntilHungry();
		unit->computeMinDistToResources();
		
		// compute matches for foodable
		if (unit->minDistToResource[CORN] < unit->stepsLeftUntilHungry)
			for (std::list<Building *>::iterator foodableIt = foodable.begin(); foodableIt != foodable.end(); ++foodableIt)
			{
				Building *building = *foodableIt;
				Uint32 dist;
				if (canReach(unit, building, &dist))
				{
					// compute score
					Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
					negativeScore += BTP_INN;
					negativeScore += building->matchPriority;
					
					// push result
					matchResults.push_back(MatchResult(negativeScore, unit, building));
				}
			}
		
		// compute matches for fillable
		if (!unit->allResourcesAreTooFar)
			for (std::list<Building *>::iterator fillableIt = fillable.begin(); fillableIt != fillable.end(); ++fillableIt)
			{
				Building *building = *fillableIt;
				Uint32 dist;
				if (canReach(unit, building, &dist))
				{
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
							resourceScoreModifier += ((map->getW() + map->getH()) << 5) / building->wishedResources[ri];
						}
					}
					if (noResourceMatch)
						continue; // there is nothing we can bring that this building needs.
					if (building->unitsWorking.size() >= 2 * wishedResourceSum)
						continue; // there are already enough units working on this building
					resourceScoreModifier *= wishedResourceSum;
					
					// compute score
					Uint32 negativeScore = (dist << 8) / (building->maxUnitWorking - building->unitsWorking.size());
					negativeScore += BTP_BUILDING;
					negativeScore += building->matchPriority;
					negativeScore += resourceScoreModifier;
					
					// push result
					matchResults.push_back(MatchResult(negativeScore, unit, building));
				}
			}
		
		// compute matches for flags
		// TODO zzz
	}
}

//! Match all free units vs all buildings to globally allocate units at once
void Matcher::match()
{
	matchWorkers();
}
