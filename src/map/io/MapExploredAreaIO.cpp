// SPDX-License-Identifier: GPL-3.0-or-later

#include "Map.h"

#include <Stream.h>
#include <PackedArray.h>
#include "FileFormatVersions.h"
#include <vector>

void Map::saveExploredArea(GAGCore::OutputStream *stream, int numberOfTeams)
{
	stream->writeEnterSection("exploredArea");
	for (int t=0; t<numberOfTeams; t++)
	{
		assert(exploredArea[t]);
		stream->writeEnterSection(t);
		if(GAGCore::PackedArray::binary(stream)) GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return exploredArea[t][i];});
        else stream->write(exploredArea[t], size, "explored");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

void Map::loadExploredArea(GAGCore::InputStream *stream, int numberOfTeams, bool keep, int versionMinor)
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
		if(versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream))
            GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){dest[i]=v;});
        else stream->read(dest, size, "explored");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}
