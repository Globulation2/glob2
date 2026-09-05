// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression test for the scroll-wheel modifier-routing bug in
// GameGUI::flushScrollWheelOrders(). The old code sampled SDL_GetModState()
// several times mid-expression (once snapshotted, twice live), so releasing
// SHIFT between the reads routed the accumulated scroll delta to the wrong
// building field. The decision is now the pure, SDL-free helper
// scrollWheelTarget(), sampled once per scroll event, so this test needs no
// SDL linkage — it just pins the routing truth table.

#include <cppunit/extensions/HelperMacros.h>

#include "ScrollWheelTarget.h"

class ScrollWheelTargetTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(ScrollWheelTargetTest);
		CPPUNIT_TEST(testEnabledNoShiftIsWorkers);
		CPPUNIT_TEST(testEnabledShiftIsStayRange);
		CPPUNIT_TEST(testEnabledCtrlIrrelevant);
		CPPUNIT_TEST(testDisabledCtrlIsWorkers);
		CPPUNIT_TEST(testDisabledShiftIsStayRange);
		CPPUNIT_TEST(testDisabledNeitherIsNone);
		CPPUNIT_TEST(testDisabledCtrlWinsOverShift);
	CPPUNIT_TEST_SUITE_END();

protected:
	// Setting ON: no SHIFT scrolls the assigned-worker count.
	void testEnabledNoShiftIsWorkers(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(false, false, true) == ScrollWheelTarget::MaxUnitWorking);
	}

	// Setting ON: SHIFT scrolls the flag stay range.
	void testEnabledShiftIsStayRange(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(true, false, true) == ScrollWheelTarget::UnitStayRange);
	}

	// Setting ON: CTRL has no effect; SHIFT alone decides.
	void testEnabledCtrlIrrelevant(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(false, true, true) == ScrollWheelTarget::MaxUnitWorking);
		CPPUNIT_ASSERT(scrollWheelTarget(true, true, true) == ScrollWheelTarget::UnitStayRange);
	}

	// Setting OFF: CTRL is required to scroll the assigned-worker count.
	void testDisabledCtrlIsWorkers(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(false, true, false) == ScrollWheelTarget::MaxUnitWorking);
	}

	// Setting OFF: SHIFT scrolls the flag stay range.
	void testDisabledShiftIsStayRange(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(true, false, false) == ScrollWheelTarget::UnitStayRange);
	}

	// Setting OFF: no modifier scrolls nothing.
	void testDisabledNeitherIsNone(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(false, false, false) == ScrollWheelTarget::None);
	}

	// Setting OFF: CTRL wins when both CTRL and SHIFT are held (matches the
	// original else-if order, where the CTRL branch was tested first).
	void testDisabledCtrlWinsOverShift(void)
	{
		CPPUNIT_ASSERT(scrollWheelTarget(true, true, false) == ScrollWheelTarget::MaxUnitWorking);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(ScrollWheelTargetTest);
