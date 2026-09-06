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

#include "GradientBFSTest.h"
#include "echo/Echo.h"

#include <algorithm>
#include <cstdlib>
#include <queue>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION( GradientBFSTest );

using AIEcho::position;
using AIEcho::Gradients::Gradient;
using AIEcho::Gradients::GradientInfo;

std::vector<Sint16> GradientBFSTest::run_bfs(int width, int height,
                                             const std::vector<position>& sources,
                                             const std::vector<position>& obstacles)
{
	GradientInfo gi;
	Gradient g(gi);
	g.width = width;
	g.gradient.assign(width * height, 0);
	std::queue<position> q;
	for (const auto& s : sources)
	{
		g.gradient[s.y * width + s.x] = 2;
		q.push(s);
	}
	for (const auto& o : obstacles)
		g.gradient[o.y * width + o.x] = 1;
	g.expand_bfs(q);
	CPPUNIT_ASSERT(q.empty());
	return g.gradient;
}

namespace
{
	Sint16 at(const std::vector<Sint16>& g, int width, int x, int y)
	{
		return g[y * width + x];
	}

	// Toroidal Chebyshev distance under 8-connectivity. This is the math
	// reference for the BFS output on an obstacle-free grid.
	int chebyshev_torus(int x1, int y1, int x2, int y2, int w, int h)
	{
		int dx = std::abs(x1 - x2);
		int dy = std::abs(y1 - y2);
		dx = std::min(dx, w - dx);
		dy = std::min(dy, h - dy);
		return std::max(dx, dy);
	}
}

void GradientBFSTest::testEmptyQueueIsNoop()
{
	auto g = run_bfs(4, 4, {}, {});
	for (auto v : g)
		CPPUNIT_ASSERT_EQUAL(Sint16(0), v);
}

void GradientBFSTest::testSingleSourceMatchesChebyshev()
{
	// 5x5 torus, single source at (2,2). Every other cell is within Chebyshev
	// distance 2 under 8-connectivity, so expected gradient = dist + 2.
	const int W = 5, H = 5;
	auto g = run_bfs(W, H, { position(2, 2) }, {});
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Sint16 expected = static_cast<Sint16>(chebyshev_torus(x, y, 2, 2, W, H) + 2);
			CPPUNIT_ASSERT_EQUAL(expected, at(g, W, x, y));
		}
}

void GradientBFSTest::testWrapAroundSmallGrid()
{
	// 4x4 torus, source at (0,0). (3,3) is reached via wrap (Chebyshev = 1)
	// rather than the longer non-wrapped path.
	const int W = 4, H = 4;
	auto g = run_bfs(W, H, { position(0, 0) }, {});
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Sint16 expected = static_cast<Sint16>(chebyshev_torus(x, y, 0, 0, W, H) + 2);
			CPPUNIT_ASSERT_EQUAL(expected, at(g, W, x, y));
		}
	CPPUNIT_ASSERT_EQUAL(Sint16(3), at(g, W, 3, 3));
}

void GradientBFSTest::testObstacleNotOverwritten()
{
	// 3x3 torus, source at (0,0), obstacle at (1,1). On a 3x3 torus all 8
	// non-source cells are direct neighbors of (0,0), so reachable cells get 3
	// and the obstacle stays at 1.
	const int W = 3, H = 3;
	auto g = run_bfs(W, H, { position(0, 0) }, { position(1, 1) });
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Sint16 expected;
			if (x == 0 && y == 0) expected = 2;
			else if (x == 1 && y == 1) expected = 1;
			else expected = 3;
			CPPUNIT_ASSERT_EQUAL(expected, at(g, W, x, y));
		}
}

void GradientBFSTest::testMultipleSourcesUseMinimum()
{
	// 6x6 torus, sources at (0,0) and (5,5). Each cell takes the minimum
	// Chebyshev distance to either source, plus 2.
	const int W = 6, H = 6;
	auto g = run_bfs(W, H, { position(0, 0), position(5, 5) }, {});
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			int d1 = chebyshev_torus(x, y, 0, 0, W, H);
			int d2 = chebyshev_torus(x, y, 5, 5, W, H);
			Sint16 expected = static_cast<Sint16>(std::min(d1, d2) + 2);
			CPPUNIT_ASSERT_EQUAL(expected, at(g, W, x, y));
		}
}

void GradientBFSTest::testIsolatedCellRemainsUnreachable()
{
	// 7x7 torus, source at (0,0), obstacle ring of 8 obstacles fully enclosing
	// (3,3). The center has no non-obstacle neighbor and must stay 0.
	const int W = 7, H = 7;
	std::vector<position> ring = {
		position(2, 2), position(3, 2), position(4, 2),
		position(2, 3),                 position(4, 3),
		position(2, 4), position(3, 4), position(4, 4),
	};
	auto g = run_bfs(W, H, { position(0, 0) }, ring);

	CPPUNIT_ASSERT_EQUAL(Sint16(0), at(g, W, 3, 3));
	for (const auto& cell : ring)
		CPPUNIT_ASSERT_EQUAL(Sint16(1), at(g, W, cell.x, cell.y));
}

void GradientBFSTest::testQueueIsDrained()
{
	// Postcondition asserted inside run_bfs(), but verified explicitly here too
	// in case run_bfs is later refactored.
	GradientInfo gi;
	Gradient g(gi);
	g.width = 4;
	g.gradient.assign(16, 0);
	g.gradient[0] = 2;
	std::queue<position> q;
	q.push(position(0, 0));

	g.expand_bfs(q);

	CPPUNIT_ASSERT(q.empty());
}
