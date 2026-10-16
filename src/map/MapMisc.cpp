// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "gradient/GradientRuntime.h"
#include "Bullet.h"
#include "Sector.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <FileManager.h>



// Miscellaneous helpers: checkSum, warpDist*, dumpGradient

Uint32 Map::checkSum(bool heavy, bool includePending, bool legacy152)
{
    // Only the current-boundary phase matters: old tick markers all mean due
    // publication/submission on the next observation, while current ones skip it.
    // This scheduling state belongs in every current checksum tier; the explicit
    // legacy checksum projection keeps its historical field layout.
    const Uint32 worldPhase = !legacy152 && game
        ? (gradientRuntime->worldResultsPublishedTick == game->stepCounter ? 1u : 0u)
          | (gradientRuntime->worldWorkSubmittedTick == game->stepCounter ? 2u : 0u)
        : 0u;
    Uint32 cs = worldPhase ^ size ^ terrainRegistry().checksum() ^ resourceRegistry().checksum() ^ gradientRuntime->growth.checksum(heavy && includePending);
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
	// Cache age and eviction order affect subsequent routes and are simulation state.
	const auto mix64=[&](Uint64 value) { cs=rotl1(cs)^Uint32(value)^Uint32(value>>32); };
	mix64(std::max<Uint64>(gradientRuntime->materialCacheBudget,Uint64(size)*sizeof(Uint16))); mix64(gradientRuntime->materialCacheClock);
	for (const auto& team : gradientRuntime->stockRevision) for (Uint64 revision : team) mix64(revision);
	for (Uint64 key : gradientRuntime->materialLru)
	{
		const auto& entry=gradientRuntime->materialFields.at(key);
		cs=rotl1(cs)^Uint32(key)^Uint32(key>>32)^entry.topology^entry.builtStep;
		cs=rotl1(cs)^Uint32(entry.recency)^Uint32(entry.recency>>32)^Uint32(entry.sourceRevision)^Uint32(entry.sourceRevision>>32);
        if (!legacy152) {
            cs=rotl1(cs)^Uint32(entry.published!=nullptr)^Uint32(entry.awaitingFirstRead<<1)^Uint32(entry.excludesShared<<2);
            cs=rotl1(cs)^entry.publishedStep;
        }
        if (heavy && entry.published) for (size_t i=0; i<size; ++i) cs=rotl1(cs)^entry.published[i];
	}

    // The explicit historical representation predates delayed publication.
    // Keep its checksum layout stable even when the new queues are empty.
    if (!legacy152) {
        cs=rotl1(cs)^Uint32(gradientRuntime->fieldRequests.size());
        for (const auto& queued:gradientRuntime->fieldRequests) {
            const auto& request=queued.request;
            // Hash stored request metadata without reverse-looking up its slot
            // in the destination registry on every tick.
            cs=rotl1(cs)^unsigned(request.kind)^unsigned(request.team<<8)^unsigned(request.material<<16)^unsigned(request.swim<<24);
            cs=rotl1(cs)^Uint32(request.consumer)^request.modes^Uint32(request.excludesShared);
        }
        if (includePending) gradientRuntime->pipeline.visitLogical([&](auto* slot, unsigned remaining, bool superseded) {
            cs=rotl1(cs)^gradientDestination(slot)^remaining^Uint32(superseded);
        });

        // Requests and deadlines change future routes. Hash normalized validity rather
        // than epoch counters, which are deliberately normalized when loading saves.
        mix64(gradientRuntime->buildingRequests.size());
        for (const auto& request : gradientRuntime->buildingRequests)
        {
            const auto* b = buildingGradientDestination(request.team, request.building, request.identity);
            mix64(request.team); mix64(request.building); mix64(request.identity); mix64(request.slot);
            mix64(request.stale || !b || b->refreshEpoch[request.slot] != request.epoch);
        }
        gradientRuntime->buildings.visitLogical([&](const building_gradient::Job& p, unsigned remaining, bool superseded) {
            const auto* b = buildingGradientDestination(p.team, p.buildingId, p.scriptIdentity);
            mix64(p.team); mix64(p.buildingId); mix64(p.scriptIdentity); mix64(p.slot);
            mix64(p.captureTick); mix64(p.generation); mix64(remaining);
            mix64(superseded || !b || b->refreshEpoch[p.slot] != p.epoch);
        });
        if (heavy && game)
            for (int t = 0; t < game->teamsCount(); ++t)
                for (int id = 0; id < Building::MAX_COUNT; ++id)
                    if (const auto* b = game->teams[t]->myBuildings[id])
                        for (int slot = 0; slot < BUILDING_GRADIENT_COUNT; ++slot)
                        {
                            mix64(b->globalGradient[slot] != nullptr);
                            mix64(b->dirtyGradient[slot]); mix64(b->gradientGeneration[slot]);
                            mix64(b->lastGlobalGradientUpdateStepCounter[slot]); mix64(b->globalGradientUsedStep[slot]);
                            if (b->globalGradient[slot]) for (size_t i = 0; i < size; ++i) mix64(b->globalGradient[slot][i]);
                        }

        cs=gradientRuntime->air.checksum(*this,cs,heavy);
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


