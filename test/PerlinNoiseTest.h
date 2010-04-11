/*
 Copyright (C) 2010 Leo Wandersleb

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

#ifndef PERLINNOISETEST_H_
#define PERLINNOISETEST_H_

#include <cppunit/extensions/HelperMacros.h>

class PerlinNoiseTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( PerlinNoiseTest );
		CPPUNIT_TEST( testConstructor );
		CPPUNIT_TEST( testNotZeroOne );
		CPPUNIT_TEST( testReseed );
		CPPUNIT_TEST( testReseedIntDifferent );
		CPPUNIT_TEST( testReseedIntSame );
		CPPUNIT_TEST( testnoise1d );
		CPPUNIT_TEST( testnoise2d );
		CPPUNIT_TEST( testnoise3d );
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp();
	void tearDown();

	void testConstructor();
	void testNotZeroOne();
	void testReseed();
	void testReseedIntDifferent();
	void testReseedIntSame();
	void testnoise1d();
	void testnoise2d();
	void testnoise3d();
};

#endif /* PERLINNOISETEST_H_ */
