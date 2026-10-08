// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "OverlayFill.h"
#include <algorithm>
#include <cstdint>

namespace OverlayFill
{

void increasePoint(int x, int y, int distance, int width, int height,
                   std::span<Uint32> field, Uint32& max)
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
                 std::span<Uint32> field, Uint32& max)
{
	size_t cursor=0;
	while (!spreadPointChunk(x,y,value,distance,width,height,field,max,cursor,1024)) {}
}

bool spreadPointChunk(int x, int y, int value, int distance, int width, int height,
                      std::span<Uint32> field, Uint32& max, size_t& cursor, size_t budget)
{
	if (distance<=0) return true;
	const size_t side=size_t(distance)*2+2, count=side*side;
	const size_t end=cursor+std::min(budget,count-cursor);
	for (;cursor<end;++cursor)
	{
		const int relx=int(cursor/side)-distance-1, rely=int(cursor%side)-distance-1;
		const auto squared=std::int64_t(relx)*relx+std::int64_t(rely)*rely;
		if (squared>std::int64_t(distance)*distance) continue;
		// Large catalog-defined radii can wrap a small map more than once.
		const int targetX=((x+relx)%width+width)%width;
		const int targetY=((y+rely)%height+height)%height;
		auto& cell=field[size_t(targetX)*height+targetY];
		cell+=Uint32(std::int64_t(value)*(distance-squared/distance));
		max=std::max(max,cell);
	}
	return cursor==count;
}

} // namespace OverlayFill
