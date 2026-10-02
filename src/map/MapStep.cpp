// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "kernel/GradientPropagation.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"
#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#include <algorithm>
#include <tuple>


// growResources, syncStep, fog of war, discovery, explored area

void Map::growResources(void)
{
	if (game->gameHeader.isResourceGrowthDisabled())
		return;
	rebuildGrowthCoverage();
	// Custom-game "scarce resources" rule: an extra grow/extend probability
	// divisor, stacking with (not replacing) corn's own CORN_GROWTH_DIVISOR
	// roll below, applied uniformly to every resource type.
	static constexpr int scarcityDivisor[] = {1, 2, 4, 8};
	const int scarcity = scarcityDivisor[game->gameHeader.getResourceScarcityLevel()];

	int dy=(syncRand()&0x3);
	for (int y=dy; y<h; y+=4)
	{
		for (int x=(syncRand()&0xF); x<w; x+=(syncRand()&0x1F))
		{
			const Resource &r = getResource(x, y);
			if (r.type!=NO_RES_TYPE)
			{
				// we look around to see if there is any water :
				// TODO: uses UnderMap.
				int dwax=(syncRand()&0xF)-(syncRand()&0xF);
				int dway=(syncRand()&0xF)-(syncRand()&0xF);
				int wax1=x+dwax;
				int way1=y+dway;

				int wax2=x+dway*2;
				int way2=y+dwax*2;

				int wax3=x-dwax;
				int way3=y-dway;

				// alga, wood and wheat are limited by near underground. Others are not.
				bool expand=true;
				if (r.type == ALGA)
					expand = isWater(wax1, way1) && isSand(wax2, way2);
				else if (r.type == WOOD)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));
				else if (r.type == WHEAT)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));

				// Growth rate of wheat is 1/WHEAT_GROWTH_DIVISOR
				if(r.type == WHEAT && expand)
					if(syncRand() % WHEAT_GROWTH_DIVISOR != 0)
						expand = false;

				if (expand && (scarcity==1 || syncRand()%scarcity==0))
				{
					if (r.amount<=(syncRand()&7))
					{
						// we grow resource:
						if(canResourcesGrow(x, y))
						{
							const int beforeType = r.type, beforeAmount = r.amount;
							incResource(x, y, beforeType, r.variety);
							recordNaturalGrowth(x,y,beforeType,beforeType,beforeAmount);
						}
					}
					else if (globalContainer->resourcesTypes.get(r.type)->expendable)
					{
						// we extend resource:
						int dx, dy;
						Unit::dxDyFromDirection((syncRand()&7), &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if(canResourcesGrow(nx, ny))
						{
							const Resource &before = getResource(nx, ny);
							const int beforeType = before.type, beforeAmount = before.amount;
							incResource(nx, ny, r.type, r.variety);
							recordNaturalGrowth(nx,ny,r.type,beforeType,beforeAmount);
						}
					}
				}
			}
		}
	}
}

void Map::rebuildGrowthCoverage()
{
	static_assert(GROWTH_COVERAGE_BANDS * Team::MAX_COUNT <= sizeof(Uint64) * CHAR_BIT,
		"Growth distance masks must fit one tile word (currently at most 21 teams)");
	// Old saves start the new diagnostic interval at the loaded tick.
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		Team *team = game->teams[t];
		if (!team) continue;
		auto &stats = team->stats;
		if (stats.coverageBuildings.empty() && stats.coverageBuildingTick == 0 &&
			stats.extendedCoverageStartTick > 0 && game->stepCounter >= stats.extendedCoverageStartTick)
		{
			for (int i = 0; i < Building::MAX_COUNT; ++i)
			{
				Building *b = team->myBuildings[i];
				if (b && !b->type->isVirtual && b->buildingState != Building::DEAD)
					stats.coverageBuildings.push_back({b->posX,b->posY,b->type->width,b->type->height});
			}
			stats.coverageBuildingTick = game->stepCounter;
			std::sort(stats.coverageBuildings.begin(), stats.coverageBuildings.end(),
				[](const TeamStats::CoverageBuilding &a, const TeamStats::CoverageBuilding &b) {
					return std::tie(a.x,a.y,a.width,a.height) < std::tie(b.x,b.y,b.width,b.height);
				});
			++stats.coverageBuildingGeneration;
		}
	}
	const int teams = game->mapHeader.getNumberOfTeams();
	const size_t tileCount = size_t(w) * h;
	if (!growthCoverageValid || growthCoverage.size() != tileCount ||
		growthCoverageCounts[0].size() != tileCount * teams)
	{
		growthCoverage.assign(tileCount, 0);
		for (auto &counts : growthCoverageCounts) counts.assign(tileCount * teams, 0);
		for (auto &buildings : growthCoverageBuildings) buildings.clear();
		std::fill(std::begin(growthCoverageGeneration), std::end(growthCoverageGeneration), Uint32(-1));
		growthCoverageValid = true;
	}
	const auto &radius = GROWTH_COVERAGE_RADII;
	constexpr int outerBand = GROWTH_COVERAGE_BANDS - 1;
	// The count planes are team-major. A changed building touches only its
	// Chebyshev footprint; growth events still read just three contiguous masks.
	const auto paint = [&](int t, const TeamStats::CoverageBuilding &b, int delta)
	{
		const size_t plane = size_t(t) * tileCount;
		for (int y = b.y - radius[outerBand]; y < b.y + b.height + radius[outerBand]; ++y)
		{
			const int dy = std::max({b.y - y, 0, y - (b.y + b.height - 1)});
			const size_t row = size_t(y & hMask) * w;
			for (int x = b.x - radius[outerBand]; x < b.x + b.width + radius[outerBand]; ++x)
			{
				const int dx = std::max({b.x - x, 0, x - (b.x + b.width - 1)});
				const int d = std::max(dx,dy);
				const size_t index = row + (x & wMask);
				for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
					if (d <= radius[band])
				{
					const Uint64 bit = Uint64(1) << (band * Team::MAX_COUNT + t);
					Uint16 &count = growthCoverageCounts[band][plane + index];
					if (delta > 0)
					{
						if (count++ == 0) growthCoverage[index] |= bit;
					}
					else if (--count == 0) growthCoverage[index] &= ~bit;
				}
			}
		}
	};
	const auto less = [](const TeamStats::CoverageBuilding &a, const TeamStats::CoverageBuilding &b) {
		return std::tie(a.x,a.y,a.width,a.height) < std::tie(b.x,b.y,b.width,b.height);
	};
	for (int t = 0; t < teams; ++t)
	{
		Team *team = game->teams[t];
		if (!team) continue;
		auto &stats = team->stats;
		if (growthCoverageGeneration[t] == stats.coverageBuildingGeneration) continue;
		auto &old = growthCoverageBuildings[t];
		const auto &now = stats.coverageBuildings;
		// Remove first: on a 16x16 torus a footprint can paint a tile 36
		// times. At most 1024 anchors => 36864, fitting Uint16. Interleaved
		// additions could temporarily combine both complete anchor sets.
		for (int pass = 0; pass < 2; ++pass)
		{
			size_t i = 0, j = 0;
			while (i < old.size() || j < now.size())
			{
				if (j == now.size() || (i < old.size() && less(old[i],now[j])))
				{ if (pass == 0) paint(t,old[i],-1); ++i; }
				else if (i == old.size() || less(now[j],old[i]))
				{ if (pass == 1) paint(t,now[j],1); ++j; }
				else { ++i; ++j; }
			}
		}
		old = now;
		growthCoverageGeneration[t] = stats.coverageBuildingGeneration;
	}
}

void Map::recordNaturalGrowth(int x, int y, int resourceType, int oldType, int oldAmount)
{
	const Resource &after = getResource(x,y);
	if (resourceType < 0 || resourceType >= MAX_NB_RESOURCES) return;
	const int tiles = oldType == NO_RES_TYPE && after.type == resourceType;
	const int delta = after.amount - (tiles ? 0 : oldAmount);
	if (!tiles && !delta) return;
	const size_t index = size_t(y & hMask) * w + (x & wMask);
	const Uint64 packed = growthCoverage[index];
	Uint32 masks[GROWTH_COVERAGE_BANDS];
	for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
		masks[band] = Uint32(packed >> (band * Team::MAX_COUNT));
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		Team *team = game->teams[t];
		if (!team) continue;
		auto &m = team->stats.measurements;
		m.growthGlobal[0][resourceType] += tiles;
		m.growthGlobal[1][resourceType] += std::max(0,delta);
		m.growthGlobal[2][resourceType] += std::max(0,-delta);
		for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
			if (masks[band] & (Uint32(1) << t))
			{
				m.growthTiles[band][resourceType] += tiles;
				m.growthAmount[band][resourceType] += std::max(0,delta);
				m.growthReduction[band][resourceType] += std::max(0,-delta);
			}
	}
}


bool Map::gradientPipelineEnabled() const { return gradientRuntime->pipeline.enabled(); }

Map::GradientPipelineStatus Map::gradientPipelineStatus() const
{
	const auto &pipeline = gradientRuntime->pipeline;
	const auto &metrics = pipeline.metrics;
	return {pipeline.enabled(), pipeline.workerCount(), pipeline.delayTicks(),
		pipeline.pendingCount(), metrics.jobs, metrics.published, metrics.discarded,
		metrics.maxPending, metrics.waitNs, pipeline.activeElapsedNs()};
}

void Map::advanceGradientPipeline() { gradientRuntime->pipeline.advance(); }
void Map::finishGradientPipeline() { gradientRuntime->pipeline.finish(); }
void Map::setGradientWorkerCount(unsigned workers) { gradientRuntime->pipeline.setWorkerCount(workers); }

void Map::configureGradientPipeline(unsigned workers, unsigned delay)
{
	if (workers>16 || delay<1 || delay>16) throw std::invalid_argument("Invalid gradient pipeline configuration");
#ifdef YOG_SERVER_ONLY
	workers=0;
#endif
	gradientRuntime->pipeline.configure(workers, delay, size, [this](GradientPipeline::Job &job, GradientWorkspace &scratch) {
#ifndef YOG_SERVER_ONLY
		const gradient_kernel::GradientGeometry geometry{size, wMask, hMask, wDec};
		if (!job.water)
			gradient_kernel::propagateField(job.data.get(), job.swim, GRADIENT_COST_LIMIT,
				geometry, scratch, [this](size_t i) { return isWater(static_cast<unsigned>(i)); });
		else
		{
			const auto *water = job.water->data();
			gradient_kernel::propagateField(job.data.get(), job.swim, GRADIENT_COST_LIMIT,
				geometry, scratch, [water](size_t i) { return water[i] != 0; });
		}
#else
		throw std::logic_error("The server does not simulate gradients");
#endif
	});
}

#ifndef YOG_SERVER_ONLY
void Map::syncStep(Uint32 stepCounter)
{
	PERF_SCOPE_TIME(Map);
	growResources();
	for (int i=0; i<sizeSector; i++)
		sectors[i].step();
	game->animations->step();

	if (stepCounter & 1)
	{
		// Historical 64-tick exploration cycle: spare schedule slots are idle.
		constexpr int explorationScheduleSlots = 32;
		static_assert(Team::MAX_COUNT <= explorationScheduleSlots);
		int team = (stepCounter >> 1) % explorationScheduleSlots;
		if (team < game->mapHeader.getNumberOfTeams())
			updateExploredArea(team);
	}
	
	// Bound escape-field staleness independently of forbidden orders.
	// At most one allocated field per eight map ticks; no extra saved state.
	// Normal cycle: 8 * teams * SWIM_CLASS_COUNT ticks; counter wrap can
	// extend one interval to less than two cycles.
	const int escapeSlots = game->mapHeader.getNumberOfTeams() * SWIM_CLASS_COUNT;
	if (escapeSlots && (stepCounter & 7) == 0)
	{
		const int slot = (stepCounter >> 3) % escapeSlots;
		const int escapeTeam = slot / SWIM_CLASS_COUNT;
		const int escapeSwim = slot % SWIM_CLASS_COUNT;
		const Uint16* field = forbiddenGradient[escapeTeam][escapeSwim];
		if (field)
		{
			// Class 0 and SWIM_CLASS_EVEN have fixed edge costs. Their propagated fields retain the
			// obstacle/free-goal/forbidden-interior partition used to seed it.
			// Matching partitions imply matching seeds, including border seeds.
			// MapGradientField.cpp statically asserts EVEN water cost equals land.
			// Other swimmers can change costs without changing these markers, so
			// conservatively refresh their scheduled fields unconditionally.
			bool changed = escapeSwim != 0 && escapeSwim != SWIM_CLASS_EVEN;
			if (!changed)
			{
				const Uint32 teamMask = Team::teamNumberToMask(escapeTeam);
				for (size_t i = 0; i < size; ++i)
				{
					const Tile& tile = tiles[i];
					const bool blocked = tile.resource.type != NO_RES_TYPE
						|| tile.building != NOGBID || (escapeSwim == 0 && isWater((unsigned)i))
						|| immobileUnits[i] != IMMOBILE_UNIT_NONE;
					const bool goal = !blocked && !(tile.forbidden & teamMask);
					if ((field[i] == GRADIENT_FORBIDDEN) != blocked
						|| (field[i] == GRADIENT_AT_GOAL) != goal)
					{
						changed = true;
						break;
					}
				}
			}
			if (changed)
			{
				updateForbiddenGradient(escapeTeam, escapeSwim);
			}
		}
	}

	auto dispatch = [&](Uint16 **slot, int swim, auto seed) {
		gradientRuntime->pipeline.submit(slot, swim, [&](GradientPipeline::Job &job) {
			seed(job.data.get());
			if (swim != 0 && swim != SWIM_CLASS_EVEN) {
				job.water = frozenWaterSnapshot();
			} else job.water.reset();
		});
	};
	// We only update one gradient per step, round robin over the gradients in use.
	// Fields are allocated lazily: the second pass runs on freshly reset flags,
	// so finding nothing there means no gradient exists yet and there is nothing to do.
	for (int pass=0; pass<2; pass++)
	{
		int numberOfTeam=game->mapHeader.getNumberOfTeams();
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESOURCES; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					if (resourcesGradient[t][r][s] && !gradientUpdated[t][r][s])
					{
						if (gradientRuntime->pipeline.enabled()) dispatch(&resourcesGradient[t][r][s], s, [&](Uint16 *field) { seedResourcesGradient(t, r, s, field); });
						else updateResourcesGradient(t, r, s);
						gradientUpdated[t][r][s]=true;
						return;
					}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(guardAreasGradient[t][s] && !guardGradientUpdated[t][s])
				{
					if (gradientRuntime->pipeline.enabled()) dispatch(&guardAreasGradient[t][s], s, [&](Uint16 *field) { seedGuardAreasGradient(t, s, field); });
					else updateGuardAreasGradient(t, s);
					guardGradientUpdated[t][s]=true;
					return;
				}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(clearAreasGradient[t][s] && !clearGradientUpdated[t][s])
				{
					if (gradientRuntime->pipeline.enabled()) dispatch(&clearAreasGradient[t][s], s, [&](Uint16 *field) { seedClearAreasGradient(t, s, field); });
					else updateClearAreasGradient(t, s);
					clearGradientUpdated[t][s]=true;
					return;
				}
				

		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESOURCES; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					gradientUpdated[t][r][s]=false;
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
			{
				guardGradientUpdated[t][s]=false;
				clearGradientUpdated[t][s]=false;
			}
	}
}
#endif  // !YOG_SERVER_ONLY

void Map::switchFogOfWar(void)
{
	PERF_SCOPE_TIME(Fog);
	memset(fogOfWar, 0, size*sizeof(Uint32));
	if (fogOfWar == &fogOfWarA[0])
		fogOfWar = &fogOfWarB[0];
	else
		fogOfWar = &fogOfWarA[0];
}

void Map::setMapDiscovered(int x, int y, Uint32 sharedVision)
{
	size_t index = coordToIndex(x, y);
	mapDiscovered[index] |= sharedVision;
	fogOfWarA[index] |= sharedVision;
	fogOfWarB[index] |= sharedVision;
}

void Map::setMapDiscovered(int x, int y, int w, int h,  Uint32 sharedVision)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setMapDiscovered(dx, dy, sharedVision);
}

void Map::setMapBuildingsDiscovered(int x, int y, Uint32 sharedVision, Team *teams[Team::MAX_COUNT])
{
	Uint16 bgid = tiles[coordToIndex(x, y)].building;
	if (bgid != NOGBID)
	{
		int id = Building::GIDtoID(bgid);
		int team = Building::GIDtoTeam(bgid);
		assert(id>=0);
		assert(id<Building::MAX_COUNT);
		assert(team>=0);
		assert(team<Team::MAX_COUNT);
		teams[team]->myBuildings[id]->seenByMask|=sharedVision;
	}
}

void Map::setMapBuildingsDiscovered(int x, int y, int w, int h, Uint32 sharedVision, Team *teams[Team::MAX_COUNT])
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setMapBuildingsDiscovered(dx, dy, sharedVision, teams);
}

void Map::setMapExploredByUnit(int x, int y, int w, int h, int team)
{
	for (int dx = x; dx < x + w; dx++)
		for (int dy = y; dy < y + h; dy++)
			exploredArea[team][coordToIndex(dx, dy)] = EXPLORED_FRESH;
}

void Map::setMapExploredByBuilding(int x, int y, int w, int h, int team)
{
	for (int dx = x; dx < x + w; dx++)
		for (int dy = y; dy < y + h; dy++)
			if (exploredArea[team][coordToIndex(dx, dy)] < EXPLORED_BY_BUILDING_MIN)
				exploredArea[team][coordToIndex(dx, dy)] = EXPLORED_BY_BUILDING_MIN;
}

void Map::unsetMapDiscovered(void)
{
	fill(mapDiscovered, 0u);
}

bool Map::isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const
{
	assert((x1<x2) && (y1<y2));
	for(int x=x1;x<=x2;x++)
	{
		for(int y=y1;y<=y2;y++)
		{
			if(isMapDiscovered(x,y,visionMask))
			{
				return true;
			}
		}
	}
	return false;
}

void Map::setMapDiscovered(void)
{
	fill(mapDiscovered, ~0u);
}

void Map::computeDisplayedForbidden(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedForbiddenView.set(i, (tiles[i].forbidden & teamMask) != 0);
}

void Map::computeDisplayedGuardArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedGuardAreaView.set(i, (tiles[i].guardArea & teamMask) != 0);
}

void Map::computeDisplayedClearArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedClearAreaView.set(i, (tiles[i].clearArea & teamMask) != 0);
}


void Map::initExploredArea(int teamNumber)
{
	std::fill(exploredArea[teamNumber], exploredArea[teamNumber] + size, 0);
}

// Seed a team's explored area from its discovery map. Used when the file
// carries no explored area (map files, saves older than
// EXPLORED_AREA_SAVED_VERSION_MINOR): every discovered tile is stamped as
// freshly explored so explorers head outward from the start area instead of
// treating the whole map as unexplored.
void Map::makeDiscoveredAreasExplored(int teamNumber)
{
	assert(game->teams[teamNumber]);
	assert(game->teams[teamNumber]->me);
	assert(exploredArea[teamNumber]);
	for (int x = 0; x < getW(); x++)
		for (int y = 0; y < getH(); y++)
			if (isMapDiscovered(x, y, game->teams[teamNumber]->me))
				setMapExploredByUnit(x, y, 1, 1, teamNumber);
}

void Map::updateExploredArea(int teamNumber)
{
	for (size_t i = 0; i < size; i++)
		if (exploredArea[teamNumber][i] > 0)
			exploredArea[teamNumber][i]--;
}
