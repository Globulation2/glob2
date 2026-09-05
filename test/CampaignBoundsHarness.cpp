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
// CampaignLoadTestStubs.cpp against libgag_server).

#include "Campaign.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                  \
	do {                                                                   \
		if (!(cond)) {                                                     \
			std::fprintf(stderr, "FAIL: %s  (%s:%d)\n",                    \
			             (msg), __FILE__, __LINE__);                       \
			++failures;                                                    \
		}                                                                  \
	} while (0)

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

int main(int /*argc*/, char* /*argv*/[])
{
	std::printf("# CampaignBoundsHarness golden output\n");

	// === removeMap: in-bounds removal still works ===
	{
		Campaign c = buildFixture();
		c.removeMap(1);
		std::printf("removeMap(1): count=%zu\n", c.getMapCount());
		EXPECT(c.getMapCount() == 2, "in-bounds removal shrinks the list");
		EXPECT(c.getMap(0).getMapName() == "Map1", "entry before removed index kept");
		EXPECT(c.getMap(1).getMapName() == "Map3", "entry after removed index shifts down");
	}

	// === removeMap: out-of-range index is a no-op ===
	{
		Campaign c = buildFixture();
		c.removeMap(3);  // one past the end — the stale-index case
		std::printf("removeMap(3) on size 3: count=%zu\n", c.getMapCount());
		EXPECT(c.getMapCount() == 3, "one-past-the-end removal must be ignored");

		c.removeMap(static_cast<unsigned>(-1));  // pathological stale index
		std::printf("removeMap(UINT_MAX): count=%zu\n", c.getMapCount());
		EXPECT(c.getMapCount() == 3, "wildly out-of-range removal must be ignored");
		EXPECT(c.getMap(2).getMapName() == "Map3", "list contents untouched");
	}

	// === removeMap: last valid index is still removable ===
	{
		Campaign c = buildFixture();
		c.removeMap(2);
		std::printf("removeMap(2) on size 3: count=%zu\n", c.getMapCount());
		EXPECT(c.getMapCount() == 2, "last valid index is in bounds");
		EXPECT(c.getMap(1).getMapName() == "Map2", "remaining entries intact");
	}

	// === getMap: in-bounds access returns the right entry ===
	{
		Campaign c = buildFixture();
		EXPECT(c.getMap(0).getMapName() == "Map1", "getMap(0) returns first entry");
		EXPECT(c.getMap(2).getMapName() == "Map3", "getMap(size-1) returns last entry");
		std::printf("getMap(0)=\"%s\" getMap(2)=\"%s\"\n",
		            c.getMap(0).getMapName().c_str(),
		            c.getMap(2).getMapName().c_str());
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
		std::printf("getMap(3) on size 3: threw=%d\n", threw ? 1 : 0);
		EXPECT(threw, "one-past-the-end access must throw std::out_of_range");

		threw = false;
		try {
			(void)c.getMap(static_cast<unsigned>(-1));
		} catch (const std::out_of_range&) {
			threw = true;
		}
		std::printf("getMap(UINT_MAX): threw=%d\n", threw ? 1 : 0);
		EXPECT(threw, "wildly out-of-range access must throw std::out_of_range");
	}

	std::printf("result: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
