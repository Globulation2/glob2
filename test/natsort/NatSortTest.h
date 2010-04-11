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

#ifndef NATSORTTEST_H_
#define NATSORTTEST_H_

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

#endif /* NATSORTTEST_H_ */
