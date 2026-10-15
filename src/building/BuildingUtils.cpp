// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingUtils.h"
void BuildingUtils::turretScanTile(int posX, int posY, int ring, int offset,
                                   int octant, int& outX, int& outY)
{
	const int i = ring, j = offset;
	switch (octant)
	{
		case 0: outX = posX-j;   outY = posY-i;   break;
		case 1: outX = posX+j+1; outY = posY-i;   break;
		case 2: outX = posX-j;   outY = posY+i+1; break;
		case 3: outX = posX+j+1; outY = posY+i+1; break;
		case 4: outX = posX-i;   outY = posY-j;   break;
		case 5: outX = posX+i+1; outY = posY-j;   break;
		case 6: outX = posX-i;   outY = posY+j+1; break;
		case 7: outX = posX+i+1; outY = posY+j+1; break;
		default:
			assert(false);
			outX = 0;
			outY = 0;
			break;
	}
}

