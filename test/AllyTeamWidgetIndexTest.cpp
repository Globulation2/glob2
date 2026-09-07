// SPDX-License-Identifier: GPL-3.0-or-later

#include <cppunit/extensions/HelperMacros.h>

#include "AllyTeamWidgetIndex.h"

// Regression tests for allyTeamNumberToWidgetIndex, the 1-based header value
// to 0-based widget row mapping shared by CustomGameOtherOptions and the map
// editor's TeamsEditor. The editor used to subtract 1 unchecked, so a header
// carrying 0 or a value above the team count reached MultiTextButton::setIndex
// (vector::at) with an out-of-range index and threw on dialog open.
class AllyTeamWidgetIndexTest : public CPPUNIT_NS::TestCase
{
	CPPUNIT_TEST_SUITE(AllyTeamWidgetIndexTest);
		CPPUNIT_TEST(testWellFormedValuesMapToValueMinusOne);
		CPPUNIT_TEST(testSingleTeam);
		CPPUNIT_TEST(testZeroClampsToFirstRow);
		CPPUNIT_TEST(testAboveTeamCountClampsToFirstRow);
		CPPUNIT_TEST(testMaxUint8ClampsToFirstRow);
	CPPUNIT_TEST_SUITE_END();

protected:
	void testWellFormedValuesMapToValueMinusOne(void)
	{
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(1, 4));
		CPPUNIT_ASSERT_EQUAL(1, allyTeamNumberToWidgetIndex(2, 4));
		CPPUNIT_ASSERT_EQUAL(2, allyTeamNumberToWidgetIndex(3, 4));
		CPPUNIT_ASSERT_EQUAL(3, allyTeamNumberToWidgetIndex(4, 4));
	}

	void testSingleTeam(void)
	{
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(1, 1));
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(2, 1));
	}

	void testZeroClampsToFirstRow(void)
	{
		// An uninitialized header carries 0, which used to underflow to setIndex(-1).
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(0, 4));
	}

	void testAboveTeamCountClampsToFirstRow(void)
	{
		// Only teamCount rows exist in the widget.
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(5, 4));
	}

	void testMaxUint8ClampsToFirstRow(void)
	{
		CPPUNIT_ASSERT_EQUAL(0, allyTeamNumberToWidgetIndex(255, 4));
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(AllyTeamWidgetIndexTest);
