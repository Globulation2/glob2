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
#include "NatSortTest.h"
CPPUNIT_TEST_SUITE_REGISTRATION( NatSortTest );

std::vector<ABResult> aBResults;

void NatSortTest::setUp()
{
}
void NatSortTest::tearDown()
{
	aBResults.clear();
}

void NatSortTest::testStrnatcmp()
{
	//case
	aBResults.push_back(ABResult("T", "t", -1));
	aBResults.push_back(ABResult("aBcDeF", "AbCdEf", 1));

	testMany(aBResults, strnatcmp);
}

void NatSortTest::testStrnatcasecmp()
{
	//case
	aBResults.push_back(ABResult("T", "t", 0));
	aBResults.push_back(ABResult("aBcDeF", "AbCdEf", 0));

	testMany(aBResults, strnatcasecmp);
}

void NatSortTest::testBothStrnatcmp()
{
	//TODO: what should happen if left/right is NULL?
	//aBResults.push_back(ABResult(NULL, NULL, 0));

	//equal
	aBResults.push_back(ABResult("", "", 0));
	aBResults.push_back(ABResult("a", "a", 0));
	aBResults.push_back(ABResult("tt", "tt", 0));

	//different
	aBResults.push_back(ABResult("b", "a", 1));
	aBResults.push_back(ABResult("a", "", 1));
	aBResults.push_back(ABResult("aa", "a", 1));

	//natural
	aBResults.push_back(ABResult("a13", "a5", 1));

	testMany(aBResults, strnatcasecmp);
}

void NatSortTest::testMany(std::vector<ABResult> expectedResults, int(&func)(
		const nat_char*, const nat_char*))
{
	std::vector<ABResult>::iterator aBResultsIterator = expectedResults.begin();
	for (; aBResultsIterator < expectedResults.end(); aBResultsIterator++)
	{
		const nat_char *left =
				(const nat_char*) aBResultsIterator->left.c_str();
		const nat_char *right =
				(const nat_char*) aBResultsIterator->right.c_str();
		int expectedResult = aBResultsIterator->result;

		testOne(left, right, expectedResult, func);
		testOne(right, left, -expectedResult, func);
	}
}
void NatSortTest::testOne(std::string leftString, std::string rightString,
		int expectedResult, int(&func)(const nat_char*, const nat_char*))
{
	const nat_char *left = (const nat_char*) leftString.c_str();
	const nat_char *right = (const nat_char*) rightString.c_str();

	std::string message = std::string("Expecting: ").append(left);
	if (expectedResult < 0)
	{
		message.append(" > ");
	}
	else if (expectedResult > 0)
	{
		message.append(" < ");
	}
	else
	{
		message.append(" == ");
	}
	message.append(right);

	int actualResult = func(left, right);

	CPPUNIT_ASSERT_EQUAL_MESSAGE(message, expectedResult, actualResult);
}
