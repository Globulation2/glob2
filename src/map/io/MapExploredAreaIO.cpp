// SPDX-License-Identifier: GPL-3.0-or-later

#include "Map.h"

#include <Stream.h>
#include <vector>

void Map::saveExploredArea(GAGCore::OutputStream *stream, int numberOfTeams)
{
	stream->writeEnterSection("exploredArea");
	for (int t=0; t<numberOfTeams; t++)
	{
		assert(exploredArea[t]);
		stream->writeEnterSection(t);
		stream->write(exploredArea[t], size, "explored");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

void Map::loadExploredArea(GAGCore::InputStream *stream, int numberOfTeams, bool keep)
{
	std::vector<Uint8> discard(keep ? 0 : size);
	stream->readEnterSection("exploredArea");
	for (int t=0; t<numberOfTeams; t++)
	{
		stream->readEnterSection(t);
		Uint8 *dest = discard.data();
		if (keep)
		{
			assert(exploredArea[t] == NULL);
			exploredArea[t] = new Uint8[size];
			dest = exploredArea[t];
		}
		stream->read(dest, size, "explored");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}
