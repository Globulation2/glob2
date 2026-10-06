// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "Bullet.h"
#include "Sector.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <FileManager.h>



// Miscellaneous helpers: checkSum, warpDist*, dumpGradient

Uint32 Map::checkSum(bool heavy)
{
	Uint32 cs = size ^ terrainRegistry().checksum() ^ resourceRegistry().checksum();
	if (heavy)
	{
		for (size_t index = 0; index < tiles.size(); ++index)
		{
			const auto& c = tiles[index];
			cs+=
				static_cast<Uint32>(terrainIds[index]) +
				c.terrain +
				c.building +
				c.resource.getUint32() +
				c.groundUnit +
				c.airUnit +
				c.forbidden +
				c.farmArea + // zero everywhere unless a farm area was painted
				c.scriptAreas;
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
		if (heavy) for (size_t i=0; i<size; ++i) cs=rotl1(cs)^entry.cells[i];
	}

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


