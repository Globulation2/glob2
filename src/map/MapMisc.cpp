// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <FileManager.h>



// Miscellaneous helpers: checkSum, warpDist*, dumpGradient

Uint32 Map::checkSum(bool heavy)
{
	Uint32 cs = size ^ terrainRegistry().checksum();
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
			cs=rotl1(cs);
		}
	};
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


