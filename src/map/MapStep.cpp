// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "gradient/GradientTargetCell.h"
#include "gradient/AdaptiveGradientModel.h"
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


void Map::configureGradientWorkStats(const std::string& path)
{
    gradientRuntime->workStats.reset();
    gradientRuntime->workStats=std::make_unique<GradientWorkStats>(path);
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
	return !gradientRuntime->preparations.empty() || !gradientRuntime->stagedBuildings.empty();
}
namespace {
std::optional<GradientWorkStats::Key> gradientWorkIdentity(const Map& map,std::optional<GradientFieldKey> shared,int kind,int team,int material,int swim,int consumer,unsigned modes)
{
    if(shared)return GradientWorkStats::Key(*shared);
    if(kind!=int(gradient_preparation::Kind::Markets) || !map.game)return std::nullopt;
    const auto* building=consumer<0?nullptr:map.game->teams[team]->myBuildings[Building::GIDtoID(consumer)];
    return GradientWorkStats::consumerKey(team,swim,material,consumer<0?65535:consumer,building?building->scriptIdentity:0,modes);
}
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
    auto staged=std::exchange(gradientRuntime->preparations,{});
    std::vector<GradientPipeline::Job*> jobs;
    jobs.reserve(staged.size());
    for (const auto& preparation:staged) {
        auto* job=preparation.job;
        auto request=gradientRequest(preparation,*this);
        // Consumer tickets retain the exclusions selected at admission.
        request.consumer=job->request.consumer;
        request.modes=job->request.modes;
        request.excludesShared=job->request.excludesShared;
        job->request=request;
        job->snapshotLease=foundation.gradientView();
        if(job->adaptiveKey) {
            if(job->adaptiveKey->family==GradientFieldFamily::Markets)ensureGradientMarketTargets(job->adaptiveKey->team,job->adaptiveKey->material);
            auto& policy=gradientRuntime->adaptive;
            job->adaptiveCapture=policy.enroll(*job->adaptiveKey).capture(unsigned(foundation.tick),policy.currentTargets(*job->adaptiveKey),0);
        }
        jobs.push_back(job);
    }
    gradientRuntime->pipeline.prepareBatch(jobs,[](GradientPipeline::Job& job) {
        job.preparedGoalCount=gradient_preparation::seed(job.request,*job.snapshotLease,job.data.get(),*job.crowding);
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
void Map::resetGradientPipeline(bool replacingWorld) noexcept
{
	gradientRuntime->worldResultsPublishedTick.reset();
	gradientRuntime->worldWorkSubmittedTick.reset();
	gradientRuntime->preparations.clear();
    gradientRuntime->fieldRequests.clear();
	gradientRuntime->pipeline.reset();
    gradientRuntime->adaptive.clearAssociations();
	resetBuildingGradientPipeline();
    // Loading rebuilds this cache from authoritative suppliers without changing
    // saved epochs. A live-world pipeline reset must keep the ledger so the next
    // supplier transition is still observed.
    if (replacingWorld) gradientRuntime->marketTargets={};
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
	gradientRuntime->pipeline.onPublished = [this](GradientPipeline::Job& job) {
        auto** slot=job.slot;
        if(job.adaptiveKey) {
            auto capture=job.adaptiveCapture;
            capture.goalCount=job.preparedGoalCount;
            auto& field=gradientRuntime->adaptive.enroll(*job.adaptiveKey);
            field.published(capture);
            field.inflight=gradientRuntime->pipeline.contains(*job.adaptiveKey,&job);
        }
        if(auto& stats=gradientRuntime->workStats)
            if(const auto key=gradientWorkIdentity(*this,job.adaptiveKey,int(job.request.kind),job.request.team,job.request.material,job.request.swim,job.request.consumer,job.request.modes))
                stats->published(*key,game->stepCounter,job.workReason,size);
        if (const auto key=planeKeyForSlot(slot)) {
            const auto id=MapState::decodePlaneKey(*key);
            (id.market ? marketMaterialGradientStale[id.team][id.resource][id.swim]
                       : materialGradientStale[id.team][id.resource][id.swim])=false;
        } else {
            for (int t=0;t<Team::MAX_COUNT;++t)
                for (int swim=0;swim<MOVEMENT_CLASS_COUNT;++swim) {
                    if (slot==&forbiddenGradient[t][swim]) forbiddenGradientStale[t][swim]=false;
                    if (slot==&guardAreasGradient[t][swim]) guardGradientStale[t][swim]=false;
                    if (slot==&clearAreasGradient[t][swim]) clearGradientStale[t][swim]=false;
                }
        }
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
	
}

void Map::stagePeriodicGradientPreparation()
{
    // Lazy building and consumer caches keep their explicit requests. Idle
    // shared capacity stays idle; no family replaces another family's work.
    stageBuildingGradientPreparation();
    if(auto& probe=gradientRuntime->qualityProbe)probe->stageEpisodes(*this);
    auto& pipeline=gradientRuntime->pipeline;
    auto& policy=gradientRuntime->adaptive;
    const auto admit=[&](const GradientRuntime::FieldRequest& request) {
        auto* job=pipeline.reserve(request.slot,request.request.swim);
        job->request=request.request;
        job->adaptiveKey=gradientFieldKey(request.slot);
        if(job->adaptiveKey) {
            policy.enroll(*job->adaptiveKey).admitted(game->stepCounter);
        }
        if(auto& stats=gradientRuntime->workStats)
            if(const auto key=gradientWorkIdentity(*this,job->adaptiveKey,int(request.request.kind),request.request.team,request.request.material,request.request.swim,request.request.consumer,request.request.modes))
                job->workReason=stats->admitted(*key,game->stepCounter,size);
        gradientRuntime->preparations.push_back({job,request.request.kind,request.request.team,request.request.material,request.request.swim});
    };
    // Restored tickets may have been queued under an earlier, shorter minimum.
    // Keep them pending without preventing a missing field behind them from
    // initializing. Eligible requests retain their original FIFO order.
    for(auto it=gradientRuntime->fieldRequests.begin();
        it!=gradientRuntime->fieldRequests.end() && pipeline.canReserve();) {
        const auto key=gradientFieldKey(it->slot);
        if(*it->slot && key && !policy.enroll(*key).minimumIntervalElapsed(game->stepCounter)) {
            ++it;
            continue;
        }
        const auto request=*it;
        it=gradientRuntime->fieldRequests.erase(it);
        admit(request);
    }
    for(auto& [key,field]:policy.fields) {
        auto** slot=gradientFieldDestination(key);
        if(!slot)continue;
        const auto interval=gradientRuntime->exploration?gradientRuntime->exploration->intervalFor(key.packed()):0;
        const auto prediction=[family=static_cast<unsigned>(key.family)](const auto& inputs,const auto& counts) {
            return AdaptiveGradientModel::predict(inputs,counts,family);
        };
        const auto evaluation=field.evaluate(game->stepCounter,policy.currentTargets(key),*slot!=nullptr,
            prediction,interval);
        if(!evaluation.requested())continue;
        if(!pipeline.canReserve()){field.defer(evaluation.reason);continue;}
        if(!requestGradientField(slot,int(key.family),key.team,key.material==GradientFieldKey::NoMaterial?0:key.material,key.swim,-1,1,int(evaluation.reason)))continue;
        const auto request=gradientRuntime->fieldRequests.back();
        gradientRuntime->fieldRequests.pop_back();admit(request);
        if(auto& exploration=gradientRuntime->exploration)
            exploration->record(game->stepCounter,key.packed(),GradientWorkStats::reasonName(unsigned(evaluation.reason)),true);
    }
}

void Map::switchFogOfWar(void)
{
	visibilityChanges.markAll();
	PERF_SCOPE_TIME(Fog);
	auto* previous=fogOfWar;
	auto* next=previous==fogOfWarA.data() ? fogOfWarB.data() : fogOfWarA.data();
	if(gradientRuntime->adaptive.fields.empty()) {
		memset(previous,0,size*sizeof(Uint32));
		fogOfWar=next;
	} else {
		fogOfWar=next;
		// Fuse target visibility deltas into the existing fog-plane clear. No
		// inference scan, reference gradient, or loop over enrolled fields.
		for(size_t index=0;index<size;++index) {
			const auto old=previous[index];
			previous[index]=0;
			const auto type=resourceCells[index].resource.type;
			if(old==next[index] || type==NO_RES_TYPE
				|| !resourcePropertiesByIndex(type).visibleToHarvest || !materialMaskAt(index)) continue;
			auto before=captureGradientTargetCell(index);
			before.visibleTeams=old;
			gradientTargetCellChanged(index,before);
		}
	}
}

void Map::setMapDiscovered(int x, int y, Uint32 sharedVision)
{
	size_t index = coordToIndex(x, y);
	const auto targetBefore=gradientRuntime->adaptive.fields.empty() || !fogOfWar
		|| (fogOfWar[index]&sharedVision)==sharedVision ? std::optional<GradientTargetCell>{}
		: std::optional{captureGradientTargetCell(index)};
	// The snapshot observes discovery and the active fog plane. Updating only
	// the inactive plane becomes visible after switchFogOfWar invalidates it.
	if ((mapDiscovered[index] & sharedVision) != sharedVision
		|| (fogOfWar && (fogOfWar[index] & sharedVision) != sharedVision))
		markVisibility(index);
	mapDiscovered[index] |= sharedVision;
	fogOfWarA[index] |= sharedVision;
	fogOfWarB[index] |= sharedVision;
	if(targetBefore) gradientTargetCellChanged(index,*targetBefore);
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

bool Map::requestGradientField(Uint16** slot, int kind, int team, int material, int swim, int consumer, unsigned modes, int refreshReason)
{
    // Snapshot readers cannot mutate shared scheduling, even when a serial
    // executor runs their job on the owner thread. Enrollment is owner-only.
    if (compute.insideJob()) return false;
    if (!gradientRuntime->pipeline.enabled()) configureGradientPipeline(0,8);
    const auto key=gradientFieldKey(slot);
    const auto reason=refreshReason<0?unsigned(*slot?AdaptiveGradientState::Reason::Reactive:AdaptiveGradientState::Reason::Cold):unsigned(refreshReason);
    const auto observedRequest=[&](bool duplicate) {
        if(auto& stats=gradientRuntime->workStats)
            if(const auto identity=gradientWorkIdentity(*this,key,kind,team,material,swim,consumer,modes))
                stats->request(*identity,game?game->stepCounter:0,reason,duplicate);
    };
    // A queued ticket captures all edits up to admission. An admitted ticket
    // keeps its snapshot/deadline; later edits are picked up by a subsequent
    // periodic or demand refresh instead of continually restarting this work.
    for (const auto& queued:gradientRuntime->fieldRequests) if (queued.slot==slot){observedRequest(true);return false;}
    bool inFlight=false;
    gradientRuntime->pipeline.forEachDestination([&](Uint16** destination) { if (destination==slot) inFlight=true; });
    if(inFlight) {
        observedRequest(true);
        // Re-reading an unchanged pending field does not manufacture a second
        // reactive refresh. A real post-capture edit retains its recovery need.
        if(key)gradientRuntime->pipeline.visitAdaptiveCaptures([&](GradientFieldKey pending,bool superseded,const AdaptiveGradientState::Capture& capture) {
            if(!superseded && pending==*key && gradientRuntime->adaptive.currentTargets(*key)!=capture.targets)
                gradientRuntime->adaptive.enroll(*key).reactive=true;
        });
        return false;
    }
    if(key && *slot) {
        auto& field=gradientRuntime->adaptive.enroll(*key);
        field.reactive=true;
        // Direct recovery uses the same minimum as model evaluation. Retain the
        // signal for a later boundary instead of bypassing it through the FIFO.
        if(game && !field.minimumIntervalElapsed(game->stepCounter)) {
            field.demand=true;observedRequest(false);return false;
        }
    }
    gradient_preparation::Request request;
    request.kind=static_cast<gradient_preparation::Kind>(kind); request.team=team;
    request.material=material; request.swim=swim; request.consumer=consumer; request.modes=modes;
    if (consumer>=0) {
        const auto* building=game->teams[team]->myBuildings[Building::GIDtoID(consumer)];
        request.excludesShared=building && building->usesTeamMaterials();
    }
    observedRequest(false);
    gradientRuntime->fieldRequests.push_back({slot,request});
    if(key)gradientRuntime->adaptive.enroll(*key).queued=true;
    return true;
}

std::optional<GradientFieldKey> Map::gradientFieldKey(Uint16* const* slot) const
{
    if(const auto plane=planeKeyForSlot(slot)) {
        const auto id=MapState::decodePlaneKey(*plane);
        if(id.resource<MaterialCount)return GradientFieldKey{id.market?GradientFieldFamily::Markets:GradientFieldFamily::Materials,
            Uint8(id.team),Uint8(id.swim),Uint8(id.resource)};
    }
    for(int team=0;team<Team::MAX_COUNT;++team)for(int swim=0;swim<MOVEMENT_CLASS_COUNT;++swim) {
        if(slot==&guardAreasGradient[team][swim])return GradientFieldKey{GradientFieldFamily::Guard,Uint8(team),Uint8(swim)};
        if(slot==&clearAreasGradient[team][swim])return GradientFieldKey{GradientFieldFamily::Clear,Uint8(team),Uint8(swim)};
        if(slot==&forbiddenGradient[team][swim])return GradientFieldKey{GradientFieldFamily::Forbidden,Uint8(team),Uint8(swim)};
    }
    return std::nullopt;
}
Uint16** Map::gradientFieldDestination(GradientFieldKey key)
{
    if(!key.valid(Team::MAX_COUNT,MOVEMENT_CLASS_COUNT,MaterialCount))return nullptr;
    switch(key.family) {
    case GradientFieldFamily::Materials:return &materialGradients[key.team][key.material][key.swim];
    case GradientFieldFamily::Markets:return &marketMaterialGradients[key.team][key.material][key.swim];
    case GradientFieldFamily::Guard:return &guardAreasGradient[key.team][key.swim];
    case GradientFieldFamily::Clear:return &clearAreasGradient[key.team][key.swim];
    case GradientFieldFamily::Forbidden:return &forbiddenGradient[key.team][key.swim];
    }
    return nullptr;
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

void Map::configureResourceGradientExploration(const std::string& intervals, Uint32 seed,
    const std::string& decisionPath)
{
    if (!globalContainer || !globalContainer->structuredHeadless || !gradientRuntime->qualityProbe)
        throw std::invalid_argument("Gradient exploration requires structured headless collection with the probe enabled");
    ResourceGradientExploration config;
    if (!intervals.empty()) config.intervals=ResourceGradientExploration::parseIntervals(intervals);
    if(!AdaptiveGradientModel::Trained && std::find(config.intervals.begin(),config.intervals.end(),0u)!=config.intervals.end())
        throw std::invalid_argument("Unified adaptive coefficients are untrained; bootstrap requires positive fixed intervals");
    if(!AdaptiveGradientModel::Trained && config.intervals.empty())
        throw std::invalid_argument("Untrained unified collection requires explicit fixed intervals");
    config.seed=seed;
    config.decisions.open(decisionPath);
    if (!config.decisions) throw std::runtime_error("Cannot open gradient exploration decisions");
    config.decisions<<"tick,key,behavior_seed,behavior_interval,reason,admitted\n";
    gradientRuntime->exploration=std::make_unique<ResourceGradientExploration>(std::move(config));
}
