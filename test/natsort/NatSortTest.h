// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

#pragma once

#include "Glob2Test.h"
#include <vector>
#include <string>

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

class NatSortTest
{

public:
	NatSortTest();
	~NatSortTest();

	void testStrnatcmp();
	void testStrnatcasecmp();
	void testBothStrnatcmp();
private:
	void testMany(std::vector<ABResult> expectedResults, int(&func)(
			const nat_char*, const nat_char*));
	void testOne(std::string leftString, std::string rightString, int expectedResult,
			int(&func)(const nat_char*, const nat_char*));
};

