// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression test for the post-delete selection clamp in list widgets
// (ChooseMapScreen's delete-game button). The old call-site expression
// `std::min(i, getCount() - 1)` underflowed to SIZE_MAX on an empty list
// and relied on implementation-defined int narrowing to land on -1.
// List::selectionAfterRemoval is the wraparound-free replacement: it is a
// pure, header-inline helper, so this test needs no SDL linkage.

#include <cppunit/extensions/HelperMacros.h>

#include "GUIList.h"

using GAGGUI::List;

class GUIListSelectionTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(GUIListSelectionTest);
		CPPUNIT_TEST(testEmptyListSelectsNothing);
		CPPUNIT_TEST(testLastEntryRemovedClampsToNewEnd);
		CPPUNIT_TEST(testMiddleEntryRemovedKeepsIndex);
	CPPUNIT_TEST_SUITE_END();

protected:
	void testEmptyListSelectsNothing(void)
	{
		// Deleting the last remaining file: old index 0, new count 0.
		// The old expression computed std::min(0u, SIZE_MAX) == 0 and
		// depended on narrowing; the helper must say "no selection".
		CPPUNIT_ASSERT(!List::selectionAfterRemoval(0, 0).has_value());
		CPPUNIT_ASSERT(!List::selectionAfterRemoval(7, 0).has_value());
	}

	void testLastEntryRemovedClampsToNewEnd(void)
	{
		// Deleting the bottom entry of a 4-entry list (index 3): the
		// selection moves up to the new last entry (index 2).
		std::optional<size_t> sel = List::selectionAfterRemoval(3, 3);
		CPPUNIT_ASSERT(sel.has_value());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), *sel);
	}

	void testMiddleEntryRemovedKeepsIndex(void)
	{
		// Deleting a middle entry: the same index now names the next
		// file down, which is the entry the user expects highlighted.
		std::optional<size_t> sel = List::selectionAfterRemoval(1, 3);
		CPPUNIT_ASSERT(sel.has_value());
		CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), *sel);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(GUIListSelectionTest);
