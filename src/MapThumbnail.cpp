/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "BinaryStream.h"
#include <FileManager.h>
#include <GUIBase.h>
#include "Map.h"
#include "MapHeader.h"
#include "MapThumbnail.h"
#include "Stream.h"
#include "Toolkit.h"
#include "Utilities.h"
#include "zlib.h"

using namespace GAGCore;

MapThumbnail::MapThumbnail()
{
	lastW = 0;
	lastH = 0;
	loaded = false;
}

void MapThumbnail::loadFromMap(const std::string& map)
{
	if (map.empty())
	{
		loaded = false;
		return;
	}

	loaded = true;

	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(map));
	//InputStream *stream = new BinaryInputStream(new CompressedInputStreamBackendFilter(Toolkit::getFileManager()->openInputStreamBackend(mapName)));
	if (stream->isEndOfStream())
	{
		delete stream;
	}
	else
	{
		// read header
		MapHeader header;
		bool good = header.load(stream);
		if (!good)
		{
			delete stream;
			return;
		}

		// read map
		if (stream->canSeek())
			stream->seekFromStart(header.getMapOffset());
		else
			; // TODO : enter correct section


		Map map;
		good = map.load(stream, header);
		delete stream;
		if (!good)
			return;

		// set values
		lastW = map.getW();
		lastH = map.getH();

		// TODO : put this thumbnail code in a function
		int H[3]= { 0, 90, 0 };
		int E[3]= { 0, 40, 120 };
		int S[3]= { 170, 170, 0 };
		int wood[3]= { 0, 60, 0 };
		int corn[3]= { 211, 207, 167 };
		int stone[3]= { 104, 112, 124 };
		int alga[3]= { 41, 157, 165 };
		int pcol[7];
		int pcolIndex, pcolAddValue;

		int dx, dy;
		int nCount;
		float dMx, dMy;
		float minidx, minidy;
		int r, b, g;
		// get data
		int mMax;
		int szX, szY;
		int decX, decY;
		Utilities::computeMinimapData(128, map.getW(), map.getH(), &mMax, &szX, &szY, &decX, &decY);

		dMx=(float)mMax/128.0f;
		dMy=(float)mMax/128.0f;

		for(int i=0; i<(128*128*3); ++i)
		{
			buffer[i]=0;
		}

		for (dy=0; dy<szY; dy++)
		{
			for (dx=0; dx<szX; dx++)
			{
				for (int i=0; i<7; i++)
					pcol[i]=0;
				nCount=0;

				// compute
				for (minidx=(dMx*dx); minidx<=(dMx*(dx+1)); minidx++)
				{
					for (minidy=(dMy*dy); minidy<=(dMy*(dy+1)); minidy++)
					{
						// get color to add
						if (map.isRessourceTakeable((int)minidx, (int)minidy, WOOD))
							pcolIndex=3;
						else if (map.isRessourceTakeable((int)minidx, (int)minidy, CORN))
							pcolIndex=4;
						else if (map.isRessourceTakeable((int)minidx, (int)minidy, STONE))
							pcolIndex=5;
						else if (map.isRessourceTakeable((int)minidx, (int)minidy, ALGA))
							pcolIndex=6;
						else
							pcolIndex=map.getUMTerrain((int)minidx,(int)minidy);

						// get weight to add
						pcolAddValue=5;

						pcol[pcolIndex]+=pcolAddValue;
						nCount++;
					}
				}

				nCount*=5;
				r=(int)((H[0]*pcol[GRASS]+E[0]*pcol[WATER]+S[0]*pcol[SAND]+wood[0]*pcol[3]+corn[0]*pcol[4]+stone[0]*pcol[5]+alga[0]*pcol[6])/(nCount));
				g=(int)((H[1]*pcol[GRASS]+E[1]*pcol[WATER]+S[1]*pcol[SAND]+wood[1]*pcol[3]+corn[1]*pcol[4]+stone[1]*pcol[5]+alga[1]*pcol[6])/(nCount));
				b=(int)((H[2]*pcol[GRASS]+E[2]*pcol[WATER]+S[2]*pcol[SAND]+wood[2]*pcol[3]+corn[2]*pcol[4]+stone[2]*pcol[5]+alga[2]*pcol[6])/(nCount));

				buffer[(dx+decX) * 128 * 3 + (dy+decY) * 3 + 0] = r;
				buffer[(dx+decX) * 128 * 3 + (dy+decY) * 3 + 1] = g;
				buffer[(dx+decX) * 128 * 3 + (dy+decY) * 3 + 2] = b;
			}
		}
	}
}



void MapThumbnail::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("MapThumbnail");
	stream->writeSint16(lastW, "lastW");
	stream->writeSint16(lastH, "lastH");
	//Compress with zlib
	//According to zlib documentation, the out buffer must be 0.1% larger than in buffer + 12 bytes
	unsigned long compressedLength = (128 * 128 * 3 * 1001) / 1000 + 13;
	Uint8* compressed = new Uint8[compressedLength];
	compress2(compressed, &compressedLength, buffer, 128 * 128 * 3, 9);
	stream->writeUint32(compressedLength, "compressedLength");
	stream->write(compressed, compressedLength, "compressed");
	stream->writeLeaveSection();
	delete[] compressed;
}



void MapThumbnail::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapThumbnail");
	lastW = stream->readSint16("lastW");
	lastH = stream->readSint16("lastH");
	Uint32 compressedLength = stream->readUint32("compressedLength");
	Uint8* compressed = new Uint8[compressedLength];
	stream->read(compressed, compressedLength, "compressed");
	//uncompress with zlib
	unsigned long uncompLen = 128 * 128 * 3;
	uncompress(buffer, &uncompLen, compressed, compressedLength);
	stream->readLeaveSection();
	delete[] compressed;
	loaded=true;
}



void MapThumbnail::loadIntoSurface(GAGCore::DrawableSurface *surface)
{
	for(int x=0; x<surface->getW(); ++x)
	{
		for(int y=0; y<surface->getH(); ++y)
		{
			int r = buffer[x * 128 * 3 + y * 3 + 0];
			int g = buffer[x * 128 * 3 + y * 3 + 1];
			int b = buffer[x * 128 * 3 + y * 3 + 2];
			surface->drawPixel(x, y, Color(r, g, b));
		}
	}
}



int MapThumbnail::getMapWidth()
{
	return lastW;
}



int MapThumbnail::getMapHeight()
{
	return lastH;
}



bool MapThumbnail::isLoaded()
{
	return loaded;
}

