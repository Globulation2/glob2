// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "field/RuntimeTerrainGradient.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"
#include "render/GameAnimations.h"

#include <algorithm>
#include <tuple>
#include <utility>


// growResources, syncStep, fog of war, discovery, explored area

void Fertility::applyGrowthOpportunities(Map& map,int x,int y,std::uint32_t rate,int scarcity)
{
    static_assert(MersenneTwister::min()==0 && MersenneTwister::max()==UINT32_MAX);
    assert(scarcity>=1);
    const unsigned opportunities=growthOpportunities(rate,[]{return syncRand();});
    for(unsigned attempt=0;attempt<opportunities;++attempt)
    {
        if(scarcity!=1 && syncRand()%scarcity!=0) continue;
        // Re-read the source after each attempt: growth can change its amount.
        const Resource& resource=map.getResource(x,y);
        if(resource.type==NO_RES_TYPE) break;
        const auto& properties=map.resourcePropertiesByIndex(resource.type);
        const bool growsHere=!properties.stockDependentGrowth || resource.amount <= syncRand()%properties.stockBranchDivisor;
        if(growsHere)
        {
            if(map.canResourcesGrow(x,y))
            {
                const int type=resource.type;
                const auto stocks=map.materialStocksAt(map.coordToIndex(x,y));
                if (map.incResourceByIndex(x,y,type,resource.variety))
                    map.recordNaturalGrowth(x,y,type,type,stocks);
            }
        }
        if(properties.spreadRate && (!properties.stockDependentGrowth || !growsHere))
        {
            const auto spreads=Fertility::growthOpportunities(properties.spreadRate,[]{return syncRand();});
            for (unsigned spread=0;spread<spreads;++spread)
            {
                int dx,dy;
                Unit::dxDyFromDirection(syncRand()&7,&dx,&dy);
                const int nx=x+dx,ny=y+dy;
                if(map.canResourcesGrow(nx,ny))
                {
                    const auto& before=map.getResource(nx,ny);
                    const int oldType=before.type,type=resource.type;
                    const auto stocks=map.materialStocksAt(map.coordToIndex(nx,ny));
                    if (map.incResourceByIndex(nx,ny,type,resource.variety))
                        map.recordNaturalGrowth(nx,ny,type,oldType,stocks);
                }
            }
        }
    }
}

void Map::growResources(void)
{
    if(game->gameHeader.isResourceGrowthDisabled()) return;
    rebuildGrowthCoverage();
    static constexpr int scarcityDivisor[]={1,2,4,8};
    const int scarcity=scarcityDivisor[game->gameHeader.getResourceScarcityLevel()];
    const int firstY=syncRand()&3;
    for(int y=firstY;y<h;y+=4)
        for(int x=syncRand()&15;x<w;x+=syncRand()&31)
        {
            const auto& resource=getResource(x,y);
            if(resource.type!=NO_RES_TYPE)
                Fertility::applyGrowthOpportunities(*this,x,y,
                    resourceGrowthRateAt(coordToIndex(x,y),resource.type),scarcity);
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

void Map::recordNaturalGrowth(int x,int y,int resourceType,int oldType,int oldAmount)
{
    std::array<Uint16,MaterialCount> stocks{};
    if (oldType!=NO_RES_TYPE && resourceRegistry().valid(unsigned(oldType)))
        stocks[materialIndex(resourcePropertiesByIndex(oldType).primaryMaterial)]=oldAmount;
    recordNaturalGrowth(x,y,resourceType,oldType,stocks);
}

void Map::recordNaturalGrowth(int x,int y,int resourceType,int oldType,const std::array<Uint16,MaterialCount>& oldStocks)
{
    if (!game || !resourceRegistry().valid(unsigned(resourceType))) return;
    const auto index=coordToIndex(x,y);
    const auto& after=resourceCells[index].resource;
    const bool newTile=oldType==NO_RES_TYPE && after.type==resourceType;
    const auto stocks=materialStocksAt(index);
    if (growthCoverage.size()!=size) rebuildGrowthCoverage();
    const auto packed=growthCoverage[index];
    MaterialMask changedMaterials=resourcePropertiesByIndex(resourceType).materialMask;
    if (oldType!=NO_RES_TYPE) changedMaterials|=resourcePropertiesByIndex(oldType).materialMask;
    for (unsigned mask=changedMaterials;mask;mask&=mask-1)
    {
        const auto material=std::countr_zero(mask);
        const int delta=int(stocks[material])-oldStocks[material];
        const int added=newTile && stocks[material]>0;
        if (!delta && !added) continue;
        for (int t=0;t<game->mapHeader.getNumberOfTeams();++t)
        {
            Team* team=game->teams[t]; if (!team) continue;
            auto& m=team->stats.measurements;
            m.growthGlobal[0][material]+=added;
            m.growthGlobal[1][material]+=std::max(0,delta);
            m.growthGlobal[2][material]+=std::max(0,-delta);
            for (int band=0;band<GROWTH_COVERAGE_BANDS;++band)
                if ((packed>>(band*Team::MAX_COUNT))&(Uint64(1)<<t))
                {
                    m.growthTiles[band][material]+=added;
                    m.growthAmount[band][material]+=std::max(0,delta);
                    m.growthReduction[band][material]+=std::max(0,-delta);
                }
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
		metrics.maxPending, metrics.waitNs, pipeline.activeElapsedNs(), metrics.preparationNs};
}

bool Map::hasPendingGradientPreparation() const { return gradientRuntime->preparation.job != nullptr; }
SimulationSnapshot::Requirements Map::pendingGradientRequirements() const
{
    return hasPendingGradientPreparation() ? SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain) : 0;
}
void Map::preparePendingGradient() { preparePendingGradientInputs(nullptr); }
void Map::preparePendingGradient(const SimulationSnapshot::Handle& foundation)
{
    if (!hasPendingGradientPreparation()) return;
    if (!foundation.terrain || foundation.worldIdentity != identity() ||
        foundation.width != getW() || foundation.height != getH() ||
        foundation.terrain->revision != terrainGeneration())
        throw std::invalid_argument("Gradient preparation requires current projected terrain");
    preparePendingGradientInputs(&foundation);
}
void Map::preparePendingGradientInputs(const SimulationSnapshot::Handle* foundation)
{
	// Consume before executing: failure leaves no dangling descriptor, while the
	// pipeline records a completed error so saves/publication cannot wait forever.
	const auto preparation = std::exchange(gradientRuntime->preparation, {});
	if (!preparation.job) return;
	gradientRuntime->pipeline.prepare(preparation.job, [&](GradientPipeline::Job &job) {
		const auto [reserved, kind, team, material, swim] = preparation;
		using Kind = GradientRuntime::Preparation::Kind;
		switch (kind) {
		case Kind::Materials: seedMaterialGradient(team, material, swim, job.data.get()); break;
		case Kind::Markets: seedMaterialGradient(team, material, swim, job.data.get(), true); break;
		case Kind::Guard: seedGuardAreasGradient(team, swim, job.data.get()); break;
		case Kind::Clear: seedClearAreasGradient(team, swim, job.data.get()); break;
		}
		// Propagation owns immutable terrain inputs after the read barrier closes.
		if (foundation) job.terrainLease = foundation->project(SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain));
        job.modifiedCosts = foundation ? foundation->terrain->movementModifiers : hasTerrainMovementModifiers();
		job.registry = foundation ? foundation->terrain->registry : frozenTerrainRegistry();
		job.terrainBuckets = terrainQueueBuckets();
		job.water = !job.modifiedCosts && terrainRegistry().size()>TERRAIN_COUNT && gradient_kernel::weightedClass(swim) ? frozenWaterSnapshot() : nullptr;
		job.profiles = job.modifiedCosts && terrainRegistry().size()>TERRAIN_COUNT ? frozenTerrainMovementSnapshot(swim) : nullptr;
		job.terrain = !job.profiles && !job.water && (job.modifiedCosts || (swim != 0 && swim != SWIM_CLASS_EVEN)) ? (foundation ? foundation->terrain->identity : frozenTerrainSnapshot()) : nullptr;
	});
}

void Map::advanceGradientPipeline() { preparePendingGradient(); gradientRuntime->pipeline.advance(); }
void Map::finishGradientPipeline() { preparePendingGradient(); gradientRuntime->pipeline.finish(); }
void Map::setGradientWorkerCount(unsigned workers) { preparePendingGradient(); gradientRuntime->pipeline.setWorkerCount(workers); }

void Map::configureGradientPipeline(unsigned workers, unsigned delay)
{
	if (workers>16 || delay<1 || delay>16) throw std::invalid_argument("Invalid gradient pipeline configuration");
	preparePendingGradient();
	gradientRuntime->pipeline.onPublished = [this](Uint16** slot) { ++resourceFieldGenerations[slot]; };
	gradientRuntime->pipeline.configure(workers, delay, size, [this](GradientPipeline::Job &job, GradientWorkspace &scratch) {
		const field::Grid geometry{getW(), getH()};
		if (job.water && job.registry && job.registry->size()>TERRAIN_COUNT && !job.modifiedCosts) {
            gradient_kernel::propagateField(job.data.get(),job.swim,GRADIENT_COST_LIMIT,geometry,scratch,
                [water=job.water->data()](size_t i){return water[i]!=0;});
            return;
        }
        if (job.profiles)
		{
			gradient_kernel::propagateTerrainProfiles(job.data.get(), job.swim, GRADIENT_COST_LIMIT,
													  geometry, scratch, job.profiles->data(),
													  job.profiles->movement, job.terrainBuckets);
			return;
		}
		const auto *types = job.terrain ? job.terrain->data() : nullptr;
        // Uniform profiles perform no terrain reads. A weighted profile always
        // captures its semantic IDs before the job leaves the simulation thread.
		gradient_kernel::propagateTerrainField(
			job.data.get(), job.swim, GRADIENT_COST_LIMIT, geometry, scratch,
			[types](size_t i) { return types ? types[i] : GRASS; }, job.modifiedCosts,
			job.registry ? *job.registry : terrainRegistry(), job.terrainBuckets);
	});
}

void Map::syncStep(Uint32 stepCounter, bool preparePeriodic)
{
	preparePendingGradient();
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
			bool changed = hasTerrainMovementModifiers() || (escapeSwim != 0 && escapeSwim != SWIM_CLASS_EVEN);
			if (!changed)
			{
				const Uint32 teamMask = Team::teamNumberToMask(escapeTeam);
				for (size_t i = 0; i < size; ++i)
				{
					const bool blocked = resourceBlocksGround(i)
						|| occupancyCells[i].building != NOGBID || (!terrainPropertiesAt(i).walkable && !(escapeSwim > 0 && terrainPropertiesAt(i).swimmable))
						|| occupancyCells[i].immobileUnit != IMMOBILE_UNIT_NONE;
					const bool goal = !blocked && !(areaCells[i].forbidden & teamMask);
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

	if (preparePeriodic) { stagePeriodicGradientPreparation(); preparePendingGradient(); }
}

void Map::stagePeriodicGradientPreparation()
{
	preparePendingGradient();
	using Kind = GradientRuntime::Preparation::Kind;
	// Queue membership and round-robin flags belong to the simulation owner.
	// Reserve before AI lazy refreshes so invalidation can supersede this job
	// regardless of which read-phase task starts first.
	auto dispatch = [&](Uint16 **slot, Kind kind, int team, int material, int swim) {
		auto *job = gradientRuntime->pipeline.reserve(slot, swim);
		gradientRuntime->preparation = {job, kind, team, material, swim};
	};
	// We only update one gradient per step, round robin over the gradients in use.
	// Fields are allocated lazily: the second pass runs on freshly reset flags,
	// so finding nothing there means no gradient exists yet and there is nothing to do.
	for (int pass=0; pass<2; pass++)
	{
		int numberOfTeam=game->mapHeader.getNumberOfTeams();
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MaterialCount; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					if (hasMaterialSourceSlot(r) && materialGradients[t][r][s] && !gradientUpdated[t][r][s])
					{
						if (gradientRuntime->pipeline.enabled()) dispatch(&materialGradients[t][r][s], Kind::Materials, t, r, s);
						else updateMaterialGradient(t, r, s);
						gradientUpdated[t][r][s]=true;
						return;
					}
		// Market fields participate in the same fixed-tick pipeline and round robin.
		// A stock transition also requests an early refresh.
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MaterialCount; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
					if (marketsV2Enabled() && marketMaterialGradients[t][r][s] && (marketGradientDirty[t][r][s] || !marketGradientUpdated[t][r][s]))
					{
						// Stock transitions already invalidate stale snapshots. Regular refreshes
						// must let earlier jobs publish, even when this is the only field.
						if (gradientRuntime->pipeline.enabled()) dispatch(&marketMaterialGradients[t][r][s], Kind::Markets, t, r, s);
						else updateMaterialGradient(t, r, s, true);
						marketGradientDirty[t][r][s]=false;
						marketGradientUpdated[t][r][s]=true;
						return;
					}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(guardAreasGradient[t][s] && !guardGradientUpdated[t][s])
				{
					if (gradientRuntime->pipeline.enabled()) dispatch(&guardAreasGradient[t][s], Kind::Guard, t, 0, s);
					else updateGuardAreasGradient(t, s);
					guardGradientUpdated[t][s]=true;
					return;
				}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
				if(clearAreasGradient[t][s] && !clearGradientUpdated[t][s])
				{
					if (gradientRuntime->pipeline.enabled()) dispatch(&clearAreasGradient[t][s], Kind::Clear, t, 0, s);
					else updateClearAreasGradient(t, s);
					clearGradientUpdated[t][s]=true;
					return;
				}
				

		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MaterialCount; r++)
				for (int s=0; s<SWIM_CLASS_COUNT; s++)
				{
					gradientUpdated[t][r][s]=false;
					if (marketsV2Enabled()) marketGradientUpdated[t][r][s]=false;
				}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<SWIM_CLASS_COUNT; s++)
			{
				guardGradientUpdated[t][s]=false;
				clearGradientUpdated[t][s]=false;
			}
	}
}

void Map::switchFogOfWar(void)
{
	++snapshotVisibility;
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
	// The snapshot observes discovery and the active fog plane. Updating only
	// the inactive plane becomes visible after switchFogOfWar invalidates it.
	if ((mapDiscovered[index] & sharedVision) != sharedVision
		|| (fogOfWar && (fogOfWar[index] & sharedVision) != sharedVision))
		++snapshotVisibility;
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
	Uint16 bgid = occupancyCells[coordToIndex(x, y)].building;
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
		displayedForbiddenView.set(i, (areaCells[i].forbidden & teamMask) != 0);
}

void Map::computeDisplayedGuardArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedGuardAreaView.set(i, (areaCells[i].guard & teamMask) != 0);
}

void Map::computeDisplayedClearArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedClearAreaView.set(i, (areaCells[i].clear & teamMask) != 0);
}

void Map::computeDisplayedFarmArea(int teamNumber)
{
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
		displayedFarmAreaView.set(i, (areaCells[i].farm & teamMask) != 0);
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
