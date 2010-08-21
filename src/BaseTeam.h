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

#ifndef BaseTeam_h
#define BaseTeam_h

#include "GraphicContext.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class BaseTeam
{
public:
	enum TeamType
	{
		T_HUMAN,
		T_AI,
		// Note : T_AI + n is AI type n
	};

	BaseTeam();
	virtual ~BaseTeam(void) { }

	TeamType type;
	Sint32 teamNumber; // index of the current team in the game::teams[] array.
	Sint32 numberOfPlayer; // number of controling players
	GAGCore::Color color;
	Uint32 playersMask;
	
public:
	bool disableRecursiveDestruction;
	
private:
	Uint8 data[16];

public:
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream) const;

	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength();
	Uint32 checkSum();
};

#endif
