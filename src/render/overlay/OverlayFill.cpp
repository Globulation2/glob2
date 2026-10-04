// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "OverlayFill.h"
#include <algorithm>

namespace OverlayFill
{

void increasePoint(int x, int y, int distance, int width, int height,
                   std::vector<Uint32>& field, Uint32& max)
{
	//Update the map
	for(int px=0; px<(distance*2+1); ++px)
	{
		for(int py=0; py<(distance*2+1); ++py)
		{
			int relx = (px-distance);
			int rely = (py-distance);
			if(relx*relx + rely*rely < distance*distance)
			{
				int posx=(x - distance + px + width) % width;
				int posy=(y - distance + py + height) % height;

				field[posx * height + posy]+=distance - (relx*relx + rely*rely) / distance;
				max=std::max(max, field[posx * height + posy]);
			}
		}
	}
}

void spreadPoint(int x, int y, int value, int distance, int width, int height,
                 std::vector<Uint32>& field, Uint32& max)
{
	for (int px=x-distance-1; px<(x+distance+1); px++)
	{
		for (int py=y-distance-1; py<(y+distance+1); py++)
		{
			int relx = (px-x);
			int rely = (py-y);
			if((relx*relx + rely*rely) <= (distance*distance))
			{
				int targetX=(px + width) % width;
				int targetY=(py + height) % height;

				field[targetX * height + targetY]+=value * (distance - (relx*relx + rely*rely) / distance);
				max=std::max(max, field[targetX * height + targetY] );
			}
		}
	}
}

} // namespace OverlayFill
