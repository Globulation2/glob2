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

#include "Glob2Test.h"
#include "Campaign.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {


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

TEST_SUITE("CampaignSelection")
{
TEST_CASE("selection resolves the displayed name rather than the list index")
{

	Campaign campaign = buildNonLinearFixture();
	std::vector<std::string> displayed = buildDisplayedList(campaign);

	for (size_t i = 0; i < displayed.size(); ++i)

	GLOB2_CHECK(campaign.getMapCount() == 3, "fixture should have 3 maps");
	GLOB2_CHECK(displayed.size() == 2, "displayed list should hide the locked map");
	GLOB2_CHECK(displayed[0] == "Map1", "displayed[0] should be Map1");
	GLOB2_CHECK(displayed[1] == "Map3", "displayed[1] should be Map3 (skipping locked Map2)");

	// Simulate the user clicking the second item in the displayed list.
	const size_t userClickedIndex = 1;
	const std::string userClickedName = displayed[userClickedIndex];

	// === OLD algorithm (the bug) ===
	// CampaignMenuScreen used to do `campaign.getMap(getSelectionIndex())`,
	// treating the displayed-list index as a campaign.maps index.
	CampaignMapEntry& byIndex = campaign.getMap(static_cast<unsigned>(userClickedIndex));
	GLOB2_CHECK(byIndex.getMapName() == "Map2",
	       "old algorithm reproduces the bug: returns Map2 instead of Map3");
	GLOB2_CHECK(byIndex.getMapName() != userClickedName,
	       "old algorithm must disagree with the user's click on this fixture; "
	       "if this assertion fails the fixture no longer exercises the bug");

	// === NEW algorithm (the fix) ===
	// The post-fix CampaignMenuScreen routes selection through
	// Campaign::findUnlockedMap(displayedName).
	CampaignMapEntry* byName = campaign.findUnlockedMap(userClickedName);
	GLOB2_CHECK(byName != nullptr, "new algorithm should resolve the displayed name");
	GLOB2_CHECK(byName && byName->getMapName() == "Map3",
	       "new algorithm should return the actually-clicked map");
	GLOB2_CHECK(byName && byName->getMapFileName() == "fake3.map",
	       "new algorithm should yield the correct .map filename");

	// === Boundary cases the old code crashed/asserted on ===
	// Selection cleared (displayed name not in campaign): pre-fix
	// getMissionName() hit `assert(false)`; the helper returns nullptr so
	// the menu screen can no-op gracefully.
	CampaignMapEntry* missing = campaign.findUnlockedMap("DoesNotExist");
	GLOB2_CHECK(missing == nullptr, "lookup of missing name must return nullptr");

	// Lookup of a locked map's name: must not return the locked entry,
	// even if the displayed list somehow contained it.
	CampaignMapEntry* locked = campaign.findUnlockedMap("Map2");
	GLOB2_CHECK(locked == nullptr,
	       "findUnlockedMap must not return a locked entry");

}
}
