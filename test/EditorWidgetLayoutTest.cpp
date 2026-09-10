// SPDX-License-Identifier: GPL-3.0-or-later

#include <cppunit/extensions/HelperMacros.h>
#include "WidgetRectangle.h"

class EditorWidgetLayoutTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(EditorWidgetLayoutTest);
	CPPUNIT_TEST(testResizeKeepsClickCoordinatesAndEdges);
	CPPUNIT_TEST(testRepeatedResizeDoesNotDrift);
	CPPUNIT_TEST(testOrdinaryRectanglesStayAbsolute);
	CPPUNIT_TEST_SUITE_END();

public:
	void testResizeKeepsClickCoordinatesAndEdges()
	{
		RightAnchoredWidgetRectangle area(widgetRectangle(656, 160, 128, 96), 800);
		// A size change can be followed by input before the next paint.
		area.updateWindowWidth(1000);
		CPPUNIT_ASSERT_EQUAL(856, area.x);
		CPPUNIT_ASSERT_EQUAL(160, area.y);
		CPPUNIT_ASSERT(!area.is_in(656, 160));
		CPPUNIT_ASSERT(area.is_in(856, 160));
		CPPUNIT_ASSERT(area.is_in(983, 255));
		CPPUNIT_ASSERT(!area.is_in(984, 255));
		CPPUNIT_ASSERT(!area.is_in(983, 256));
		// The same brush cell has the same local coordinates after moving.
		CPPUNIT_ASSERT_EQUAL(65, 921 - area.x);
		CPPUNIT_ASSERT_EQUAL(37, 197 - area.y);
	}

	void testRepeatedResizeDoesNotDrift()
	{
		RightAnchoredWidgetRectangle area(widgetRectangle(656, 160, 128, 96), 800);
		for (int width : {1024, 1024, 640, 1920, 800})
		{
			area.updateWindowWidth(width);
			CPPUNIT_ASSERT_EQUAL(16, width - (area.x + area.width));
			CPPUNIT_ASSERT_EQUAL(160, area.y);
			CPPUNIT_ASSERT_EQUAL(128, area.width);
			CPPUNIT_ASSERT_EQUAL(96, area.height);
		}
		CPPUNIT_ASSERT_EQUAL(656, area.x);
		// Controls created at a different window width use their own anchor.
		RightAnchoredWidgetRectangle later(widgetRectangle(880, 160, 128, 96), 1024);
		later.updateWindowWidth(800);
		CPPUNIT_ASSERT_EQUAL(area.x, later.x);
	}

	void testOrdinaryRectanglesStayAbsolute()
	{
		widgetRectangle map(0, 16, 640, 480);
		CPPUNIT_ASSERT(map.is_in(0, 16));
		CPPUNIT_ASSERT(!map.is_in(640, 16));
		CPPUNIT_ASSERT(!map.is_in(0, 496));
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(EditorWidgetLayoutTest);
