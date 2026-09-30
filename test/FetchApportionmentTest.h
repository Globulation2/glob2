// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include "Glob2Test.h"

// Tests for FetchApportionment::rank, the order in which a building hires
// fetchers for the several resources it wants at once.
class FetchApportionmentTest
{

public:
	void testEqualTargetsInterleave();
	void testUnequalTargetsFollowTheRatio();
	void testEverySlotIsFilledExactlyToTarget();
	void testSatisfiedResourcesAreDropped();
	void testDeliveriesAlreadyLandedCount();
	void testTiesKeepTheLowerResourceIndex();
	void testNothingWantedRanksNothing();
};
