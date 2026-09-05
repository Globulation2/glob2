// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for CS-051: CampaignMenuScreen::onAction's
// LIST_ELEMENT_SELECTED handler used `campaign.getMap(displayedListIndex)`,
// but the displayed list contains only *unlocked* maps while
// `campaign.maps[]` holds locked entries too. As soon as a campaign has
// any locked map ahead of an unlocked one (the natural case for
// non-linear unlock graphs), the indices diverge and clicking item N
// brings up the preview/description for the wrong mission.
//
// The fix routes selection through `Campaign::findUnlockedMap(name)` so
// that lookup is by the displayed name, not by list position.
//
// This harness proves both halves of the fix:
//   1. The OLD algorithm (`getMap(index)`) returns the WRONG entry on a
//      non-linear fixture — confirming the bug was real.
//   2. The NEW algorithm (`findUnlockedMap(name)`) returns the RIGHT
//      entry — confirming the fix works.
//
// Pre-fix tree: this harness fails to LINK because `Campaign::findUnlockedMap`
// does not exist. That is the "broken before" signal.
// Post-fix tree: this harness builds, runs, exits 0, and prints a
// deterministic golden line for diff-based verification.

#include "Campaign.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

// Build the regression fixture in memory:
//   maps[0] = "Map1" / "fake1.map"  unlocked
//   maps[1] = "Map2" / "fake2.map"  LOCKED
//   maps[2] = "Map3" / "fake3.map"  unlocked (unlocked-by Map1)
//
// The displayed mission list (which CampaignMenuScreen::repopulateAvailableMissions
// builds by filtering on isUnlocked) is therefore: ["Map1", "Map3"].
// Position 1 in the displayed list is "Map3" -- but maps[1] is "Map2".
// This is the index/name divergence the bug fix targets.
Campaign buildNonLinearFixture()
{
	Campaign c;
	c.setName("RegressionCampaign");

	CampaignMapEntry m1("Map1", "fake1.map");
	m1.unlockMap();
	c.appendMap(m1);

	CampaignMapEntry m2("Map2", "fake2.map");
	m2.lockMap();
	c.appendMap(m2);

	CampaignMapEntry m3("Map3", "fake3.map");
	m3.unlockMap();
	m3.getUnlockedByMaps().push_back("Map1");
	c.appendMap(m3);

	return c;
}

// Mirrors CampaignMenuScreen::repopulateAvailableMissions — builds the
// list of names the widget would display, in the same order.
std::vector<std::string> buildDisplayedList(Campaign& c)
{
	std::vector<std::string> displayed;
	for (unsigned i = 0; i < c.getMapCount(); ++i)
		if (c.getMap(i).isUnlocked())
			displayed.push_back(c.getMap(i).getMapName());
	return displayed;
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	std::printf("# CampaignSelectionHarness golden output\n");

	Campaign campaign = buildNonLinearFixture();
	std::vector<std::string> displayed = buildDisplayedList(campaign);

	std::printf("fixture: maps=%zu displayed=%zu\n",
	            campaign.getMapCount(), displayed.size());
	for (size_t i = 0; i < displayed.size(); ++i)
		std::printf("  displayed[%zu] = \"%s\"\n", i, displayed[i].c_str());

	EXPECT(campaign.getMapCount() == 3, "fixture should have 3 maps");
	EXPECT(displayed.size() == 2, "displayed list should hide the locked map");
	EXPECT(displayed[0] == "Map1", "displayed[0] should be Map1");
	EXPECT(displayed[1] == "Map3", "displayed[1] should be Map3 (skipping locked Map2)");

	// Simulate the user clicking the second item in the displayed list.
	const size_t userClickedIndex = 1;
	const std::string userClickedName = displayed[userClickedIndex];
	std::printf("user clicks displayed[%zu] = \"%s\"\n",
	            userClickedIndex, userClickedName.c_str());

	// === OLD algorithm (the bug) ===
	// CampaignMenuScreen used to do `campaign.getMap(getSelectionIndex())`,
	// treating the displayed-list index as a campaign.maps index.
	CampaignMapEntry& byIndex = campaign.getMap(static_cast<unsigned>(userClickedIndex));
	std::printf("[old]  campaign.getMap(%zu)              -> name=\"%s\" file=\"%s\"\n",
	            userClickedIndex, byIndex.getMapName().c_str(),
	            byIndex.getMapFileName().c_str());
	EXPECT(byIndex.getMapName() == "Map2",
	       "old algorithm reproduces the bug: returns Map2 instead of Map3");
	EXPECT(byIndex.getMapName() != userClickedName,
	       "old algorithm must disagree with the user's click on this fixture; "
	       "if this assertion fails the fixture no longer exercises the bug");

	// === NEW algorithm (the fix) ===
	// The post-fix CampaignMenuScreen routes selection through
	// Campaign::findUnlockedMap(displayedName).
	CampaignMapEntry* byName = campaign.findUnlockedMap(userClickedName);
	std::printf("[new]  campaign.findUnlockedMap(\"%s\") -> name=\"%s\" file=\"%s\"\n",
	            userClickedName.c_str(),
	            byName ? byName->getMapName().c_str() : "(null)",
	            byName ? byName->getMapFileName().c_str() : "(null)");
	EXPECT(byName != nullptr, "new algorithm should resolve the displayed name");
	EXPECT(byName && byName->getMapName() == "Map3",
	       "new algorithm should return the actually-clicked map");
	EXPECT(byName && byName->getMapFileName() == "fake3.map",
	       "new algorithm should yield the correct .map filename");

	// === Boundary cases the old code crashed/asserted on ===
	// Selection cleared (displayed name not in campaign): pre-fix
	// getMissionName() hit `assert(false)`; the helper returns nullptr so
	// the menu screen can no-op gracefully.
	CampaignMapEntry* missing = campaign.findUnlockedMap("DoesNotExist");
	std::printf("[new]  findUnlockedMap(\"DoesNotExist\") -> %s\n",
	            missing ? "FOUND (BUG)" : "nullptr");
	EXPECT(missing == nullptr, "lookup of missing name must return nullptr");

	// Lookup of a locked map's name: must not return the locked entry,
	// even if the displayed list somehow contained it.
	CampaignMapEntry* locked = campaign.findUnlockedMap("Map2");
	std::printf("[new]  findUnlockedMap(\"Map2\" locked)   -> %s\n",
	            locked ? "FOUND (BUG)" : "nullptr");
	EXPECT(locked == nullptr,
	       "findUnlockedMap must not return a locked entry");

	std::printf("result: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
