// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Building.h"
#include "gradient/MapRouting.h"
#include "Game.h"
#include "gradient/GradientRuntime.h"
#include "Bullet.h"
#include "Sector.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <FileManager.h>
#include <algorithm>
#include <tuple>



// Miscellaneous helpers: checkSum, warpDist*, dumpGradient

Uint32 Map::checkSum(bool heavy, bool includePending, bool legacy152)
{
    // Only the current-boundary phase matters: old tick markers all mean due
    // publication/submission on the next observation, while current ones skip it.
    // This scheduling state belongs in every current checksum tier; the explicit
    // legacy checksum projection keeps its historical field layout.
    const Uint32 worldPhase = !legacy152 && game
        ? (routing().gradientRuntime->worldResultsPublishedTick == game->stepCounter ? 1u : 0u)
          | (routing().gradientRuntime->worldWorkSubmittedTick == game->stepCounter ? 2u : 0u)
        : 0u;
    Uint32 cs = (legacy152 ? 0 : routing().gradientRuntime->adaptive.checksum()) ^ worldPhase ^ size ^ terrainRegistry().checksum() ^ resourceRegistry().checksum() ^ routing().gradientRuntime->growth.checksum(heavy && includePending);
    if (!legacy152 && activeClaimsEnabled()) cs ^= activeClaimRouteChecksum(heavy && includePending);
	if (heavy)
	{
		for (size_t index = 0; index < cellCount(); ++index)
		{

			cs+=
				static_cast<Uint32>(vertexTerrain[index]) +
				occupancyCells[index].building +
				resourceCells[index].resource.getUint32() +
                resourceCells[index].mayGrow +
				occupancyCells[index].groundUnit +
				occupancyCells[index].airUnit +
				areaCells[index].forbidden +
				areaCells[index].farm + // zero everywhere unless a farm area was painted
				scriptAreaCells[index];
            if (!resourceStockIndices.empty() && resourceStockIndices[index])
                for (auto stock:resourceStocks[resourceStockIndices[index]-1]) cs=rotl1(cs)^stock;
			cs=rotl1(cs);
		}
	};
	// Bullets retain launch-time recipe state after their source disappears.
	// Include list order: multiple impacts can change destruction and attribution.
	for (int sector = 0; sector < sizeSector; ++sector)
		if (!sectors[sector].bullets.empty())
		{
			cs = rotl1(cs) ^ static_cast<Uint32>(sector);
			cs = rotl1(cs) ^ static_cast<Uint32>(sectors[sector].bullets.size());
			for (const Bullet* bullet : sectors[sector].bullets) cs = rotl1(cs) ^ bullet->checkSum();
		}
    // Costs belong to each published generation, independently of team policy.
    // Iterate logical slots, never the address-keyed metadata registry.
    if(!legacy152 && game) {
        const auto field=[&](const Uint16* cells,int access) {
            cs=rotl1(cs)^Uint32(cells?routing().publishedCostClass(cells,access)+1:0);
        };
        for(int team=0;team<game->teamsCount();++team) {
            for(int access:{0,int(MovementAccess::Amphibious),WATER_ONLY_CLASS,AIR_MOVEMENT_CLASS}) {
                for(int material=0;material<MaterialSlotCount;++material) {
                    field(routing().materialGradients[team][material][access],access);
                    field(routing().marketMaterialGradients[team][material][access],access);
                }
                field(routing().forbiddenGradient[team][access],access);
                field(routing().guardAreasGradient[team][access],access);field(routing().clearAreasGradient[team][access],access);
            }
            for(const auto* building:game->teams[team]->liveBuildings.entries())
                for(int slot=0;slot<BUILDING_GRADIENT_COUNT;++slot)
                    field(building->globalGradient[slot],slot%MOVEMENT_CLASS_COUNT);
        }
        if(includePending)routing().gradientRuntime->pipeline.visitPreparationCosts([&](int cost){cs=rotl1(cs)^Uint32(cost);});
    }
	// Cache age and eviction order affect subsequent routes and are simulation state.
	const auto mix64=[&](Uint64 value) { cs=rotl1(cs)^Uint32(value)^Uint32(value>>32); };
	mix64(std::max<Uint64>(routing().gradientRuntime->materialCacheBudget,Uint64(size)*sizeof(Uint16))); mix64(routing().gradientRuntime->materialCacheClock);
	for (const auto& team : routing().gradientRuntime->stockRevision) for (Uint64 revision : team) mix64(revision);
	for (Uint64 key : routing().gradientRuntime->materialLru)
	{
		const auto& entry=routing().gradientRuntime->materialFields.at(key);
		cs=rotl1(cs)^Uint32(key)^Uint32(key>>32)^entry.topology^entry.builtStep;
		cs=rotl1(cs)^Uint32(entry.recency)^Uint32(entry.recency>>32)^Uint32(entry.sourceRevision)^Uint32(entry.sourceRevision>>32);
        if (!legacy152) {
            cs=rotl1(cs)^Uint32(entry.published!=nullptr)^Uint32(entry.awaitingFirstRead<<1)^Uint32(entry.excludesShared<<2);
            cs=rotl1(cs)^entry.publishedStep;
            if(entry.published)cs=rotl1(cs)^routing().publishedCostClass(entry.published,entry.swim);
        }
        if (heavy && entry.published) for (size_t i=0; i<size; ++i) cs=rotl1(cs)^entry.published[i];
	}

    if (!legacy152 && routing().gradientRuntime->materialDestinationsEnabled) {
        mix64(0x4d4154455249414cULL);
        mix64(routing().gradientRuntime->pipeline.destinationPublicationSequence());
        std::vector<std::tuple<unsigned,std::shared_ptr<const gradient_kernel::OwnedDestinations>,Uint64>> outputs;
        routing().gradientRuntime->pipeline.visitPublishedDestinations([&](auto* slot,auto planes,auto generation) {
            outputs.emplace_back(routing().gradientDestination(slot),std::move(planes),generation);
        });
        std::sort(outputs.begin(),outputs.end(),[](const auto& a,const auto& b){return std::get<0>(a)<std::get<0>(b);});
        for (const auto& [id,planes,generation]:outputs) {
            mix64(id); mix64(generation);
            if (heavy) for (size_t i=0;i<size;++i) mix64(Uint32(planes->x[i])|(Uint32(planes->y[i])<<16));
        }
    }

    // The explicit historical representation predates delayed publication.
    // Keep its checksum layout stable even when the new queues are empty.
    if (!legacy152) {
        cs=rotl1(cs)^Uint32(routing().gradientRuntime->fieldRequests.size());
        for (const auto& queued:routing().gradientRuntime->fieldRequests) {
            const auto& request=queued.request;
            // Hash stored request metadata without reverse-looking up its slot
            // in the destination registry on every tick.
            cs=rotl1(cs)^unsigned(request.kind)^unsigned(request.team<<8)^unsigned(request.material<<16)^unsigned(request.swim<<24);
            cs=rotl1(cs)^Uint32(request.consumer)^request.modes^Uint32(request.excludesShared)^Uint32(request.destinations<<1);
        }
        if (includePending) routing().gradientRuntime->pipeline.visitLogical([&](auto* slot, unsigned remaining, bool superseded) {
            cs=rotl1(cs)^routing().gradientDestination(slot)^remaining^Uint32(superseded);
        });
        const auto mix=[&](Uint32 value) { cs=rotl1(cs)^value; };
        mix(routing().gradientRuntime->buildingRequests.size());
        for (const auto& r:routing().gradientRuntime->buildingRequests) {
            const auto* b=routing().buildingGradientDestination(r.team,r.building,r.identity);
            mix(r.team); mix(r.building); mix(r.identity); mix(r.slot);
            mix(r.stale || !b || b->refreshEpoch[r.slot]!=r.epoch);
        }
        routing().gradientRuntime->buildings.visitLogical([&](const auto& p,unsigned remaining,bool superseded) {
            const auto* b=routing().buildingGradientDestination(p.team,p.buildingId,p.scriptIdentity);
            mix(p.team); mix(p.buildingId); mix(p.scriptIdentity); mix(p.slot);mix(p.swim);
            mix(p.captureTick); mix(p.generation); mix(remaining);
            mix(superseded || !b || b->refreshEpoch[p.slot]!=p.epoch);
        });
        if (includePending) routing().gradientRuntime->pipeline.visitAdaptiveCaptures([&](auto key,bool,const auto& capture,bool legacy) {
            mix64(key.packed());mix64(legacy);mix64(capture.tick);
            // goalCount is derived worker output and is installed only after
            // joining. Hash owner-captured metadata, never partially-built data.
            mix64(capture.targets.added);mix64(capture.targets.removed);
            mix64(capture.targets.routeEpoch);mix64(capture.targets.seedCostEpoch);
            mix64(capture.known);for(auto value:capture.baseline)mix64(value);
        });

    }
    // Suspended freshness changes the next explicit query, so it is simulation state.
    for (int t=0; !legacy152 && game && t<game->teamsCount(); ++t)
        for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw) {
            Uint32 stale=Uint32(routing().forbiddenGradientStale[t][sw])
                | (Uint32(routing().guardGradientStale[t][sw])<<1) | (Uint32(routing().clearGradientStale[t][sw])<<2);
            for (int r=0; r<MaterialCount; ++r) {
                stale |= Uint32(routing().materialGradientStale[t][r][sw]) << (3+r);
                stale |= Uint32(routing().marketMaterialGradientStale[t][r][sw]) << (3+MaterialCount+r);
            }
            if (stale) mix64((Uint64(t*SWIM_CLASS_COUNT+sw)<<32)|stale);
        }

	privateRandom(RandomDomain::GrowthJobs); // Initialize without advancing.
	for (const auto& random : worldRandom.streams)
		cs = rotr1(cs) ^ (legacy152 ? random.legacyChecksum() : random.checksum());
	return cs;
}

void Map::dumpGradient(Uint8 *gradient, const std::string filename)
{
	FILE *fp = globalContainer->fileManager->openFP(filename, "wb");
	if (fp)
	{
		fprintf(fp, "P5 %d %d 255\n", w, h);
		fwrite(gradient, w, h, fp);
		fclose(fp);
	}
}


