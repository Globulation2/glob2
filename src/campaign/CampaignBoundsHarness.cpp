// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for the Campaign map-list bounds fixes:
//   - Campaign::removeMap(n) with an out-of-range n used to call
//     maps.erase(maps.begin()+n) unguarded — undefined behavior on a stale
//     index from the campaign editor's list widget. It now silently ignores
//     out-of-range requests.
//   - Campaign::getMap(n) used to return maps[n] unguarded — UB when a UI
//     loop ran off the end. It now uses at(), so an out-of-range index
//     throws std::out_of_range deterministically.
//
// Link surface is identical to CampaignSelectionHarness (Campaign.cpp +
// the shared unit stubs in test/unit/stubs/ against libgag_server).

#include "Glob2Test.h"
#include "Campaign.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {


Campaign buildFixture()
{
	Campaign c;
	c.setName("BoundsCampaign");
	CampaignMapEntry m1("Map1", "fake1.map");
	c.appendMap(m1);
	CampaignMapEntry m2("Map2", "fake2.map");
	c.appendMap(m2);
	CampaignMapEntry m3("Map3", "fake3.map");
	c.appendMap(m3);
	return c;
}

}  // namespace

TEST_SUITE("CampaignBounds")
{
TEST_CASE("removeMap ignores and getMap rejects out-of-range indices")
{

	// === removeMap: in-bounds removal still works ===
	{
		Campaign c = buildFixture();
		c.removeMap(1);
		GLOB2_CHECK(c.getMapCount() == 2, "in-bounds removal shrinks the list");
		GLOB2_CHECK(c.getMap(0).getMapName() == "Map1", "entry before removed index kept");
		GLOB2_CHECK(c.getMap(1).getMapName() == "Map3", "entry after removed index shifts down");
	}

	// === removeMap: out-of-range index is a no-op ===
	{
		Campaign c = buildFixture();
		c.removeMap(3);  // one past the end — the stale-index case
		GLOB2_CHECK(c.getMapCount() == 3, "one-past-the-end removal must be ignored");

		c.removeMap(static_cast<unsigned>(-1));  // pathological stale index
		GLOB2_CHECK(c.getMapCount() == 3, "wildly out-of-range removal must be ignored");
		GLOB2_CHECK(c.getMap(2).getMapName() == "Map3", "list contents untouched");
	}

	// === removeMap: last valid index is still removable ===
	{
		Campaign c = buildFixture();
		c.removeMap(2);
		GLOB2_CHECK(c.getMapCount() == 2, "last valid index is in bounds");
		GLOB2_CHECK(c.getMap(1).getMapName() == "Map2", "remaining entries intact");
	}

	// === getMap: in-bounds access returns the right entry ===
	{
		Campaign c = buildFixture();
		GLOB2_CHECK(c.getMap(0).getMapName() == "Map1", "getMap(0) returns first entry");
		GLOB2_CHECK(c.getMap(2).getMapName() == "Map3", "getMap(size-1) returns last entry");
	}

	// === getMap: out-of-range index throws instead of UB ===
	{
		Campaign c = buildFixture();
		bool threw = false;
		try {
			(void)c.getMap(3);  // one past the end
		} catch (const std::out_of_range&) {
			threw = true;
		}
		GLOB2_CHECK(threw, "one-past-the-end access must throw std::out_of_range");

		threw = false;
		try {
			(void)c.getMap(static_cast<unsigned>(-1));
		} catch (const std::out_of_range&) {
			threw = true;
		}
		GLOB2_CHECK(threw, "wildly out-of-range access must throw std::out_of_range");
	}

}
}
