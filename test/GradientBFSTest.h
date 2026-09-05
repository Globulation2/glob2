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

#pragma once

#include <cppunit/extensions/HelperMacros.h>

#include <SDL_stdinc.h>
#include <vector>

namespace AIEcho { class position; }

// Friend of AIEcho::Gradients::Gradient (declared in AIEcho.h). Lives in the
// global namespace because production code only needs a single forward decl
// to friend it without dragging the cppunit headers into AIEcho.h.
class GradientBFSTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( GradientBFSTest );
		CPPUNIT_TEST( testEmptyQueueIsNoop );
		CPPUNIT_TEST( testSingleSourceMatchesChebyshev );
		CPPUNIT_TEST( testWrapAroundSmallGrid );
		CPPUNIT_TEST( testObstacleNotOverwritten );
		CPPUNIT_TEST( testMultipleSourcesUseMinimum );
		CPPUNIT_TEST( testIsolatedCellRemainsUnreachable );
		CPPUNIT_TEST( testQueueIsDrained );
	CPPUNIT_TEST_SUITE_END();

public:
	void testEmptyQueueIsNoop();
	void testSingleSourceMatchesChebyshev();
	void testWrapAroundSmallGrid();
	void testObstacleNotOverwritten();
	void testMultipleSourcesUseMinimum();
	void testIsolatedCellRemainsUnreachable();
	void testQueueIsDrained();

	// Drives the production Gradient::expand_bfs on a real instance. Static
	// member (rather than a free helper) so it inherits this fixture's friend
	// access to Gradient's private members.
	static std::vector<Sint16> run_bfs(int width, int height,
	                                   const std::vector<AIEcho::position>& sources,
	                                   const std::vector<AIEcho::position>& obstacles);
};

