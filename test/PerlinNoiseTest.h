// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

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
