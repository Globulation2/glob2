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
    const unsigned opportunities=growthOpportunities(rate,[&]{return map.privateRandom(RandomDomain::ReferenceGrowth).nextU32();});
    for(unsigned attempt=0;attempt<opportunities;++attempt)
    {
        if(scarcity!=1 && map.privateRandom(RandomDomain::ReferenceGrowth).nextU32()%scarcity!=0) continue;
        // Re-read the source after each attempt: growth can change its amount.
        const Resource& resource=map.getResource(x,y);
        if(resource.type==NO_RES_TYPE) break;
        const auto& properties=map.resourcePropertiesByIndex(resource.type);
        const bool growsHere=!properties.stockDependentGrowth || resource.amount <= map.privateRandom(RandomDomain::ReferenceGrowth).nextU32()%properties.stockBranchDivisor;
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
            const auto spreads=Fertility::growthOpportunities(properties.spreadRate,[&]{return map.privateRandom(RandomDomain::ReferenceGrowth).nextU32();});
            for (unsigned spread=0;spread<spreads;++spread)
            {
                int dx,dy;
                Unit::dxDyFromDirection(map.privateRandom(RandomDomain::ReferenceGrowth).nextU32()&7,&dx,&dy);
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

// Immediate reference path for tests and benchmarks. Production ticks use the
// snapshot pipeline; calling both would apply growth twice.
void Map::growResources(void)
{
    if(game->gameHeader.isResourceGrowthDisabled()) return;
    rebuildGrowthCoverage();
    static constexpr int scarcityDivisor[]={1,2,4,8};
    const int scarcity=scarcityDivisor[game->gameHeader.getResourceScarcityLevel()];
    const int firstY=privateRandom(RandomDomain::ReferenceGrowth).nextU32()&3;
    for(int y=firstY;y<h;y+=4)
        for(int x=privateRandom(RandomDomain::ReferenceGrowth).nextU32()&15;x<w;x+=privateRandom(RandomDomain::ReferenceGrowth).nextU32()&31)
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
			for (Building *b : team->liveBuildings.entries())
			{
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
            auto& m=team->stats.events();
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
		metrics.maxPending};
}

bool Map::hasPendingGradientPreparation() const
{
	return gradientRuntime->preparation.job != nullptr || !gradientRuntime->stagedBuildings.empty();
}
namespace {
gradient_preparation::Request gradientRequest(const GradientRuntime::Preparation& p, const Map& map)
{
    gradient_preparation::Request request;
    request.kind=p.kind; request.team=p.team; request.material=p.material; request.swim=p.swim;
    request.terrainBuckets=map.movementQueueBuckets(request.swim);
    if (p.kind == gradient_preparation::Kind::Guard) {
        request.allies=map.game->teams[p.team]->allies;
        request.crowding=map.game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);
    }
    return request;
}
}
void Map::preparePendingGradient(const SimulationSnapshot::Handle& foundation)
{
    if (!hasPendingGradientPreparation()) return;
    if (!foundation.terrain || foundation.terrain->revision != terrainGeneration() ||
        foundation.worldIdentity != identity() || foundation.tick != game->stepCounter ||
        foundation.width != getW() || foundation.height != getH())
        throw std::invalid_argument("Gradient preparation requires the current observation boundary");
    prepareStagedBuildingGradients(foundation);
    if (!gradientRuntime->preparation.job) return;
    auto request=gradientRequest(gradientRuntime->preparation, *this);
    request.consumer=gradientRuntime->preparation.job->request.consumer;
    request.modes=gradientRuntime->preparation.job->request.modes;
    request.excludesShared=gradientRuntime->preparation.job->request.excludesShared;
    auto projected=foundation.gradientView();
    auto* job=std::exchange(gradientRuntime->preparation, {}).job;
    job->request=request;
    job->snapshotLease=std::move(projected);
    gradientRuntime->pipeline.prepare(job, [](GradientPipeline::Job& job) {
        gradient_preparation::seed(job.request, *job.snapshotLease, job.data.get(), *job.crowding);
    });
}

void Map::advanceGradientPipeline()
{
	ensureBuildingGradientPipeline();
	gradientRuntime->pipeline.advance();
	// Building fields publish after the periodic planes, before teams step.
	if (gradientRuntime->buildings.enabled()) gradientRuntime->buildings.advance();
}
void Map::finishGradientPipeline(JoinContext context)
{
	gradientRuntime->pipeline.finish(context);
	gradientRuntime->buildings.finish(context);
}
void Map::resetGradientPipeline() noexcept
{
	gradientRuntime->worldResultsPublishedTick.reset();
	gradientRuntime->worldWorkSubmittedTick.reset();
	gradientRuntime->preparation={};
    gradientRuntime->fieldRequests.clear();
	gradientRuntime->pipeline.reset();
	resetBuildingGradientPipeline();
}
void Map::setGradientWorkerCount(unsigned workers)
{
	gradientRuntime->pipeline.finish({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
	gradientRuntime->buildings.finish({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
}

void Map::configureGradientPipeline(unsigned workers, unsigned delay)
{
	if (workers>16 || delay<1 || delay>16) throw std::invalid_argument("Invalid gradient pipeline configuration");
	ensureBuildingGradientPipeline();
    gradientRuntime->pipeline.validDestination=[this](Uint16** slot) {
        for (const auto& [key,entry]:gradientRuntime->materialFields) if (&entry.published==slot && entry.consumer>=0) {
            const auto* building=game->teams[entry.team]->myBuildings[Building::GIDtoID(entry.consumer)];
            return building && building->scriptIdentity==entry.identity && building->typeNum==entry.type
                && building->posX==entry.x && building->posY==entry.y
                && building->usesTeamMaterials()==entry.excludesShared;
        }
        return true;
    };
	gradientRuntime->pipeline.onPublished = [this](Uint16** slot) {
        publishPlane(slot);
        for (auto& [key,entry] : gradientRuntime->materialFields)
            if (&entry.published==slot) {
                // advance() has moved the old allocation into the retired job.
                // Transfer ownership of the new published array to this entry
                // without deleting the old allocation a second time.
                entry.cells.release();
                entry.cells.reset(entry.published);
                entry.awaitingFirstRead=true;
                entry.publishedStep=game->stepCounter;
                break;
            }
    };
	// Work observed at c publishes at boundary c + remaining.
	gradientRuntime->pipeline.deadline = [this](unsigned remaining) {
		return ComputeExecutor::advanceDue(std::uint64_t(game ? game->stepCounter : 0) + remaining);
	};
	gradientRuntime->pipeline.configure(compute, delay, size,
        [](GradientPipeline::Job &job, GradientWorkspace &scratch) {
            gradient_preparation::propagateCPU(job.request,*job.snapshotLease,job.data.get(),scratch);
        });
    gradientRuntime->pipeline.setAsyncWork([](GradientPipeline::Job& job,gradient_kernel::PlanDecision decision) {
        return gradient_preparation::ownPropagation(job.request,*job.snapshotLease,job.data,
            job.owner->session(),decision,job.executorDue);
    });
}

void Map::syncStep(Uint32 stepCounter)
{
	PERF_SCOPE_TIME(Map);
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
	
    // Refresh one enrolled escape field per eight ticks. Seeding and partition
    // examination belong to the worker; publication keeps the fixed deadline.
    const int activeSwimClasses=game->hasWaterOnlyUnits()?SWIM_CLASS_COUNT:LEGACY_SWIM_CLASS_COUNT;
    const int escapeSlots=game->teamsCount()*activeSwimClasses;
    if (escapeSlots && !(stepCounter&7)) {
        const int slot=(stepCounter>>3)%escapeSlots;
        const int team=slot/activeSwimClasses, swim=slot%activeSwimClasses;
        if (forbiddenGradient[team][swim]) updateForbiddenGradient(team,swim);

    }

}

void Map::stagePeriodicGradientPreparation()
{
	// Building work has its own admission budget. Periodic selection may return
	// early inside enqueuePeriodic without skipping FIFO material/area admission.
	stageBuildingGradientPreparation();
    const auto enqueuePeriodic=[&]() {
        using Kind = GradientRuntime::Preparation::Kind;

        // Round-robin visits enroll at most one new warm request per boundary.
        // Coalesced destinations do not consume that opportunity; cold and warm
        // requests then share the same FIFO admission order.
        auto dispatch = [&](Uint16 **slot, Kind kind, int team, int material, int swim) {
            return requestGradientField(slot,int(kind),team,material,swim);
        };
        // A second pass begins a fresh visitation round. If every enrolled field
        // already has a ticket, there is no additional work to enqueue this tick.
        for (int pass=0; pass<2; pass++)
        {
            int numberOfTeam=game->mapHeader.getNumberOfTeams();
            for (int t=0; t<numberOfTeam; t++)
                for (int r=0; r<MaterialCount; r++)
                    for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                        if (hasMaterialSourceSlot(r) && materialGradients[t][r][s] && !gradientUpdated[t][r][s])
                        {
                            const bool queued=dispatch(&materialGradients[t][r][s], Kind::Materials, t, r, s);
                            gradientUpdated[t][r][s]=true; // Visited in this refresh round, even if already pending.
                            if (queued) return;
                        }
            // Market fields participate in the same fixed-tick pipeline and round robin.
            // A stock transition also requests an early refresh.
            for (int t=0; t<numberOfTeam; t++)
                for (int r=0; r<MaterialCount; r++)
                    for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                        if (marketsV2Enabled() && marketMaterialGradients[t][r][s] && (marketGradientDirty[t][r][s] || !marketGradientUpdated[t][r][s]))
                        {
                            // A coalesced request has not captured the newer stock.
                            // Preserve its dirty mark and inspect other destinations;
                            // the old ticket still publishes at its fixed deadline.
                            const bool queued=dispatch(&marketMaterialGradients[t][r][s], Kind::Markets, t, r, s);
                            marketGradientUpdated[t][r][s]=true;
                            if (queued) {
                                marketGradientDirty[t][r][s]=false;
                                return;
                            }
                        }
            for (int t=0; t<numberOfTeam; t++)
                for(int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                    if(guardAreasGradient[t][s] && !guardGradientUpdated[t][s])
                    {
                        const bool queued=dispatch(&guardAreasGradient[t][s], Kind::Guard, t, 0, s);
                        guardGradientUpdated[t][s]=true; // Visited in this refresh round, even if already pending.
                        if (queued) return;
                    }
            for (int t=0; t<numberOfTeam; t++)
                for(int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                    if(clearAreasGradient[t][s] && !clearGradientUpdated[t][s])
                    {
                        const bool queued=dispatch(&clearAreasGradient[t][s], Kind::Clear, t, 0, s);
                        clearGradientUpdated[t][s]=true; // Visited in this refresh round, even if already pending.
                        if (queued) return;
                    }


            for (int t=0; t<numberOfTeam; t++)
                for (int r=0; r<MaterialCount; r++)
                    for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                    {
                        gradientUpdated[t][r][s]=false;
                        if (marketsV2Enabled()) marketGradientUpdated[t][r][s]=false;
                    }
            for (int t=0; t<numberOfTeam; t++)
                for(int s=0; s<MOVEMENT_CLASS_COUNT; s++)
                {
                    guardGradientUpdated[t][s]=false;
                    clearGradientUpdated[t][s]=false;
                }
        }
    };
    enqueuePeriodic();
    if (!gradientRuntime->fieldRequests.empty()) {
        auto request=gradientRuntime->fieldRequests.front();
        gradientRuntime->fieldRequests.pop_front();
        auto* job=gradientRuntime->pipeline.reserve(request.slot, request.request.swim);
        job->request=request.request;
        gradientRuntime->preparation={job,request.request.kind,request.request.team,request.request.material,request.request.swim};
    }


}

void Map::switchFogOfWar(void)
{
	visibilityChanges.markAll();
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
		markVisibility(index);
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
	visibilityChanges.markAll();
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
	visibilityChanges.markAll();
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

bool Map::requestGradientField(Uint16** slot, int kind, int team, int material, int swim, int consumer, unsigned modes)
{
    // Snapshot readers cannot mutate shared scheduling, even when a serial
    // executor runs their job on the owner thread. Enrollment is owner-only.
    if (compute.insideJob()) return false;
    if (!gradientRuntime->pipeline.enabled()) configureGradientPipeline(0,8);
    // A queued ticket captures all edits up to admission. An admitted ticket
    // keeps its snapshot/deadline; later edits are picked up by a subsequent
    // periodic or demand refresh instead of continually restarting this work.
    for (const auto& queued:gradientRuntime->fieldRequests) if (queued.slot==slot) return false;
    bool inFlight=false;
    gradientRuntime->pipeline.forEachDestination([&](Uint16** destination) { if (destination==slot) inFlight=true; });
    if (inFlight) return false;
    gradient_preparation::Request request;
    request.kind=static_cast<gradient_preparation::Kind>(kind); request.team=team;
    request.material=material; request.swim=swim; request.consumer=consumer; request.modes=modes;
    if (consumer>=0) {
        const auto* building=game->teams[team]->myBuildings[Building::GIDtoID(consumer)];
        request.excludesShared=building && building->usesTeamMaterials();
    }
    gradientRuntime->fieldRequests.push_back({slot,request});
    return true;
}

// Persisted destination namespaces. Keep these bases stable across save formats;
// cache entries use std::map order, reconstructed before tickets are restored.
namespace {
constexpr unsigned ForbiddenDestinationBase=5000;
constexpr unsigned CachedDestinationBase=10000;
static_assert(Team::MAX_COUNT*(2*MaterialSlotCount+2)*MOVEMENT_CLASS_COUNT<=ForbiddenDestinationBase);
static_assert(ForbiddenDestinationBase+Team::MAX_COUNT*MOVEMENT_CLASS_COUNT<=CachedDestinationBase);
}

unsigned Map::gradientDestination(Uint16* const* slot) const
{
    for (int t=0;t<Team::MAX_COUNT;++t) for (int swim=0;swim<MOVEMENT_CLASS_COUNT;++swim) {
        if (slot==&forbiddenGradient[t][swim]) return ForbiddenDestinationBase+t*MOVEMENT_CLASS_COUNT+swim;
        if (slot==&guardAreasGradient[t][swim]) return (t*(MaterialSlotCount+2)+MaterialSlotCount)*MOVEMENT_CLASS_COUNT+swim;
        if (slot==&clearAreasGradient[t][swim]) return (t*(MaterialSlotCount+2)+MaterialSlotCount+1)*MOVEMENT_CLASS_COUNT+swim;
        for (int m=0;m<MaterialSlotCount;++m) {
            if (slot==&materialGradients[t][m][swim]) return (t*(MaterialSlotCount+2)+m)*MOVEMENT_CLASS_COUNT+swim;
            if (slot==&marketMaterialGradients[t][m][swim]) return Team::MAX_COUNT*(MaterialSlotCount+2)*MOVEMENT_CLASS_COUNT+(t*MaterialSlotCount+m)*MOVEMENT_CLASS_COUNT+swim;
        }
    }
    unsigned i=CachedDestinationBase;
    for (const auto& [key,entry]:gradientRuntime->materialFields) { if (slot==&entry.published) return i; ++i; }
    throw std::runtime_error("Unknown gradient destination");
}
Uint16** Map::gradientDestination(unsigned id, unsigned savedClasses)
{
    if (id>=CachedDestinationBase) {
        id-=CachedDestinationBase; if (id>=gradientRuntime->materialFields.size()) throw std::runtime_error("Invalid cached gradient destination");
        auto it=gradientRuntime->materialFields.begin(); std::advance(it,id); return &it->second.published;
    }
    if (id>=ForbiddenDestinationBase) {
        id-=ForbiddenDestinationBase; if (id>=unsigned(game->teamsCount()*savedClasses)) throw std::runtime_error("Invalid escape gradient destination");
        return &forbiddenGradient[id/savedClasses][id%savedClasses];
    }
    const unsigned marketBase=Team::MAX_COUNT*(MaterialSlotCount+2)*savedClasses;
    const bool market=id>=marketBase; if (market) id-=marketBase;
    const unsigned kinds=market?MaterialSlotCount:MaterialSlotCount+2;
    const unsigned team=id/(kinds*savedClasses), kind=(id/savedClasses)%kinds, swim=id%savedClasses;
    if (team>=unsigned(game->teamsCount())) throw std::runtime_error("Invalid gradient team");
    return market?&marketMaterialGradients[team][kind][swim]:kind<MaterialSlotCount?&materialGradients[team][kind][swim]:kind==MaterialSlotCount?&guardAreasGradient[team][swim]:&clearAreasGradient[team][swim];
}
