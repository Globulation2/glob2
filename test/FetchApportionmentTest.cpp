// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "FetchApportionmentTest.h"

#include "FetchApportionment.h"

#include <string>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION( FetchApportionmentTest );

namespace
{
	// Hires `hires` fetchers one at a time, each for whatever rank() puts first,
	// and returns the resource indices in the order they were staffed. This is
	// exactly how subscribeToBringResourcesStep consumes the ranking: one unit
	// per call, with served incremented by the subscription itself.
	std::vector<int> hireSequence(std::vector<int> targets, std::vector<int> served, int hires)
	{
		std::vector<int> out;
		std::vector<int> order(targets.size());
		for(int i=0; i<hires; ++i)
		{
			int wanted = FetchApportionment::rank(targets.data(), served.data(),
			                                      (int)targets.size(), order.data());
			if(wanted==0)
				break;
			out.push_back(order[0]);
			served[order[0]]++;
		}
		return out;
	}

	// "wsws" style rendering so a failure message names the sequence, not indices.
	std::string spell(const std::vector<int>& sequence, const std::string& letters)
	{
		std::string out;
		for(int r : sequence)
			out += letters[r];
		return out;
	}
}

void FetchApportionmentTest::testEqualTargetsInterleave()
{
	// A market wanting 4 wood and 4 stone alternates instead of sending everyone
	// to whichever resource happens to be nearer.
	CPPUNIT_ASSERT_EQUAL(std::string("wswswsws"),
		spell(hireSequence({4, 4}, {0, 0}, 8), "ws"));
}

void FetchApportionmentTest::testUnequalTargetsFollowTheRatio()
{
	// 4 wood and 1 algue: two wood, then the algue, then the rest of the wood.
	CPPUNIT_ASSERT_EQUAL(std::string("wwaww"),
		spell(hireSequence({4, 1}, {0, 0}, 5), "wa"));
}

void FetchApportionmentTest::testEverySlotIsFilledExactlyToTarget()
{
	// Asking for more hires than there are deliveries stops at the targets, and
	// each resource ends up with exactly its own target. This is the property
	// that caps the oversubscription: 8 wanted deliveries, never a ninth hire.
	std::vector<int> sequence = hireSequence({4, 4}, {0, 0}, 100);
	CPPUNIT_ASSERT_EQUAL((size_t)8, sequence.size());
	int perResource[2] = {0, 0};
	for(int r : sequence)
		perResource[r]++;
	CPPUNIT_ASSERT_EQUAL(4, perResource[0]);
	CPPUNIT_ASSERT_EQUAL(4, perResource[1]);
}

void FetchApportionmentTest::testSatisfiedResourcesAreDropped()
{
	// A resource whose subscriptions already cover its target is not offered
	// again, however large its target was.
	int targets[2] = {10, 2};
	int served[2] = {10, 0};
	int order[2];
	CPPUNIT_ASSERT_EQUAL(1, FetchApportionment::rank(targets, served, 2, order));
	CPPUNIT_ASSERT_EQUAL(1, order[0]);
}

void FetchApportionmentTest::testDeliveriesAlreadyLandedCount()
{
	// served mixes deliveries that arrived with units still walking; a building
	// holding 3 of its 4 wood and none of its 4 stone staffs stone next.
	CPPUNIT_ASSERT_EQUAL(std::string("sssws"),
		spell(hireSequence({4, 4}, {3, 0}, 5), "ws"));
}

void FetchApportionmentTest::testTiesKeepTheLowerResourceIndex()
{
	// Equal priorities must resolve the same way on every platform, so the
	// lockstep simulation cannot diverge between clients.
	int targets[3] = {2, 2, 2};
	int served[3] = {0, 0, 0};
	int order[3];
	CPPUNIT_ASSERT_EQUAL(3, FetchApportionment::rank(targets, served, 3, order));
	CPPUNIT_ASSERT_EQUAL(0, order[0]);
	CPPUNIT_ASSERT_EQUAL(1, order[1]);
	CPPUNIT_ASSERT_EQUAL(2, order[2]);
}

void FetchApportionmentTest::testNothingWantedRanksNothing()
{
	int targets[3] = {0, 2, 5};
	int served[3] = {0, 2, 5};
	int order[3];
	CPPUNIT_ASSERT_EQUAL(0, FetchApportionment::rank(targets, served, 3, order));
}
