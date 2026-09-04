// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

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
