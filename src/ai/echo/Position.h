// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

namespace AIEcho
{
	///A position on a map. Simple x and y cordinates, and a comparison operator for stoarge and maps and sets
	class position
	{
	public:
		position() : x(0), y(0) {}
		position(int x, int y) : x(x), y(y) {}
		int x;
		int y;
		bool operator<(const position& rhs) const
		{
			if(x!=rhs.x)
				return x<rhs.x;
			else
				return y<rhs.y;
		}
	};
}
