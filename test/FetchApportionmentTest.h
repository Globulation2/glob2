// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <cppunit/extensions/HelperMacros.h>

// Tests for FetchApportionment::rank, the order in which a building hires
// fetchers for the several resources it wants at once.
class FetchApportionmentTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( FetchApportionmentTest );
		CPPUNIT_TEST( testEqualTargetsInterleave );
		CPPUNIT_TEST( testUnequalTargetsFollowTheRatio );
		CPPUNIT_TEST( testEverySlotIsFilledExactlyToTarget );
		CPPUNIT_TEST( testSatisfiedResourcesAreDropped );
		CPPUNIT_TEST( testDeliveriesAlreadyLandedCount );
		CPPUNIT_TEST( testTiesKeepTheLowerResourceIndex );
		CPPUNIT_TEST( testNothingWantedRanksNothing );
	CPPUNIT_TEST_SUITE_END();

public:
	void testEqualTargetsInterleave();
	void testUnequalTargetsFollowTheRatio();
	void testEverySlotIsFilledExactlyToTarget();
	void testSatisfiedResourcesAreDropped();
	void testDeliveriesAlreadyLandedCount();
	void testTiesKeepTheLowerResourceIndex();
	void testNothingWantedRanksNothing();
};
