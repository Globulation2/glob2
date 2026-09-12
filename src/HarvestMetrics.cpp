// SPDX-License-Identifier: GPL-3.0-or-later

#include "HarvestMetrics.h"

#include "Game.h"
#include "GlobalContainer.h"
#include "Ressource.h"
#include "RessourceType.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace
{
	const char *const kResourceNames[MAX_RESOURCES] = {
		"WOOD", "CORN", "PAPYRUS", "STONE", "ALGA", "CHERRY", "ORANGE", "PRUNE"
	};

	struct Counters
	{
		Uint64 commits = 0;     // fetches started
		Uint64 harvests = 0;    // resources taken from the map
		Uint64 deliveries = 0;  // resources put into a building
		Uint64 steps = 0;       // steps walked towards a resource
		Uint64 reversals = 0;   // steps turning more than 90 degrees from the previous one
		Uint64 retargets = 0;   // steps after which the gradient destination moved
		Uint64 lost = 0;        // no gradient step, the job kept
		Uint64 ghosts = 0;      // of lost: standing where the gradient still sees the resource
		Uint64 abandoned = 0;   // no gradient step, the job given up
		Uint64 trips = 0;       // commits that reached a harvest
		Uint64 tripTicks = 0;   // ticks from commit to harvest, summed over trips
		Uint64 goingTicks = 0;  // unit-ticks spent heading to a resource
		Uint64 doomedTicks = 0; // unit-ticks heading to a tile already outnumbered
		Uint32 maxCrowd = 0;    // most units ever heading to one tile at once
	};

	struct UnitTrack
	{
		Uint32 commitTick = 0;
		Sint8 lastDx = 0;
		Sint8 lastDy = 0;
		bool committed = false;
	};

	Counters counters[Team::MAX_COUNT][MAX_RESOURCES];

	// Whether idle workers coincide with wheat being wanted, and with as many
	// wheat fetchers out as there is wheat on the map (swim class 0 supply).
	struct IdleCounters
	{
		Uint64 ticks = 0;
		Uint64 idleWorkerTicks = 0;      // worker-ticks free and without a job
		Uint64 demandTicks = 0;          // ticks some building wants wheat
		Uint64 idleWhenDemandTicks = 0;  // idle worker-ticks while wheat is wanted
		Uint64 boundTicks = 0;           // ticks wheat is wanted and fetchers >= wheat supply
		Uint64 idleWhenBoundTicks = 0;   // idle worker-ticks during those ticks
		Uint64 supplySum = 0;            // wheat on the map, summed over ticks
		Uint64 fetcherSum = 0;           // wheat fetchers out, summed over ticks
		Uint64 wantedSum = 0;            // wheat wanted by buildings, summed over ticks
	};
	IdleCounters idleCounters[Team::MAX_COUNT];
	UnitTrack tracks[Team::MAX_COUNT][Unit::MAX_COUNT];
	std::unordered_map<Uint64, Uint32> crowd;

	const int reportEvery = [] {
		const char *every = std::getenv("GLOB2_HARVEST_METRICS_EVERY");
		return every ? std::atoi(every) : 0;
	}();

	bool trackable(const Unit *unit, int resource)
	{
		return resource >= 0 && resource < MAX_RESOURCES
			&& unit->owner->teamNumber >= 0 && unit->owner->teamNumber < Team::MAX_COUNT;
	}

	UnitTrack &trackOf(const Unit *unit)
	{
		return tracks[unit->owner->teamNumber][Unit::GIDtoID(unit->gid)];
	}

	// How many more units a tile can still serve for this resource.
	Uint32 capacity(const Map &map, int x, int y, int resource)
	{
		const Resource &r = map.getTile(x, y).resource;
		if (r.type != resource || r.amount == 0)
			return 0;
		const ResourceType *type = globalContainer->resourcesTypes.get(resource);
		return type->granular ? r.amount : 1;
	}

	bool isFetchingFromMap(const Unit *unit)
	{
		return unit->activity == Unit::ACT_FILLING
			&& unit->displacement == Unit::DIS_GOING_TO_RESOURCE
			&& unit->ownExchangeBuilding == NULL
			&& unit->destinationPurpose >= 0 && unit->destinationPurpose < MAX_RESOURCES;
	}
}

namespace HarvestMetrics
{
	const bool enabled = std::getenv("GLOB2_HARVEST_METRICS") != NULL;

	void onCommit(const Unit *unit)
	{
		if (!enabled || !trackable(unit, unit->destinationPurpose))
			return;
		counters[unit->owner->teamNumber][unit->destinationPurpose].commits++;
		UnitTrack &track = trackOf(unit);
		track.commitTick = unit->owner->game->stepCounter;
		track.lastDx = track.lastDy = 0;
		track.committed = true;
	}

	void onStep(const Unit *unit, bool retargeted)
	{
		if (!enabled || !trackable(unit, unit->destinationPurpose))
			return;
		Counters &c = counters[unit->owner->teamNumber][unit->destinationPurpose];
		if (retargeted)
			c.retargets++;
		if (unit->dx == 0 && unit->dy == 0)
			return;
		c.steps++;
		UnitTrack &track = trackOf(unit);
		if (track.lastDx * unit->dx + track.lastDy * unit->dy < 0)
			c.reversals++;
		track.lastDx = unit->dx;
		track.lastDy = unit->dy;
	}

	void onLost(const Unit *unit, bool abandoned, bool ghost)
	{
		if (!enabled || !trackable(unit, unit->destinationPurpose))
			return;
		Counters &c = counters[unit->owner->teamNumber][unit->destinationPurpose];
		if (abandoned)
			c.abandoned++;
		else
		{
			c.lost++;
			if (ghost)
				c.ghosts++;
		}
	}

	void onHarvest(const Unit *unit, int resource)
	{
		if (!enabled || !trackable(unit, resource))
			return;
		Counters &c = counters[unit->owner->teamNumber][resource];
		c.harvests++;
		UnitTrack &track = trackOf(unit);
		if (track.committed)
		{
			c.trips++;
			c.tripTicks += unit->owner->game->stepCounter - track.commitTick;
			track.committed = false;
		}
	}

	void onDeliver(const Unit *unit, int resource)
	{
		if (!enabled || !trackable(unit, resource))
			return;
		counters[unit->owner->teamNumber][resource].deliveries++;
	}

	void sample(const Game &game)
	{
		if (!enabled)
			return;
		const Map &map = game.map;
		for (int t = 0; t < game.teamsCount() && t < Team::MAX_COUNT; t++)
		{
			const Team *team = game.teams[t];
			if (!team)
				continue;
			crowd.clear();
			Uint32 idle = 0, cornFetchers = 0;
			for (int i = 0; i < Unit::MAX_COUNT; i++)
			{
				const Unit *unit = team->myUnits[i];
				if (unit && unit->typeNum == WORKER && unit->activity == Unit::ACT_RANDOM && unit->medical == Unit::MED_FREE)
					idle++;
				if (!unit || !isFetchingFromMap(unit))
					continue;
				if (unit->destinationPurpose == CORN)
					cornFetchers++;
				Uint64 key = (Uint64(unit->destinationPurpose) << 32) | map.coordToIndex(unit->targetX, unit->targetY);
				crowd[key]++;
			}
			Uint32 wanted = 0;
			for (int b = 0; b < Building::MAX_COUNT; b++)
				if (const Building *building = team->myBuildings[b])
				{
					int need = building->type->maxResource[CORN] - building->resources[CORN] + 1 - building->type->multiplierResource[CORN];
					if (need > 0)
						wanted += need;
				}
			const Uint32 supply = map.getResourceSupply(t, CORN, 0);
			IdleCounters &ic = idleCounters[t];
			ic.ticks++;
			ic.idleWorkerTicks += idle;
			ic.supplySum += supply;
			ic.fetcherSum += cornFetchers;
			ic.wantedSum += wanted;
			if (wanted > 0)
			{
				ic.demandTicks++;
				ic.idleWhenDemandTicks += idle;
				if (cornFetchers >= supply)
				{
					ic.boundTicks++;
					ic.idleWhenBoundTicks += idle;
				}
			}
			for (const auto &entry : crowd)
			{
				int resource = int(entry.first >> 32);
				size_t index = size_t(entry.first & 0xFFFFFFFF);
				int x = int(index & map.wMask);
				int y = int(index >> map.wDec);
				Uint32 going = entry.second;
				Uint32 room = capacity(map, x, y, resource);
				Counters &c = counters[t][resource];
				c.goingTicks += going;
				if (going > room)
					c.doomedTicks += going - room;
				if (going > c.maxCrowd)
					c.maxCrowd = going;
			}
		}
		if (reportEvery > 0 && game.stepCounter > 0 && game.stepCounter % reportEvery == 0)
			report(game);
	}

	void report(const Game &game)
	{
		if (!enabled)
			return;
		for (int t = 0; t < game.teamsCount() && t < Team::MAX_COUNT; t++)
			for (int r = 0; r < MAX_RESOURCES; r++)
			{
				const Counters &c = counters[t][r];
				if (c.commits == 0 && c.harvests == 0 && c.deliveries == 0)
					continue;
				std::printf("GLOB2_HARVEST tick=%u team=%d res=%s commits=%llu harvests=%llu deliveries=%llu"
					" steps=%llu reversals=%llu retargets=%llu lost=%llu ghosts=%llu abandoned=%llu trips=%llu tripTicks=%llu"
					" goingTicks=%llu doomedTicks=%llu maxCrowd=%u\n",
					game.stepCounter, t, kResourceNames[r],
					(unsigned long long)c.commits, (unsigned long long)c.harvests, (unsigned long long)c.deliveries,
					(unsigned long long)c.steps, (unsigned long long)c.reversals, (unsigned long long)c.retargets,
					(unsigned long long)c.lost, (unsigned long long)c.ghosts,
					(unsigned long long)c.abandoned, (unsigned long long)c.trips, (unsigned long long)c.tripTicks,
					(unsigned long long)c.goingTicks, (unsigned long long)c.doomedTicks, c.maxCrowd);
			}
		for (int t = 0; t < game.teamsCount() && t < Team::MAX_COUNT; t++)
		{
			const IdleCounters &ic = idleCounters[t];
			std::printf("GLOB2_IDLE tick=%u team=%d ticks=%llu idleWorkerTicks=%llu demandTicks=%llu idleWhenDemandTicks=%llu"
				" boundTicks=%llu idleWhenBoundTicks=%llu supplySum=%llu fetcherSum=%llu wantedSum=%llu\n",
				game.stepCounter, t, (unsigned long long)ic.ticks, (unsigned long long)ic.idleWorkerTicks,
				(unsigned long long)ic.demandTicks, (unsigned long long)ic.idleWhenDemandTicks,
				(unsigned long long)ic.boundTicks, (unsigned long long)ic.idleWhenBoundTicks,
				(unsigned long long)ic.supplySum, (unsigned long long)ic.fetcherSum, (unsigned long long)ic.wantedSum);
		}
		std::fflush(stdout);
	}

	unsigned long long deliveries(int teamNumber, int resource)
	{
		if (teamNumber < 0 || teamNumber >= Team::MAX_COUNT || resource < 0 || resource >= MAX_RESOURCES)
			return 0;
		return counters[teamNumber][resource].deliveries;
	}
}
