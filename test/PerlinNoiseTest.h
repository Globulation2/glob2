// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Leo Wandersleb

#pragma once

#include "Glob2Test.h"

class PerlinNoiseTest
{

public:
	PerlinNoiseTest();
	~PerlinNoiseTest();

	void testConstructor();
	void testNotZeroOne();
	void testReseed();
	void testReseedIntDifferent();
	void testReseedIntSame();
	void testnoise1d();
	void testnoise2d();
	void testnoise3d();
};

