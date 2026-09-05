// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// This file holds the small, Map-free pieces of the AIEcho gradient system,
// kept separate so that determinism tests for `Gradient::expand_bfs` can link
// without dragging in the full game (Map, Game, GlobalContainer, ...).

#include "echo/Echo.h"

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
