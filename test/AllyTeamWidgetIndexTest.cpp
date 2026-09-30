// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glob2Test.h"

#include "AllyTeamWidgetIndex.h"

// Regression tests for allyTeamNumberToWidgetIndex, the 1-based header value
// to 0-based widget row mapping shared by CustomGameOtherOptions and the map
// editor's TeamsEditor. The editor used to subtract 1 unchecked, so a header
// carrying 0 or a value above the team count reached MultiTextButton::setIndex
// (vector::at) with an out-of-range index and threw on dialog open.
class AllyTeamWidgetIndexTest
{
public:

protected:
	void testWellFormedValuesMapToValueMinusOne(void)
	{
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(1, 4));
		CHECK_EQ(1, allyTeamNumberToWidgetIndex(2, 4));
		CHECK_EQ(2, allyTeamNumberToWidgetIndex(3, 4));
		CHECK_EQ(3, allyTeamNumberToWidgetIndex(4, 4));
	}

	void testSingleTeam(void)
	{
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(1, 1));
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(2, 1));
	}

	void testZeroClampsToFirstRow(void)
	{
		// An uninitialized header carries 0, which used to underflow to setIndex(-1).
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(0, 4));
	}

	void testAboveTeamCountClampsToFirstRow(void)
	{
		// Only teamCount rows exist in the widget.
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(5, 4));
	}

	void testMaxUint8ClampsToFirstRow(void)
	{
		CHECK_EQ(0, allyTeamNumberToWidgetIndex(255, 4));
	}
};
TEST_SUITE("AllyTeamWidgetIndex")
{
	TEST_CASE_FIXTURE(AllyTeamWidgetIndexTest, "WellFormedValuesMapToValueMinusOne") { testWellFormedValuesMapToValueMinusOne(); }
	TEST_CASE_FIXTURE(AllyTeamWidgetIndexTest, "SingleTeam") { testSingleTeam(); }
	TEST_CASE_FIXTURE(AllyTeamWidgetIndexTest, "ZeroClampsToFirstRow") { testZeroClampsToFirstRow(); }
	TEST_CASE_FIXTURE(AllyTeamWidgetIndexTest, "AboveTeamCountClampsToFirstRow") { testAboveTeamCountClampsToFirstRow(); }
	TEST_CASE_FIXTURE(AllyTeamWidgetIndexTest, "MaxUint8ClampsToFirstRow") { testMaxUint8ClampsToFirstRow(); }
}
