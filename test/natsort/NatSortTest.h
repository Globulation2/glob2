// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

#pragma once

#include <cppunit/extensions/HelperMacros.h>

extern "C"
{
#include "../../natsort/strnatcmp.c"
}

class ABResult
{
public:
	std::string left;
	std::string right;
	int result;
	ABResult(std::string left, std::string right, int result) :
		left(left), right(right), result(result)
	{
	}
};

class NatSortTest: public CppUnit::TestFixture
{
CPPUNIT_TEST_SUITE( NatSortTest );
		CPPUNIT_TEST( testStrnatcmp );
		CPPUNIT_TEST( testStrnatcasecmp );
		CPPUNIT_TEST( testBothStrnatcmp );
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp();
	void tearDown();

	void testStrnatcmp();
	void testStrnatcasecmp();
	void testBothStrnatcmp();
private:
	void testMany(std::vector<ABResult> expectedResults, int(&func)(
			const nat_char*, const nat_char*));
	void testOne(std::string leftString, std::string rightString, int expectedResult,
			int(&func)(const nat_char*, const nat_char*));
};

