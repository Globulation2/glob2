/*
  Copyright (C) 2026 glob2 contributors

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

// This file holds the small, Map-free pieces of the AIEcho gradient system,
// kept separate so that determinism tests for `Gradient::expand_bfs` can link
// without dragging in the full game (Map, Game, GlobalContainer, ...).

#include "AIEcho.h"

#include <queue>

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace boost::logic;


GradientInfo::GradientInfo()
{
	needs_updated=indeterminate;
}


GradientInfo::~GradientInfo()
{

}


Gradient::Gradient(const GradientInfo& gi)
{
	gradient_info=gi;
	width=0;
}


void Gradient::expand_bfs(std::queue<position>& positions)
{
	const int height = static_cast<int>(gradient.size()) / width;
	while(!positions.empty())
	{
		position p=positions.front();
		positions.pop();

		int left=p.x-1;
		if(left<0)
			left+=width;
		int right=p.x+1;
		if(right>=width)
			right-=width;
		int up=p.y-1;
		if(up<0)
			up+=height;
		int down=p.y+1;
		if(down>=height)
			down-=height;
		const int center_h=p.x;
		const int center_y=p.y;
		const Sint16 n=gradient[get_pos(center_h, center_y)];

		// 8-neighbor BFS step. Push order is fixed for deterministic networking
		// (lockstep desyncs if any client sees the queue in a different order);
		// do not reorder.
		const position neighbors[8] = {
			position(left,     up),
			position(center_h, up),
			position(right,    up),
			position(left,     center_y),
			position(right,    center_y),
			position(left,     down),
			position(center_h, down),
			position(right,    down),
		};
		for (const position& nb : neighbors)
		{
			const int idx = get_pos(nb.x, nb.y);
			if (gradient[idx] == 0)
			{
				gradient[idx] = n + 1;
				positions.push(nb);
			}
		}
	}
}
